#pragma once
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace phonecam {
namespace vcam {

// Shared memory header for frame delivery (double-buffered)
// Two frame slots: writer writes to back buffer, atomically swaps active index.
// Reader reads from the current active buffer.
//
// Memory layout:
//   [SharedFrameHeader]
//   [Frame 0 data: MAX_FRAME_SIZE bytes — holds BGR24 or NV12]
//   [Frame 1 data: MAX_FRAME_SIZE bytes — holds BGR24 or NV12]
constexpr uint32_t SHARED_MAGIC = 0x50434132;  // "PCA2" — V2 header format (includes pixel_format)
constexpr int MAX_WIDTH = 1920;
constexpr int MAX_HEIGHT = 1080;
constexpr int MAX_FRAME_SIZE = MAX_WIDTH * MAX_HEIGHT * 3;  // BGR24 max (NV12 fits within)
constexpr char SHARED_MEMORY_NAME[] = "PhoneCamSharedFrameV2";
constexpr char SHARED_MUTEX_NAME[] = "PhoneCamSharedMutexV2";

// Pixel format of the frame data in shared memory
enum class SharedPixelFormat : int32_t {
    BGR24 = 1,
    NV12 = 2
};

// Shared frame header — POD struct, safe for shared memory
struct SharedFrameHeader {
    uint32_t magic;            // SHARED_MAGIC
    volatile int32_t active_slot; // 0 or 1 (double buffer index, atomic swap)

    struct Slot {
        volatile int32_t width;
        volatile int32_t height;
        volatile int32_t sequence;  // Monotonically increasing counter
        double timestamp;
        volatile int32_t data_size;     // actual bytes in this frame
        volatile int32_t pixel_format;  // SharedPixelFormat (BGR24=1, NV12=2)
        volatile int32_t stride;        // row stride in bytes (for NV12 = width)
    } frame_slots[2];
};

// Producer side (phonecam.exe)
class SharedMemoryWriter {
public:
    SharedMemoryWriter();
    ~SharedMemoryWriter();

    bool open(int width = MAX_WIDTH, int height = MAX_HEIGHT);
    bool write(const uint8_t* bgr_data, int width, int height);  // legacy BGR24 path
    bool writeNv12(const uint8_t* nv12_data, int width, int height);
    bool writeBgr24(const uint8_t* bgr_data, int width, int height);
    void close();
    bool is_open() const { return m_mapping != nullptr; }

private:
#ifdef _WIN32
    void* m_mapping = nullptr;  // HANDLE
    void* m_mutex = nullptr;    // HANDLE
#endif
    SharedFrameHeader* m_header = nullptr;
    uint8_t* m_frame_data = nullptr;
    int64_t m_sequence = 0;
    int m_max_frame_size = 0;
};

// Consumer side (virtualcam DLL)
class SharedMemoryReader {
public:
    SharedMemoryReader();
    ~SharedMemoryReader();

    bool open();
    // Legacy: returns BGR24, unaware of pixel_format
    bool read(uint8_t* out_buffer, int buffer_size, int& width, int& height,
              uint64_t& sequence, int timeout_ms = 100);
    // New: returns pixel format so consumer can take NV12 fast path
    bool read(uint8_t* out_buffer, int buffer_size, int& width, int& height,
              uint64_t& sequence, SharedPixelFormat& format, int timeout_ms = 100);
    bool is_available() const;
    void close();

private:
#ifdef _WIN32
    void* m_mapping = nullptr;  // HANDLE
    void* m_mutex = nullptr;    // HANDLE
#endif
    SharedFrameHeader* m_header = nullptr;
    uint8_t* m_frame_data = nullptr;
    int64_t m_last_sequence = 0;
};

} // namespace vcam
} // namespace phonecam
