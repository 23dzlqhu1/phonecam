#pragma once
#include <QByteArray>
#include <QMetaType>
#include <cstdint>

namespace phonecam {

// Unified NV12 frame — single source of truth for both preview and virtual camera.
// FinalFrameComposer produces one of these at fixed 1280×720 after applying all
// transforms (mirror, flip, rotation, scale, letterbox).
struct Nv12Frame {
    QByteArray data;      // NV12 payload: width * height * 3 / 2 bytes
    int width = 1280;
    int height = 720;
    uint32_t sequence = 0;
    uint64_t pts_ns = 0;
    double receive_ms = 0.0;  // PC receive timestamp (ms) for latency tracking
};

} // namespace phonecam

Q_DECLARE_METATYPE(phonecam::Nv12Frame)
