#include "core/final_frame_composer.h"
#include <QPainter>
#include <QTransform>
#include <QDebug>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
}

namespace phonecam {

// ═══════════════════════════════════════════════════════════════
// Legacy QImage path
// ═══════════════════════════════════════════════════════════════

QImage FinalFrameComposer::applyTransforms(const QImage& source, const FrameTransform& transform) {
    QImage result = source;

    if (transform.androidRotation == 90) {
        result = result.transformed(QTransform().rotate(90));
    } else if (transform.androidRotation == 180) {
        result = result.transformed(QTransform().rotate(180));
    } else if (transform.androidRotation == 270) {
        result = result.transformed(QTransform().rotate(-90));
    }

    if (transform.mirror) {
        result = result.mirrored(true, false);
    }
    if (transform.flip) {
        result = result.mirrored(false, true);
    }
    if (transform.manualRotation != 0) {
        QTransform t;
        t.rotate(static_cast<qreal>(transform.manualRotation));
        result = result.transformed(t);
    }

    return result;
}

static QByteArray rgb888ToNv12(const QImage& rgb, int w, int h) {
    const int ySize = w * h;
    const int uvSize = ySize / 2;
    QByteArray nv12(ySize + uvSize, 0);

    uint8_t* yPlane = reinterpret_cast<uint8_t*>(nv12.data());
    uint8_t* uvPlane = yPlane + ySize;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const uint8_t* p = rgb.constBits() + (row * w + col) * 3;
            const int r = p[0], g = p[1], b = p[2];

            int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yPlane[row * w + col] = static_cast<uint8_t>(y < 16 ? 16 : (y > 235 ? 235 : y));

            if ((row & 1) == 0 && (col & 1) == 0) {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                const int uvIdx = (row / 2) * w + col;
                uvPlane[uvIdx]     = static_cast<uint8_t>(u < 16 ? 16 : (u > 240 ? 240 : u));
                uvPlane[uvIdx + 1] = static_cast<uint8_t>(v < 16 ? 16 : (v > 240 ? 240 : v));
            }
        }
    }

    return nv12;
}

