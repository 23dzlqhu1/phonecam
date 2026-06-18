#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace phonecam {

// RAII wrapper around AVFrame with reference-counted ownership.
// Movable but not copyable — the frame payload is owned by exactly one instance.
struct DecodedFrame {
    AVFrame* frame = nullptr;

    DecodedFrame() = default;

    // Takes ownership of an existing AVFrame (no ref, no clone — moves the pointer)
    explicit DecodedFrame(AVFrame* f) : frame(f) {}

    ~DecodedFrame() { release(); }

    // Move constructor
    DecodedFrame(DecodedFrame&& other) noexcept : frame(other.frame) {
        other.frame = nullptr;
    }

    // Move assignment
    DecodedFrame& operator=(DecodedFrame&& other) noexcept {
        if (this != &other) {
            release();
            frame = other.frame;
            other.frame = nullptr;
        }
        return *this;
    }

    // No copy
    DecodedFrame(const DecodedFrame&) = delete;
    DecodedFrame& operator=(const DecodedFrame&) = delete;

    bool valid() const { return frame != nullptr && frame->data[0] != nullptr; }

    int width() const { return frame ? frame->width : 0; }
    int height() const { return frame ? frame->height : 0; }
    AVPixelFormat format() const { return frame ? static_cast<AVPixelFormat>(frame->format) : AV_PIX_FMT_NONE; }

    // Create a ref-counted clone (av_frame_ref under the hood)
    static DecodedFrame clone(const AVFrame* src) {
        AVFrame* f = av_frame_alloc();
        if (!f) return {};
        if (av_frame_ref(f, src) < 0) {
            av_frame_free(&f);
            return {};
        }
        return DecodedFrame(f);
    }

    void release() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

} // namespace phonecam
