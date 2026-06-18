#pragma once
#include <QObject>
#include <QImage>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

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

    bool isHardware() const { return m_isHw; }
    bool isInitialized() const { return m_codecCtx != nullptr; }  // Only safe to call from decode thread
    void flush();
    void close();

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
    bool m_isHw = false;
    std::mutex m_mutex;
};

} // namespace phonecam