Nv12Frame FinalFrameComposer::compose(const QImage& source, const FrameTransform& transform,
                                       uint32_t sequence, uint64_t pts_ns,
                                       double receive_ms) {
    Nv12Frame result;
    result.width = kOutputWidth;
    result.height = kOutputHeight;
    result.sequence = sequence;
    result.pts_ns = pts_ns;
    result.receive_ms = receive_ms;

    const int ySize = kOutputWidth * kOutputHeight;
    const int uvSize = ySize / 2;

    if (source.isNull()) {
        QByteArray black(ySize + uvSize, 0);
        std::memset(black.data(), 16, ySize);
        std::memset(black.data() + ySize, 128, uvSize);
        result.data = black;
        return result;
    }

    QImage transformed = applyTransforms(source, transform);

    const int srcW = transformed.width();
    const int srcH = transformed.height();
    const double scale = qMin(static_cast<double>(kOutputWidth) / srcW,
                              static_cast<double>(kOutputHeight) / srcH);
    int dstW = static_cast<int>(srcW * scale) & ~1;
    int dstH = static_cast<int>(srcH * scale) & ~1;

    const int offsetX = ((kOutputWidth - dstW) / 2) & ~1;
    const int offsetY = ((kOutputHeight - dstH) / 2) & ~1;

    QImage canvas(kOutputWidth, kOutputHeight, QImage::Format_RGB888);
    canvas.fill(Qt::black);

    QImage scaled = transformed.scaled(dstW, dstH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    {
        QPainter painter(&canvas);
        painter.drawImage(offsetX, offsetY, scaled);
    }

    result.data = rgb888ToNv12(canvas, kOutputWidth, kOutputHeight);
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Fast YUV path (sws_scale)
// ═══════════════════════════════════════════════════════════════

FinalFrameComposer::FinalFrameComposer() = default;

FinalFrameComposer::~FinalFrameComposer() {
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
}

bool FinalFrameComposer::initSwsContext(AVPixelFormat srcFmt, int srcW, int srcH, int dstW, int dstH) {
    if (m_swsCtx && m_lastSrcFmt == srcFmt && m_lastSrcW == srcW && m_lastSrcH == srcH
        && m_lastDstW == dstW && m_lastDstH == dstH) {
        return true;
    }

    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    m_swsCtx = sws_getContext(srcW, srcH, srcFmt,
                               dstW, dstH, AV_PIX_FMT_NV12,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        qWarning() << "[COMPOSER] sws_getContext failed: src=" << av_get_pix_fmt_name(srcFmt)
                   << srcW << "x" << srcH << "→ NV12" << dstW << "x" << dstH;
        return false;
    }

    m_lastSrcFmt = srcFmt;
    m_lastSrcW = srcW;
    m_lastSrcH = srcH;
    m_lastDstW = dstW;
    m_lastDstH = dstH;

    qDebug() << "[COMPOSER] sws init:" << av_get_pix_fmt_name(srcFmt)
             << srcW << "x" << srcH << "→ NV12" << dstW << "x" << dstH;
    return true;
}

void FinalFrameComposer::fillNv12Black(uint8_t* data, int w, int h) {
    const int ySize = w * h;
    const int uvSize = ySize / 2;
    std::memset(data, 16, ySize);            // Y=16 (black)
    std::memset(data + ySize, 128, uvSize);   // UV=128 (neutral chroma)
}

void FinalFrameComposer::copyNv12Region(uint8_t* dst, int dstW, int dstH,
                                         const uint8_t* srcY, int srcYStride,
                                         const uint8_t* srcUV, int srcUVStride,
                                         int srcW, int srcH,
                                         int offsetX, int offsetY) {
    uint8_t* dstY = dst;
    for (int row = 0; row < srcH; row++) {
        std::memcpy(dstY + (offsetY + row) * dstW + offsetX,
                    srcY + row * srcYStride,
                    srcW);
    }

    uint8_t* dstUV = dst + (dstW * dstH);
    const int uvOffY = offsetY / 2;
    for (int row = 0; row < srcH / 2; row++) {
        std::memcpy(dstUV + (uvOffY + row) * dstW + offsetX,
                    srcUV + row * srcUVStride,
                    srcW);  // srcW bytes = srcW/2 interleaved U,V pairs
    }
}

// ── NV12 rotation (BT.601 pixel data, plane-aware) ──
// Handles 90° CW, 180°, 270° CW (= 90° CCW). Y and UV planes are rotated
// independently; UV interleaved pairs are preserved as atomic 2-byte units.

QByteArray FinalFrameComposer::rotateNv12(const uint8_t* srcY, int srcYStride,
                                           const uint8_t* srcUV, int srcUVStride,
                                           int srcW, int srcH, int rotation) {
    const int ySize = srcW * srcH;
    const int uvSize = ySize / 2;

    if (rotation == 0) {
        // No rotation — just copy
        QByteArray out(ySize + uvSize, 0);
        uint8_t* dstY = reinterpret_cast<uint8_t*>(out.data());
        uint8_t* dstUV = dstY + ySize;
        for (int row = 0; row < srcH; row++)
            std::memcpy(dstY + row * srcW, srcY + row * srcYStride, srcW);
        for (int row = 0; row < srcH / 2; row++)
            std::memcpy(dstUV + row * srcW, srcUV + row * srcUVStride, srcW);
        return out;
    }

    if (rotation == 180) {
        // 180°: reverse both dimensions, same output size
        QByteArray out(ySize + uvSize, 0);
        uint8_t* dstY = reinterpret_cast<uint8_t*>(out.data());
        uint8_t* dstUV = dstY + ySize;
        for (int row = 0; row < srcH; row++) {
            const int dstRow = srcH - 1 - row;
            for (int col = 0; col < srcW; col++) {
                dstY[dstRow * srcW + (srcW - 1 - col)] = srcY[row * srcYStride + col];
            }
        }
        for (int row = 0; row < srcH / 2; row++) {
            const int dstRow = srcH / 2 - 1 - row;
            for (int col = 0; col < srcW; col += 2) {
                const int dstCol = srcW - 2 - col;
                dstUV[dstRow * srcW + dstCol]     = srcUV[row * srcUVStride + col];
                dstUV[dstRow * srcW + dstCol + 1] = srcUV[row * srcUVStride + col + 1];
            }
        }
        return out;
    }

    // 90° CW or 270° CW — output dimensions are swapped
    const int dstW = srcH;
    const int dstH = srcW;
    const int dstYSize = dstW * dstH;
    QByteArray out(dstYSize + dstYSize / 2, 0);
    uint8_t* dstY = reinterpret_cast<uint8_t*>(out.data());
    uint8_t* dstUV = dstY + dstYSize;

    if (rotation == 90) {
        // 90° CW: (sx, sy) → (srcH-1-sy, sx)
        for (int sy = 0; sy < srcH; sy++) {
            for (int sx = 0; sx < srcW; sx++) {
                const int dx = srcH - 1 - sy;
                const int dy = sx;
                dstY[dy * dstW + dx] = srcY[sy * srcYStride + sx];
            }
        }
        // UV: each 2×2 source block maps to 2×2 dest block
        // Source UV at (sux, suy) covers Y at (sux*2, suy*2)..(sux*2+1, suy*2+1)
        // After 90° CW, dest UV position = (srcH/2-1-suy, sux)
        for (int suy = 0; suy < srcH / 2; suy++) {
            for (int sux = 0; sux < srcW / 2; sux++) {
                const int dux = srcH / 2 - 1 - suy;
                const int duy = sux;
                const int srcIdx = suy * srcUVStride + sux * 2;
                const int dstIdx = duy * dstW + dux * 2;
                dstUV[dstIdx]     = srcUV[srcIdx];
                dstUV[dstIdx + 1] = srcUV[srcIdx + 1];
            }
        }
        return out;
    }

    // rotation == 270 (90° CCW): (sx, sy) → (sy, srcW-1-sx)
    for (int sy = 0; sy < srcH; sy++) {
        for (int sx = 0; sx < srcW; sx++) {
            const int dx = sy;
            const int dy = srcW - 1 - sx;
            dstY[dy * dstW + dx] = srcY[sy * srcYStride + sx];
        }
    }
    for (int suy = 0; suy < srcH / 2; suy++) {
        for (int sux = 0; sux < srcW / 2; sux++) {
            const int dux = suy;
            const int duy = srcW / 2 - 1 - sux;
            const int srcIdx = suy * srcUVStride + sux * 2;
            const int dstIdx = duy * dstW + dux * 2;
            dstUV[dstIdx]     = srcUV[srcIdx];
            dstUV[dstIdx + 1] = srcUV[srcIdx + 1];
        }
    }
    return out;
}

Nv12Frame FinalFrameComposer::composeFromDecodedFrame(const DecodedFrame& source,
                                                       const FrameTransform& transform,
                                                       uint32_t sequence, uint64_t pts_ns,
                                                       double receive_ms) {
    Nv12Frame result;
    result.width = kOutputWidth;
    result.height = kOutputHeight;
    result.sequence = sequence;
    result.pts_ns = pts_ns;
    result.receive_ms = receive_ms;

    const int ySize = kOutputWidth * kOutputHeight;
    const int uvSize = ySize / 2;

    // Invalid source → black
    if (!source.valid()) {
        QByteArray black(ySize + uvSize, 0);
        fillNv12Black(reinterpret_cast<uint8_t*>(black.data()), kOutputWidth, kOutputHeight);
        result.data = black;
        return result;
    }

    // ── BUG-013: All transforms handled natively in NV12 fast path ──
    // No more needsFallback() — mirror/flip/manualRotation operate on NV12 planes directly.
    // Order: pix_fmt→NV12 → androidRotation → mirror → flip → manualRotation → scale/letterbox

    const AVFrame* srcFrame = source.frame;
    int srcW = srcFrame->width;
    int srcH = srcFrame->height;
    const AVPixelFormat srcFmt = source.format();

    // ── BUG-013 performance: format-aware NV12 fast path ──
    // For NV12: use frame data directly (no conversion needed)
    // For yuv420p/yuvj420p/p010 etc.: convert to NV12 via sws_scale, then use fast path
    // This eliminates the slow RGB24/QImage round-trip for non-NV12 formats.
    static int composeCount = 0;
    composeCount++;
    const bool isNv12 = (srcFmt == AV_PIX_FMT_NV12);

    if (composeCount <= 10 || composeCount % 300 == 0) {
        qDebug() << "[COMPOSER] composeFromDecodedFrame#" << composeCount
                 << "pix_fmt=" << av_get_pix_fmt_name(srcFmt)
                 << "isNv12=" << isNv12
                 << "rotation=" << transform.androidRotation
                 << "size=" << srcW << "x" << srcH;
    }

    // Determine effective source planes (either original NV12 or converted NV12)
    QByteArray nv12ConvertBuf;  // holds converted NV12 data if srcFmt != NV12
    const uint8_t* srcY  = srcFrame->data[0];
    int            srcYStride = srcFrame->linesize[0];
    const uint8_t* srcUV = isNv12 ? srcFrame->data[1] : nullptr;
    int            srcUVStride = isNv12 ? srcFrame->linesize[1] : 0;

    if (!isNv12) {
        // ── Convert any pix_fmt → NV12 via sws_scale (direct, no RGB round-trip) ──
        const int nv12Size = srcW * srcH * 3 / 2;
        nv12ConvertBuf.resize(nv12Size);

        SwsContext* toNv12 = sws_getContext(
            srcW, srcH, srcFmt,
            srcW, srcH, AV_PIX_FMT_NV12,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!toNv12) {
            qWarning() << "[COMPOSER] sws_getContext for NV12 conversion failed:"
                       << av_get_pix_fmt_name(srcFmt);
            // Ultimate fallback: QImage path
            return fallbackToQImageCompose(source, transform, sequence, pts_ns, receive_ms);
        }

        uint8_t* nv12Data = reinterpret_cast<uint8_t*>(nv12ConvertBuf.data());
        uint8_t* dstPlanes[4] = { nv12Data, nv12Data + srcW * srcH, nullptr, nullptr };
        int dstStrides[4] = { srcW, srcW, 0, 0 };
        sws_scale(toNv12, srcFrame->data, srcFrame->linesize, 0, srcH, dstPlanes, dstStrides);
        sws_freeContext(toNv12);

        srcY  = nv12Data;
        srcYStride = srcW;
        srcUV = nv12Data + srcW * srcH;
        srcUVStride = srcW;

        if (composeCount <= 10) {
            qDebug() << "[COMPOSER] converted" << av_get_pix_fmt_name(srcFmt)
                     << "→ NV12" << srcW << "x" << srcH;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Fast NV12 path (existing code, only reached for NV12 + valid rotation)
    // ═══════════════════════════════════════════════════════════════

    // ── Handle androidRotation: rotate NV12 in-place, then scale ──
    // For 90/270, dimensions swap after rotation.
    QByteArray rotatedBuf;
    const uint8_t* effectiveY  = srcY;
    int            effectiveYStride = srcYStride;
    const uint8_t* effectiveUV = srcUV;
    int            effectiveUVStride = srcUVStride;
    int effectiveW = srcW;
    int effectiveH = srcH;

    if (transform.androidRotation != 0) {
        rotatedBuf = rotateNv12(srcY, srcYStride,
                                 srcUV, srcUVStride,
                                 srcW, srcH, transform.androidRotation);
        if (rotatedBuf.isEmpty()) {
            result.data = QByteArray(ySize + uvSize, 0);
            fillNv12Black(reinterpret_cast<uint8_t*>(result.data.data()), kOutputWidth, kOutputHeight);
            return result;
        }
        effectiveY  = reinterpret_cast<const uint8_t*>(rotatedBuf.constData());
        effectiveYStride = (transform.androidRotation == 90 || transform.androidRotation == 270) ? srcH : srcW;
        effectiveUV = effectiveY + effectiveYStride * ((transform.androidRotation == 90 || transform.androidRotation == 270) ? srcW : srcH);
        effectiveUVStride = effectiveYStride;
        effectiveW = (transform.androidRotation == 90 || transform.androidRotation == 270) ? srcH : srcW;
        effectiveH = (transform.androidRotation == 90 || transform.androidRotation == 270) ? srcW : srcH;

        qDebug() << "[COMPOSER] NV12 rotated" << transform.androidRotation << "deg:"
                 << srcW << "x" << srcH << "→" << effectiveW << "x" << effectiveH
                 << "(fast YUV path)";
    }

    // ── Apply mirror (horizontal flip) on NV12 planes ──
    QByteArray mirroredBuf;
    if (transform.mirror) {
        mirroredBuf = mirrorNv12(effectiveY, effectiveYStride,
                                  effectiveUV, effectiveUVStride,
                                  effectiveW, effectiveH);
        effectiveY  = reinterpret_cast<const uint8_t*>(mirroredBuf.constData());
        effectiveYStride = effectiveW;
        effectiveUV = effectiveY + effectiveW * effectiveH;
        effectiveUVStride = effectiveW;
        if (composeCount <= 10) {
            qDebug() << "[COMPOSER] NV12 mirrored" << effectiveW << "x" << effectiveH;
        }
    }

    // ── Apply flip (vertical flip) on NV12 planes ──
    QByteArray flippedBuf;
    if (transform.flip) {
        flippedBuf = flipNv12(effectiveY, effectiveYStride,
                               effectiveUV, effectiveUVStride,
                               effectiveW, effectiveH);
        effectiveY  = reinterpret_cast<const uint8_t*>(flippedBuf.constData());
        effectiveYStride = effectiveW;
        effectiveUV = effectiveY + effectiveW * effectiveH;
        effectiveUVStride = effectiveW;
        if (composeCount <= 10) {
            qDebug() << "[COMPOSER] NV12 flipped" << effectiveW << "x" << effectiveH;
        }
    }

    // ── Apply manualRotation on NV12 planes ──
    QByteArray manualRotBuf;
    if (transform.manualRotation != 0) {
        manualRotBuf = rotateNv12(effectiveY, effectiveYStride,
                                   effectiveUV, effectiveUVStride,
                                   effectiveW, effectiveH, transform.manualRotation);
        if (!manualRotBuf.isEmpty()) {
            const bool swapped = (transform.manualRotation == 90 || transform.manualRotation == 270);
            effectiveY  = reinterpret_cast<const uint8_t*>(manualRotBuf.constData());
            effectiveYStride = swapped ? effectiveH : effectiveW;
            effectiveUV = effectiveY + effectiveYStride * (swapped ? effectiveW : effectiveH);
            effectiveUVStride = effectiveYStride;
            if (swapped) { int tmp = effectiveW; effectiveW = effectiveH; effectiveH = tmp; }
            if (composeCount <= 10) {
                qDebug() << "[COMPOSER] NV12 manual rotated" << transform.manualRotation
                         << "deg:" << effectiveW << "x" << effectiveH;
            }
        }
    }

    // Calculate scaled dimensions using effective (post-all-transforms) dimensions
    const double scale = qMin(static_cast<double>(kOutputWidth) / effectiveW,
                              static_cast<double>(kOutputHeight) / effectiveH);
    int scaledW = (static_cast<int>(effectiveW * scale)) & ~1;
    int scaledH = (static_cast<int>(effectiveH * scale)) & ~1;

    // Init sws context for effective source → scaled NV12
    if (!initSwsContext(AV_PIX_FMT_NV12, effectiveW, effectiveH, scaledW, scaledH)) {
        qWarning() << "[COMPOSER] sws init failed — returning black";
        QByteArray black(ySize + uvSize, 0);
        fillNv12Black(reinterpret_cast<uint8_t*>(black.data()), kOutputWidth, kOutputHeight);
        result.data = black;
        return result;
    }

    // Allocate temp frame for scaled NV12
    AVFrame* scaledFrame = av_frame_alloc();
    if (!scaledFrame) {
        result.data = QByteArray(ySize + uvSize, 0);
        fillNv12Black(reinterpret_cast<uint8_t*>(result.data.data()), kOutputWidth, kOutputHeight);
        return result;
    }
    scaledFrame->format = AV_PIX_FMT_NV12;
    scaledFrame->width = scaledW;
    scaledFrame->height = scaledH;
    if (av_frame_get_buffer(scaledFrame, 0) < 0) {
        av_frame_free(&scaledFrame);
        result.data = QByteArray(ySize + uvSize, 0);
        fillNv12Black(reinterpret_cast<uint8_t*>(result.data.data()), kOutputWidth, kOutputHeight);
        return result;
    }

    // sws_scale: effective source → scaled NV12
    // When rotated, effectiveY/effectiveUV point into rotatedBuf (flat NV12 layout)
    const uint8_t* srcPlanes[4] = { effectiveY, effectiveUV, nullptr, nullptr };
    int srcStrides[4] = { effectiveYStride, effectiveUVStride, 0, 0 };
    sws_scale(m_swsCtx, srcPlanes, srcStrides, 0, effectiveH,
              scaledFrame->data, scaledFrame->linesize);

    // Log scaled frame linesizes (diagnose padding issues)
    static int linesizeLogCount = 0;
    if (++linesizeLogCount <= 2) {
        qDebug() << "[COMPOSER] scaled NV12 linesize y=" << scaledFrame->linesize[0]
                 << "uv=" << scaledFrame->linesize[1]
                 << "size=" << scaledW << "x" << scaledH;
    }

    // Allocate output buffer + fill with black
    QByteArray outBuf(ySize + uvSize, 0);
    fillNv12Black(reinterpret_cast<uint8_t*>(outBuf.data()), kOutputWidth, kOutputHeight);

    // Calculate offset for centered placement
    const int offsetX = ((kOutputWidth - scaledW) / 2) & ~1;
    const int offsetY = ((kOutputHeight - scaledH) / 2) & ~1;

    // Copy scaled NV12 into the black canvas at centered offset
    copyNv12Region(reinterpret_cast<uint8_t*>(outBuf.data()), kOutputWidth, kOutputHeight,
                   scaledFrame->data[0], scaledFrame->linesize[0],
                   scaledFrame->data[1], scaledFrame->linesize[1],
                   scaledW, scaledH,
                   offsetX, offsetY);

    av_frame_free(&scaledFrame);
    result.data = outBuf;
    return result;
}

// ── NV12 mirror (horizontal flip) ──
// Y: reverse each row byte-by-byte.
// UV: reverse each row in 2-byte pairs (U,V must stay paired).
QByteArray FinalFrameComposer::mirrorNv12(const uint8_t* srcY, int srcYStride,
                                            const uint8_t* srcUV, int srcUVStride,
                                            int w, int h) {
    const int ySize = w * h;
    const int uvSize = ySize / 2;
    QByteArray out(ySize + uvSize, 0);
    uint8_t* dstY = reinterpret_cast<uint8_t*>(out.data());
    uint8_t* dstUV = dstY + ySize;

    // Y plane: reverse each row
    for (int row = 0; row < h; row++) {
        const uint8_t* srcRow = srcY + row * srcYStride;
        uint8_t* dstRow = dstY + row * w;
        for (int col = 0; col < w; col++) {
            dstRow[col] = srcRow[w - 1 - col];
        }
    }

    // UV plane: reverse each row in 2-byte pairs
    for (int row = 0; row < h / 2; row++) {
        const uint8_t* srcRow = srcUV + row * srcUVStride;
        uint8_t* dstRow = dstUV + row * w;
        for (int col = 0; col < w; col += 2) {
            const int dstCol = w - 2 - col;
            dstRow[dstCol]     = srcRow[col];      // U
            dstRow[dstCol + 1] = srcRow[col + 1];  // V
        }
    }

    return out;
}

// ── NV12 flip (vertical flip) ──
// Y: reverse row order.
// UV: reverse chroma row order (each row is w bytes of interleaved U,V).
QByteArray FinalFrameComposer::flipNv12(const uint8_t* srcY, int srcYStride,
                                          const uint8_t* srcUV, int srcUVStride,
                                          int w, int h) {
    const int ySize = w * h;
    const int uvSize = ySize / 2;
    QByteArray out(ySize + uvSize, 0);
    uint8_t* dstY = reinterpret_cast<uint8_t*>(out.data());
    uint8_t* dstUV = dstY + ySize;

    // Y plane: reverse row order
    for (int row = 0; row < h; row++) {
        std::memcpy(dstY + row * w, srcY + (h - 1 - row) * srcYStride, w);
    }

    // UV plane: reverse chroma row order
    for (int row = 0; row < h / 2; row++) {
        std::memcpy(dstUV + row * w, srcUV + (h / 2 - 1 - row) * srcUVStride, w);
    }

    return out;
}

Nv12Frame FinalFrameComposer::fallbackToQImageCompose(const DecodedFrame& source,
                                                       const FrameTransform& transform,
                                                       uint32_t sequence, uint64_t pts_ns,
                                                       double receive_ms) {
    const AVFrame* srcFrame = source.frame;
    const int srcW = srcFrame->width;
    const int srcH = srcFrame->height;
    const AVPixelFormat srcFmt = source.format();

    // Create sws context: srcFmt → RGB24
    SwsContext* swsCtx = sws_getContext(
        srcW, srcH, srcFmt,
        srcW, srcH, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        qWarning() << "[COMPOSER] fallback: sws_getContext failed for" << av_get_pix_fmt_name(srcFmt);
        Nv12Frame result;
        result.width = kOutputWidth;
        result.height = kOutputHeight;
        result.sequence = sequence;
        result.pts_ns = pts_ns;
        result.receive_ms = receive_ms;
        const int ySize = kOutputWidth * kOutputHeight;
        QByteArray black(ySize + ySize / 2, 0);
        fillNv12Black(reinterpret_cast<uint8_t*>(black.data()), kOutputWidth, kOutputHeight);
        result.data = black;
        return result;
    }

    // Convert to RGB24 QImage
    QImage rgbImg(srcW, srcH, QImage::Format_RGB888);
    uint8_t* dstData[4] = { rgbImg.bits(), nullptr, nullptr, nullptr };
    int dstStride[4] = { static_cast<int>(rgbImg.bytesPerLine()), 0, 0, 0 };
    // BUG-013: pass frame->data and frame->linesize directly — sws_scale handles
    // any pix_fmt (yuv420p, yuvj420p, nv12, p010, etc.) correctly with proper planes.
    sws_scale(swsCtx, srcFrame->data, srcFrame->linesize, 0, srcH, dstData, dstStride);
    sws_freeContext(swsCtx);

    // Delegate to legacy compose() which handles transforms + NV12 conversion
    return compose(rgbImg, transform, sequence, pts_ns, receive_ms);
}

} // namespace phonecam
