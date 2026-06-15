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
//   [Frame 0 data: max_width * max_height * 3 bytes (BGR24)]
//   [Frame 1 data: max_width * max_height * 3 bytes (BGR24)]
constexpr uint32_t SHARED_MAGIC = 0x5043414D;  // "PCAM"
constexpr int MAX_WIDTH = 1920;
constexpr int MAX_HEIGHT = 1080;
constexpr int MAX_FRAME_SIZE = MAX_WIDTH * MAX_HEIGHT * 3;  // BGR24
constexpr char SHARED_MEMORY_NAME[] = "PhoneCamSharedFrame";
constexpr char SHARED_MUTEX_NAME[] = "PhoneCamSharedMutex";

// Shared frame header — POD struct, safe for shared memory
struct SharedFrameHeader {
    uint32_t magic;            // SHARED_MAGIC
    volatile int32_t active_slot; // 0 or 1 (double buffer index, atomic swap)

    struct Slot {
        volatile int32_t width;
        volatile int32_t height;
        volatile int32_t sequence;  // Monotonically increasing counter
        double timestamp;
        volatile int32_t data_size;  // actual bytes in this frame
    } frame_slots[2];
};

// Producer side (phonecam.exe)
class SharedMemoryWriter {
public:
    SharedMemoryWriter();
    ~SharedMemoryWriter();

    bool open(int width = MAX_WIDTH, int height = MAX_HEIGHT);
    bool write(const uint8_t* bgr_data, int width, int height);
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
    bool read(uint8_t* out_buffer, int buffer_size, int& width, int& height,
              uint64_t& sequence, int timeout_ms = 100);
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
