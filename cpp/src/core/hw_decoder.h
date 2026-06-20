#pragma once
#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QFile>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include "core/decoded_frame.h"

namespace phonecam {

// FFmpeg H.264 decoder with hardware acceleration support.
// Tries D3D11VA → NVDEC → DXVA2 → software fallback.
// NOT thread-safe externally — use mutex() if sharing across threads.
class HwDecoder : public QObject {
    Q_OBJECT
public:
    HwDecoder(QObject* parent = nullptr);
    ~HwDecoder() override;

    // Initialize the decoder. Returns false on failure.
    bool init();

    // Decode a single H.264 NAL unit to QImage (RGB888).
    // Returns null QImage on failure.
    QImage decode(const uint8_t* data, int size);

    // Decode to AVFrame wrapper — skips QImage/RGB conversion.
    // Returns valid DecodedFrame on success, empty on failure.
    // Frame is already transferred to CPU if hardware decoder is active.
    DecodedFrame decodeFrame(const uint8_t* data, int size);

    bool isHardware() const { return m_isHw; }
    bool isInitialized() const { return m_codecCtx != nullptr; }
    void flush();
    void close();

    // ── P0 diagnostic controls ──
    void setForceSoftware(bool force) { m_forceSw = force; }
    bool isForceSoftware() const { return m_forceSw; }
    const char* decoderTypeName() const;
    void enableH264Dump(const QString& filePath);
    void disableH264Dump();

    // Mutex for external thread synchronization
    std::mutex& mutex() { return m_mutex; }

private:
    bool tryInitHw(const AVCodec* codec, const char* hw_device_type);
    QImage frameToQImage(AVFrame* frame);

    AVCodecContext* m_codecCtx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVFrame* m_swFrame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsCtx = nullptr;
    int m_lastSwsW = 0;
    int m_lastSwsH = 0;
    AVPixelFormat m_lastSwsFmt = AV_PIX_FMT_NONE;
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    QByteArray m_lastSps;
    bool m_isHw = false;
    bool m_forceSw = false;
    std::mutex m_mutex;

    // H264 dump state
    bool m_dumpEnabled = false;
    QFile m_dumpFile;
    int m_dumpFrameCount = 0;
    static constexpr int kDumpMaxFrames = 150; // ~5 seconds at 30fps
};

} // namespace phonecam
