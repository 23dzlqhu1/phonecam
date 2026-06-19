#include "core/hw_decoder.h"
#include <QDebug>
#include <cstring>

namespace phonecam {

// Hardware device types to try, in order of preference
static const char* HW_DEVICE_TYPES[] = {
    "d3d11va",
    "cuda",
    "dxva2",
    nullptr
};

static int start_code_size_at(const uint8_t* data, int size, int pos) {
    if (pos + 4 <= size &&
        data[pos] == 0 && data[pos + 1] == 0 &&
        data[pos + 2] == 0 && data[pos + 3] == 1) {
        return 4;
    }
    if (pos + 3 <= size &&
        data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1) {
        return 3;
    }
    return 0;
}

static int find_start_code(const uint8_t* data, int size, int from) {
    for (int i = from; i + 3 <= size; ++i) {
        if (start_code_size_at(data, size, i) != 0) {
            return i;
        }
    }
    return -1;
}

static QByteArray extract_first_sps(const uint8_t* data, int size) {
    int pos = 0;
    while (pos < size) {
        const int start = find_start_code(data, size, pos);
        if (start < 0) return {};

        const int startCodeSize = start_code_size_at(data, size, start);
        const int nalStart = start + startCodeSize;
        if (nalStart >= size) return {};

        const int nalType = data[nalStart] & 0x1F;
        const int nextStart = find_start_code(data, size, nalStart + 1);
        const int nalEnd = nextStart >= 0 ? nextStart : size;

        if (nalType == 7) {
            return QByteArray(reinterpret_cast<const char*>(data + nalStart), nalEnd - nalStart);
        }

        pos = nalEnd;
    }
    return {};
}

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

