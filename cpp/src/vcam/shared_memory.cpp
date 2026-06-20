// shared_memory.cpp — Double-buffered shared memory IPC for virtual camera
#include "shared_memory.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstring>
#include <cstdio>

namespace phonecam {
namespace vcam {

SharedMemoryWriter::SharedMemoryWriter() = default;
SharedMemoryWriter::~SharedMemoryWriter() { close(); }

bool SharedMemoryWriter::open(int width, int height) {
#ifdef _WIN32
    if (m_mapping) return true;
    m_mutex = CreateMutexA(nullptr, FALSE, SHARED_MUTEX_NAME);
    if (!m_mutex) return false;
    // Always use MAX_FRAME_SIZE for consistent offsets with the reader DLL
    // The reader (virtualcam DLL) uses MAX_FRAME_SIZE for its offset calculations
    m_max_frame_size = MAX_FRAME_SIZE;
    size_t total_size = sizeof(SharedFrameHeader) + 2 * MAX_FRAME_SIZE;
    m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(total_size), SHARED_MEMORY_NAME);
    if (!m_mapping) { CloseHandle((HANDLE)m_mutex); m_mutex = nullptr; return false; }
    m_header = (SharedFrameHeader*)MapViewOfFile((HANDLE)m_mapping, FILE_MAP_WRITE, 0, 0, total_size);
    if (!m_header) { CloseHandle((HANDLE)m_mapping); m_mapping = nullptr; CloseHandle((HANDLE)m_mutex); m_mutex = nullptr; return false; }
    m_frame_data = reinterpret_cast<uint8_t*>(m_header + 1);
    m_header->magic = SHARED_MAGIC;
    m_header->active_slot = 0;
    for (int i = 0; i < 2; i++) {
        m_header->frame_slots[i].width = width;
        m_header->frame_slots[i].height = height;
        m_header->frame_slots[i].sequence = 0;
        m_header->frame_slots[i].timestamp = 0;
        m_header->frame_slots[i].data_size = 0;
        m_header->frame_slots[i].pixel_format = (int32_t)SharedPixelFormat::BGR24;
        m_header->frame_slots[i].stride = width * 3;  // BGR24 default stride
    }
    m_sequence = 0;
    return true;
#else
    return false;
#endif
}

// Legacy write — delegates to BGR24 path
bool SharedMemoryWriter::write(const uint8_t* bgr_data, int width, int height) {
    return writeBgr24(bgr_data, width, height);
}

bool SharedMemoryWriter::writeBgr24(const uint8_t* bgr_data, int width, int height) {
#ifdef _WIN32
    if (!m_header) return false;
    int frame_size = width * height * 3;
    if (frame_size > m_max_frame_size) return false;
    LONG current = *(volatile LONG*)&m_header->active_slot;
    int write_slot = 1 - current;
    uint8_t* dst = m_frame_data + write_slot * m_max_frame_size;
    WaitForSingleObject((HANDLE)m_mutex, INFINITE);
    std::memcpy(dst, bgr_data, frame_size);
    m_sequence++;
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].width, width);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].height, height);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].sequence, (LONG)m_sequence);
    m_header->frame_slots[write_slot].timestamp = (double)GetTickCount64() / 1000.0;
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].data_size, frame_size);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].pixel_format, (LONG)SharedPixelFormat::BGR24);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].stride, width * 3);
    InterlockedExchange((LONG*)&m_header->active_slot, write_slot);
    ReleaseMutex((HANDLE)m_mutex);
    return true;
#else
    return false;
#endif
}

bool SharedMemoryWriter::writeNv12(const uint8_t* nv12_data, int width, int height) {
#ifdef _WIN32
    if (!m_header) return false;
    int frame_size = width * height * 3 / 2;  // NV12 = Y + UV (4:2:0)
    if (frame_size > m_max_frame_size) return false;
    LONG current = *(volatile LONG*)&m_header->active_slot;
    int write_slot = 1 - current;
    uint8_t* dst = m_frame_data + write_slot * m_max_frame_size;
    WaitForSingleObject((HANDLE)m_mutex, INFINITE);
    std::memcpy(dst, nv12_data, frame_size);
    m_sequence++;
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].width, width);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].height, height);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].sequence, (LONG)m_sequence);
    m_header->frame_slots[write_slot].timestamp = (double)GetTickCount64() / 1000.0;
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].data_size, frame_size);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].pixel_format, (LONG)SharedPixelFormat::NV12);
    InterlockedExchange((LONG*)&m_header->frame_slots[write_slot].stride, width);  // NV12 Y stride = width
    InterlockedExchange((LONG*)&m_header->active_slot, write_slot);
    ReleaseMutex((HANDLE)m_mutex);
    return true;
#else
    return false;
#endif
}

void SharedMemoryWriter::close() {
#ifdef _WIN32
    if (m_header) { UnmapViewOfFile(m_header); m_header = nullptr; m_frame_data = nullptr; }
    if (m_mapping) { CloseHandle((HANDLE)m_mapping); m_mapping = nullptr; }
    if (m_mutex) { CloseHandle((HANDLE)m_mutex); m_mutex = nullptr; }
#endif
}

SharedMemoryReader::SharedMemoryReader() = default;
SharedMemoryReader::~SharedMemoryReader() { close(); }

