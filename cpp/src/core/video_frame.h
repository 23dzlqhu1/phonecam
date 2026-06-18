#pragma once
#include <vector>
#include <cstdint>
#include <QMetaType>

namespace phonecam {

struct VideoFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int codec = 0;          // PcpCodec value
    uint32_t sequence = 0;
    uint64_t pts_us = 0;
    uint64_t pts_ns = 0;    // Camera2 timestamp for latency calc
    bool is_keyframe = false;
    int rotation = 0;       // 0/90/180/270
    double receive_time = 0.0;
};

} // namespace phonecam

// Register for Qt signal/slot across threads
Q_DECLARE_METATYPE(phonecam::VideoFrame)
