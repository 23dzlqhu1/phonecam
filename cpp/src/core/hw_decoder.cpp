#include "core/hw_decoder.h"
#include <QDebug>

namespace phonecam {

// Hardware device types to try, in order of preference
static const char* HW_DEVICE_TYPES[] = {
    "d3d11va",
    "cuda",
    "dxva2",
    nullptr
};

// Callback for FFmpeg to select hardware pixel format
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
                                         const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;
    // Find the hardware pixel format we stored in opaque
    enum AVPixelFormat desired = static_cast<AVPixelFormat>(
        reinterpret_cast<intptr_t>(ctx->opaque));
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == desired) return *p;
    }
    qWarning() << "[HW-DEC] Failed to get hw format, falling back to sw";
    return pix_fmts[0];
}

HwDecoder::HwDecoder(QObject* parent) : QObject(parent) {
    m_frame = av_frame_alloc();
    m_swFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
}

HwDecoder::~HwDecoder() {
    close();
    av_frame_free(&m_frame);
    av_frame_free(&m_swFrame);
    av_packet_free(&m_packet);
}

bool HwDecoder::init() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qWarning() << "[HW-DEC] H.264 decoder not found";
        return false;
    }

    // Try hardware decoders first
    for (int i = 0; HW_DEVICE_TYPES[i]; ++i) {
        if (tryInitHw(codec, HW_DEVICE_TYPES[i])) {
            m_isHw = true;
            qDebug() << "[HW-DEC] Using hardware decoder:" << HW_DEVICE_TYPES[i];
            return true;
        }
    }

    // Fallback to software decoder
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qWarning() << "[HW-DEC] Failed to allocate codec context";
        return false;
    }

    m_codecCtx->thread_count = 2;
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qWarning() << "[HW-DEC] Failed to open software decoder";
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_isHw = false;
    qDebug() << "[HW-DEC] Using software decoder";
    return true;
}

bool HwDecoder::tryInitHw(const AVCodec* codec, const char* hw_device_type) {
    AVHWDeviceType type = av_hwdevice_find_type_by_name(hw_device_type);
    if (type == AV_HWDEVICE_TYPE_NONE) return false;

    // Find the matching hardware pixel format
    enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
    if (hw_pix_fmt == AV_PIX_FMT_NONE) return false;

    // Allocate hardware device context
    AVBufferRef* hw_device_ctx = nullptr;
    if (av_hwdevice_ctx_create(&hw_device_ctx, type, nullptr, nullptr, 0) < 0) {
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        av_buffer_unref(&hw_device_ctx);
        return false;
    }

    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    ctx->thread_count = 1;  // HW decoders don't benefit from threading
    m_hwPixFmt = hw_pix_fmt;  // Store for frame format checking
    // FFmpeg 8.x: use get_format callback instead of hw_pix_fmt
    ctx->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(hw_pix_fmt));
    ctx->get_format = get_hw_format;

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        av_buffer_unref(&hw_device_ctx);
        return false;
    }

    m_codecCtx = ctx;
    m_hwDeviceCtx = hw_device_ctx;
    m_hwPixFmt = hw_pix_fmt;
    return true;
}

QImage HwDecoder::decode(const uint8_t* data, int size) {
    if (!m_codecCtx || !data || size <= 0) return {};

    av_packet_unref(m_packet);
    // Must use av_new_packet for proper AV_INPUT_BUFFER_PADDING_SIZE padding.
    // FFmpeg's bitstream readers may read past the end of valid data.
    if (av_new_packet(m_packet, size) < 0) {
        qWarning() << "[HW-DEC] av_new_packet failed";
        return {};
    }
    std::memcpy(m_packet->data, data, size);

    // Debug: log first bytes of each NAL
    // MEDIUM-1 note: static variable, protected by m_mutex held in DecodeWorker::decodeFrame
    static int decodeCount = 0;
    if (decodeCount < 10 || decodeCount % 300 == 0) {
        qDebug() << "[HW-DEC] decode #" << decodeCount
                 << "size=" << size
                 << "first_bytes="
                 << QByteArray(reinterpret_cast<const char*>(data), qMin(size, 16)).toHex();
    }
    decodeCount++;

    // Check if this is an SPS NAL (type 7) - if so, flush decoder to reset state
    // SPS starts with 00 00 00 01 67 (Annex-B) or 00 00 01 67
    if (size >= 5) {
        bool isSps = false;
        if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1 && (data[4] & 0x1F) == 7) {
            isSps = true;
        } else if (data[0] == 0 && data[1] == 0 && data[2] == 1 && (data[3] & 0x1F) == 7) {
            isSps = true;
        }
        if (isSps) {
            qDebug() << "[HW-DEC] SPS detected at frame" << decodeCount << "- flushing decoder for clean start";
            avcodec_flush_buffers(m_codecCtx);
        }
    }

    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return {};  // Need more data
        // Non-fatal errors (missing SPS/PPS before first keyframe): log and continue
        if (decodeCount <= 20) {
            qDebug() << "[HW-DEC] send_packet returned" << ret << "(non-fatal, waiting for keyframe)";
        }
        return {};
    }

    ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return {};  // Need more packets
        if (ret == AVERROR_EOF) return {};
        qWarning() << "[HW-DEC] receive_frame error:" << ret;
        return {};
    }

    AVFrame* frame = m_frame;

    // If hardware, download to CPU
    if (m_isHw && m_frame->format == m_hwPixFmt) {
        av_frame_unref(m_swFrame);
        ret = av_hwframe_transfer_data(m_swFrame, m_frame, 0);
        if (ret < 0) {
            qWarning() << "[HW-DEC] hw frame transfer error:" << ret;
            return {};
        }
        frame = m_swFrame;
    }

    QImage img = frameToQImage(frame);
    av_frame_unref(m_frame);
    if (frame == m_swFrame) av_frame_unref(m_swFrame);
    return img;
}

QImage HwDecoder::frameToQImage(AVFrame* frame) {
    int w = frame->width;
    int h = frame->height;

    // Convert to RGB24 using swscale
    // Recreate sws context if resolution or format changed
    if (m_swsCtx && (m_lastSwsW != w || m_lastSwsH != h || m_lastSwsFmt != frame->format)) {
        qDebug() << "[HW-DEC] Resolution/format changed"
                 << m_lastSwsW << "x" << m_lastSwsH << "→" << w << "x" << h
                 << "fmt" << m_lastSwsFmt << "→" << frame->format;
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (!m_swsCtx) {
        m_swsCtx = sws_getContext(
            w, h, static_cast<AVPixelFormat>(frame->format),
            w, h, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsCtx) {
            qWarning() << "[HW-DEC] sws_getContext failed";
            return {};
        }
        m_lastSwsW = w;
        m_lastSwsH = h;
        m_lastSwsFmt = static_cast<AVPixelFormat>(frame->format);
    }

    QImage img(w, h, QImage::Format_RGB888);
    uint8_t* dst[4] = { img.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { img.bytesPerLine(), 0, 0, 0 };

    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h, dst, dstLinesize);
    return img;
}

void HwDecoder::flush() {
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
}

void HwDecoder::close() {
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    m_isHw = false;
    m_hwPixFmt = AV_PIX_FMT_NONE;
}

} // namespace phonecam