    // Try hardware decoders first (unless forced software)
    if (!m_forceSw) {
        for (int i = 0; HW_DEVICE_TYPES[i]; ++i) {
            if (tryInitHw(codec, HW_DEVICE_TYPES[i])) {
                m_isHw = true;
                qDebug() << "[HW-DEC] Using hardware decoder:" << HW_DEVICE_TYPES[i];
                return true;
            }
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

    // Debug: log first bytes of each NAL
    // MEDIUM-1 note: static variable, protected by m_mutex held in DecodeWorker::decodeFrame
    static int decodeCount = 0;
    if (decodeCount < 5 || decodeCount % 300 == 0) {
        qDebug() << "[HW-DEC] decoder=" << decoderTypeName()
                 << "decode#" << decodeCount
                 << "size=" << size
                 << "first_bytes="
                 << QByteArray(reinterpret_cast<const char*>(data), qMin(size, 16)).toHex();
    }
    decodeCount++;

    const QByteArray sps = extract_first_sps(data, size);
    if (!sps.isEmpty()) {
        const bool spsChanged = !m_lastSps.isEmpty() && m_lastSps != sps;
        if (spsChanged) {
            // Camera switches can change decoder parameters. Hardware decoders need a full rebuild.
            qDebug() << "[HW-DEC] SPS changed at frame" << decodeCount
                     << "- hard resetting decoder for config change";
            close();
            if (!init() || !m_codecCtx) {
                qWarning() << "[HW-DEC] Decoder reinit failed after SPS change; dropping packet";
                return {};
            }
        }
        m_lastSps = sps;
    }

    av_packet_unref(m_packet);
    // Must use av_new_packet for proper AV_INPUT_BUFFER_PADDING_SIZE padding.
    // FFmpeg's bitstream readers may read past the end of valid data.
    if (av_new_packet(m_packet, size) < 0) {
        qWarning() << "[HW-DEC] av_new_packet failed";
        return {};
    }
    std::memcpy(m_packet->data, data, size);

    // H264 dump: write raw network input (check if already has Annex-B start code)
    if (m_dumpEnabled && m_dumpFile.isOpen() && m_dumpFrameCount < kDumpMaxFrames) {
        bool hasStartCode = false;
        if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
            hasStartCode = true;
        else if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1)
            hasStartCode = true;
        if (hasStartCode) {
            m_dumpFile.write(reinterpret_cast<const char*>(data), size);
        } else {
            const uint8_t sc[4] = {0, 0, 0, 1};
            m_dumpFile.write(reinterpret_cast<const char*>(sc), 4);
            m_dumpFile.write(reinterpret_cast<const char*>(data), size);
        }
        m_dumpFrameCount++;
        if (m_dumpFrameCount >= kDumpMaxFrames) {
            m_dumpFile.close();
            qDebug() << "[HW-DEC] H264 dump COMPLETE:" << m_dumpFile.fileName();
        }
    }

    if (!m_codecCtx) return {};

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

DecodedFrame HwDecoder::decodeFrame(const uint8_t* data, int size) {
    static int decodeCount = 0;

    if (!m_codecCtx || !data || size <= 0) return {};

    // ── SPS/PPS handling: use av_new_packet + memcpy (BUG-013 fix) ──
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00) {
        int nalType = 0;
        if (size >= 5) {
            int scSize = (data[2] == 0x00 && data[3] == 0x01) ? 4 : (data[2] == 0x01 ? 3 : 0);
            if (scSize > 0 && size > scSize) {
                nalType = (data[scSize] & 0x1F);
            }
        }
        if (nalType == 7) {  // SPS
            av_packet_unref(m_packet);
            if (av_new_packet(m_packet, size) < 0) return {};
            std::memcpy(m_packet->data, data, size);
            int ret = avcodec_send_packet(m_codecCtx, m_packet);
            av_packet_unref(m_packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) return {};
            // Drain any output
            ret = avcodec_receive_frame(m_codecCtx, m_frame);
            if (ret >= 0) av_frame_unref(m_frame);
            return {};
        }
    }

    // ── BUG-013 fix: use av_new_packet + memcpy for proper padding ──
    av_packet_unref(m_packet);
    if (av_new_packet(m_packet, size) < 0) {
        qWarning() << "[HW-DEC] decodeFrame: av_new_packet failed";
        return {};
    }
    std::memcpy(m_packet->data, data, size);

    decodeCount++;

    if (!m_codecCtx) return {};

    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return {};
        if (decodeCount <= 20) {
            qDebug() << "[HW-DEC] send_packet(Frame) returned" << ret;
        }
        return {};
    }

    ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) return {};
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
            av_frame_unref(m_frame);
            return {};
        }
        frame = m_swFrame;
    }

    // Clone the frame (ref-counted, caller owns)
    DecodedFrame result = DecodedFrame::clone(frame);
    av_frame_unref(m_frame);
    if (frame == m_swFrame) av_frame_unref(m_swFrame);

    // BUG-013: 增强日志 — 前 10 帧打印完整格式信息
    if (decodeCount <= 10) {
        const AVPixelFormat fmt = result.format();
        qDebug() << "[HW-DEC] decodeFrame#" << decodeCount
                 << "pix_fmt=" << av_get_pix_fmt_name(fmt)
                 << "size=" << result.width() << "x" << result.height()
                 << "linesize=[" << (result.frame ? result.frame->linesize[0] : 0)
                 << (result.frame ? result.frame->linesize[1] : 0)
                 << (result.frame ? result.frame->linesize[2] : 0)
                 << (result.frame ? result.frame->linesize[3] : 0) << "]"
                 << "data[0]=" << (result.frame && result.frame->data[0] ? "Y" : "null")
                 << "data[1]=" << (result.frame && result.frame->data[1] ? "U/UV" : "null")
                 << "data[2]=" << (result.frame && result.frame->data[2] ? "V" : "null")
                 << "data[3]=" << (result.frame && result.frame->data[3] ? "A" : "null");
    }

    return result;
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
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };

    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h, dst, dstLinesize);
    return img;
}

void HwDecoder::flush() {
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    // NOTE: do NOT free m_swsCtx here — it's reused across stream resets
    // and will be recreated automatically if resolution/format changes in frameToQImage().
}

void HwDecoder::close() {
    if (m_packet) {
        av_packet_unref(m_packet);
    }
    if (m_frame) {
        av_frame_unref(m_frame);
    }
    if (m_swFrame) {
        av_frame_unref(m_swFrame);
    }
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
    m_lastSwsW = 0;
    m_lastSwsH = 0;
    m_lastSwsFmt = AV_PIX_FMT_NONE;
    m_lastSps.clear();
}

const char* HwDecoder::decoderTypeName() const {
    if (!m_codecCtx) return "none";
    return m_isHw ? "hardware" : "software";
}

void HwDecoder::enableH264Dump(const QString& filePath) {
    m_dumpEnabled = true;
    m_dumpFrameCount = 0;
    m_dumpFile.setFileName(filePath);
    if (m_dumpFile.open(QIODevice::WriteOnly)) {
        qDebug() << "[HW-DEC] H264 dump STARTED:" << filePath;
    } else {
        qWarning() << "[HW-DEC] H264 dump file open FAILED:" << filePath;
        m_dumpEnabled = false;
    }
}

void HwDecoder::disableH264Dump() {
    if (m_dumpFile.isOpen()) {
        m_dumpFile.close();
        qDebug() << "[HW-DEC] H264 dump STOPPED, frames:" << m_dumpFrameCount;
    }
    m_dumpEnabled = false;
}

} // namespace phonecam