bool SharedMemoryReader::open() {
#ifdef _WIN32
    if (m_mapping) return true;

    char dbg[256];

    m_mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, SHARED_MUTEX_NAME);
    if (!m_mutex) {
        snprintf(dbg, sizeof(dbg), "[VCAM] open: OpenMutexA FAILED err=%lu\n", GetLastError());
        OutputDebugStringA(dbg);
        return false;
    }

    m_mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, SHARED_MEMORY_NAME);
    if (!m_mapping) {
        snprintf(dbg, sizeof(dbg), "[VCAM] open: OpenFileMappingA FAILED err=%lu\n", GetLastError());
        OutputDebugStringA(dbg);
        CloseHandle((HANDLE)m_mutex); m_mutex = nullptr;
        return false;
    }

    size_t total_size = sizeof(SharedFrameHeader) + 2 * MAX_FRAME_SIZE;
    m_header = (SharedFrameHeader*)MapViewOfFile((HANDLE)m_mapping, FILE_MAP_READ, 0, 0, total_size);
    if (!m_header) {
        snprintf(dbg, sizeof(dbg), "[VCAM] open: MapViewOfFile FAILED err=%lu size=%zu\n", GetLastError(), total_size);
        OutputDebugStringA(dbg);
        CloseHandle((HANDLE)m_mapping); m_mapping = nullptr;
        CloseHandle((HANDLE)m_mutex); m_mutex = nullptr;
        return false;
    }

    m_frame_data = reinterpret_cast<uint8_t*>(m_header + 1);
    m_last_sequence = 0;

    snprintf(dbg, sizeof(dbg), "[VCAM] open: OK m_header=%p m_frame_data=%p total=%zu magic=0x%X\n",
        m_header, m_frame_data, total_size, (unsigned)m_header->magic);
    OutputDebugStringA(dbg);
    return true;
#else
    return false;
#endif
}

// Legacy read — returns frame data regardless of format, treats as BGR24
bool SharedMemoryReader::read(uint8_t* out_buffer, int buffer_size, int& width, int& height, uint64_t& sequence, int timeout_ms) {
    SharedPixelFormat fmt;
    return read(out_buffer, buffer_size, width, height, sequence, fmt, timeout_ms);
}

// New read with format output — allows consumer to take NV12 fast path
bool SharedMemoryReader::read(uint8_t* out_buffer, int buffer_size, int& width, int& height,
                               uint64_t& sequence, SharedPixelFormat& format, int timeout_ms) {
#ifdef _WIN32
    if (!m_header) return false;

    // ALL shared memory access must be inside SEH — the writer may have
    // recreated the mapping at any time, invalidating our view.
    __try {
        // Use volatile reads — NOT InterlockedCompareExchange!
        // cmpxchg needs write access but we mapped FILE_MAP_READ.
        LONG active = *(volatile LONG*)&m_header->active_slot;
        if (active < 0 || active > 1) return false;

        LONG seq = *(volatile LONG*)&m_header->frame_slots[active].sequence;
        if (seq == (LONG)m_last_sequence) {
            Sleep(10);
            active = *(volatile LONG*)&m_header->active_slot;
            if (active < 0 || active > 1) return false;
            seq = *(volatile LONG*)&m_header->frame_slots[active].sequence;
            if (seq == (LONG)m_last_sequence) return false;
        }
        width  = (int)*(volatile LONG*)&m_header->frame_slots[active].width;
        height = (int)*(volatile LONG*)&m_header->frame_slots[active].height;
        int data_size = (int)*(volatile LONG*)&m_header->frame_slots[active].data_size;
        format = (SharedPixelFormat)*(volatile LONG*)&m_header->frame_slots[active].pixel_format;
        if (data_size <= 0 || data_size > buffer_size) return false;
        const uint8_t* src = m_frame_data + active * MAX_FRAME_SIZE;
        std::memcpy(out_buffer, src, data_size);
        sequence = (uint64_t)seq;
        m_last_sequence = seq;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Shared memory mapping is stale — close and attempt reopen
        OutputDebugStringA("[VCAM] read: ACCESS_VIOLATION, reopening shared memory\n");
        close();
        if (!open()) return false;
        // Retry once after reopen
        __try {
            LONG active = *(volatile LONG*)&m_header->active_slot;
            if (active < 0 || active > 1) return false;
            LONG seq = *(volatile LONG*)&m_header->frame_slots[active].sequence;
            if (seq == (LONG)m_last_sequence) return false;
            width  = (int)*(volatile LONG*)&m_header->frame_slots[active].width;
            height = (int)*(volatile LONG*)&m_header->frame_slots[active].height;
            int data_size = (int)*(volatile LONG*)&m_header->frame_slots[active].data_size;
            format = (SharedPixelFormat)*(volatile LONG*)&m_header->frame_slots[active].pixel_format;
            if (data_size <= 0 || data_size > buffer_size) return false;
            const uint8_t* src = m_frame_data + active * MAX_FRAME_SIZE;
            std::memcpy(out_buffer, src, data_size);
            sequence = (uint64_t)seq;
            m_last_sequence = seq;
            OutputDebugStringA("[VCAM] read: retry after reopen SUCCESS\n");
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            OutputDebugStringA("[VCAM] read: retry also FAILED\n");
            return false;
        }
    }
#else
    return false;
#endif
}

bool SharedMemoryReader::is_available() const {
#ifdef _WIN32
    HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, SHARED_MEMORY_NAME);
    if (h) { CloseHandle(h); return true; }
#endif
    return false;
}

void SharedMemoryReader::close() {
#ifdef _WIN32
    if (m_header) { UnmapViewOfFile(m_header); m_header = nullptr; m_frame_data = nullptr; }
    if (m_mapping) { CloseHandle((HANDLE)m_mapping); m_mapping = nullptr; }
    if (m_mutex) { CloseHandle((HANDLE)m_mutex); m_mutex = nullptr; }
#endif
}

} // namespace vcam
} // namespace phonecam
