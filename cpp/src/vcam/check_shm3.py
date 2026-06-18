import ctypes, struct, sys

kernel32 = ctypes.windll.kernel32
handle = kernel32.OpenFileMappingW(0x0004, False, "PhoneCamSharedFrame")
result = []
if not handle:
    result.append(f"OpenFileMapping FAILED (error {ctypes.get_last_error()})")
else:
    result.append(f"OpenFileMapping OK (handle={handle})")
    ptr = kernel32.MapViewOfFile(handle, 0x0004, 0, 0, 100)
    if not ptr:
        result.append(f"MapViewOfFile FAILED (error {ctypes.get_last_error()})")
    else:
        raw = (ctypes.c_char * 100).from_address(ptr)
        data = bytes(raw)
        magic = struct.unpack_from('<I', data, 0)[0]
        active = struct.unpack_from('<i', data, 4)[0]
        result.append(f"Magic: 0x{magic:08X} (expect 0x5043414D)")
        result.append(f"Active slot: {active}")
        for i in range(2):
            off = 8 + i * 24
            w, h, seq = struct.unpack_from('<iii', data, off)
            ds = struct.unpack_from('<i', data, off + 20)[0]
            result.append(f"Slot {i}: {w}x{h}, seq={seq}, data_size={ds}")
        kernel32.UnmapViewOfFile(ptr)
    kernel32.CloseHandle(handle)

with open(r"D:\PhoneCam\tests\output\shm_result.txt", "w") as f:
    f.write("\n".join(result))
