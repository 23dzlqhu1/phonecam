// test_shm_read.cpp - Read PhoneCam shared memory
#include <windows.h>
#include <stdio.h>

#define SHARED_MAGIC 0x5043414D

struct Slot {
    volatile int width;
    volatile int height;
    volatile int sequence;
    double timestamp;
    volatile int data_size;
};

struct Header {
    unsigned int magic;
    volatile int active_slot;
    Slot slots[2];
};

int main() {
    // Open mutex
    HANDLE mutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "PhoneCamSharedMutex");
    if (!mutex) {
        printf("ERROR: Cannot open mutex (error %d)\n", GetLastError());
        printf("Phonecam.exe may not be writing to shared memory\n");
    } else {
        printf("OK: Mutex opened\n");
    }

    // Open shared memory
    HANDLE mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, "PhoneCamSharedFrame");
    if (!mapping) {
        printf("ERROR: Cannot open shared memory (error %d)\n", GetLastError());
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    printf("OK: Shared memory opened\n");

    // Map view
    size_t total = sizeof(Header) + 2 * 1920 * 1080 * 3;
    Header* header = (Header*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, total);
    if (!header) {
        printf("ERROR: Cannot map view (error %d)\n", GetLastError());
        CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    printf("OK: View mapped\n");

    // Read header
    printf("Magic: 0x%08X (expect 0x%08X)\n", header->magic, SHARED_MAGIC);
    printf("Active slot: %d\n", header->active_slot);

    for (int i = 0; i < 2; i++) {
        printf("Slot %d: %dx%d seq=%d data_size=%d\n",
            i, header->slots[i].width, header->slots[i].height,
            header->slots[i].sequence, header->slots[i].data_size);
    }

    // Check if data is valid
    int active = header->active_slot;
    int ds = header->slots[active].data_size;
    int expected = header->slots[active].width * header->slots[active].height * 3;
    if (ds == expected) {
        printf("OK: Data size matches (%d bytes)\n", ds);
    } else {
        printf("WARNING: Data size mismatch (got %d, expected %d)\n", ds, expected);
    }

    UnmapViewOfFile(header);
    CloseHandle(mapping);
    if (mutex) CloseHandle(mutex);
    return 0;
}
