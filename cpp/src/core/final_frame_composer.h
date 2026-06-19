#pragma once
#include <QImage>
#include "core/nv12_frame.h"
#include "core/decoded_frame.h"

struct SwsContext;

namespace phonecam {

// Transform state applied before composing the final 1280×720 NV12 frame.
struct FrameTransform {
    bool mirror = false;
    bool flip = false;
    int manualRotation = 0;   // 0/90/180/270 (user-controlled)
    int androidRotation = 0;  // 0/90/180/270 (device-reported)

    // Returns true if manual transforms (mirror/flip/rot) require QImage fallback.
    // androidRotation is handled natively in the fast YUV path via NV12 rotation.
    bool needsFallback() const {
        return mirror || flip || manualRotation != 0;
    }
};

// Composes a decoded frame into a fixed NV12 1280×720 frame.
//
// Canvas strategy: CONTAIN / LETTERBOX
// Two backends:
//   Fast (YUV):  DecodedFrame → sws_scale → NV12 1280×720  (no QImage, no RGB round-trip)
//   Legacy:      QImage → QPainter transforms → rgb888ToNv12  (full fallback, ~10× slower)
//
// This is the SINGLE place where transforms happen — both preview and
// virtual camera consume the same output, guaranteeing visual consistency.
class FinalFrameComposer {
public:
    static constexpr int kOutputWidth = 1280;
    static constexpr int kOutputHeight = 720;

    FinalFrameComposer();
    ~FinalFrameComposer();

    // ── Fast YUV path (primary) ──
    // Composes from a decoded AVFrame without QImage/RGB round-trip.
    // Falls back to the QImage path internally if:
    //   - transforms are active (mirror/flip/rotation)  → TODO: handle in swscale/avfilter
    //   - source pix_fmt is unsupported
    //   - sws_scale setup fails
    Nv12Frame composeFromDecodedFrame(const DecodedFrame& source, const FrameTransform& transform,
                                       uint32_t sequence = 0, uint64_t pts_ns = 0,
                                       double receive_ms = 0.0);

    // ── Legacy QImage path (fallback / A/B comparison) ──
    Nv12Frame compose(const QImage& source, const FrameTransform& transform,
                      uint32_t sequence = 0, uint64_t pts_ns = 0,
                      double receive_ms = 0.0);

    // Convenience: apply only transforms to a QImage (for debugging/fallback)
    static QImage applyTransforms(const QImage& source, const FrameTransform& transform);

private:
    // Legacy RGB helper
    static QByteArray qImageToNv12(const QImage& rgb, int outW, int outH,
                                    int offsetX, int offsetY, int drawW, int drawH);

    // YUV compose internals
    bool initSwsContext(AVPixelFormat srcFmt, int srcW, int srcH, int dstW, int dstH);
    static void fillNv12Black(uint8_t* data, int w, int h);
    static void copyNv12Region(uint8_t* dst, int dstW, int dstH,
                                const uint8_t* srcY, int srcYStride,
                                const uint8_t* srcUV, int srcUVStride,
                                int srcW, int srcH,
                                int offsetX, int offsetY);

    // NV12 rotation (androidRotation support in fast YUV path)
    static QByteArray rotateNv12(const uint8_t* srcY, int srcYStride,
                                  const uint8_t* srcUV, int srcUVStride,
                                  int srcW, int srcH, int rotation);  // 90/180/270

    // BUG-013: safe fallback for non-NV12 formats — sws_scale → RGB24 → QImage → compose()
    Nv12Frame fallbackToQImageCompose(const DecodedFrame& source, const FrameTransform& transform,
                                       uint32_t sequence, uint64_t pts_ns, double receive_ms);

    SwsContext* m_swsCtx = nullptr;
    AVPixelFormat m_lastSrcFmt = AV_PIX_FMT_NONE;
    int m_lastSrcW = 0, m_lastSrcH = 0, m_lastDstW = 0, m_lastDstH = 0;
};

} // namespace phonecam
