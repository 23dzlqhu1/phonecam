import struct, ctypes, sys

name = "PhoneCamSharedFrame"
kernel32 = ctypes.windll.kernel32
handle = kernel32.OpenFileMappingW(0x0004, False, name)

with open(r"D:\PhoneCam\tests\output\shm_check.txt", "w") as f:
    f.write(f"Handle: {handle}\n")
    if not handle:
        err = ctypes.get_last_error()
        f.write(f"OpenFileMapping failed: error {err}\n")
        sys.exit(1)

    total_size = 100
    ptr = kernel32.MapViewOfFile(handle, 0x0004, 0, 0, total_size)
    f.write(f"Ptr: {ptr}\n")
    if not ptr:
        err = ctypes.get_last_error()
        f.write(f"MapViewOfFile failed: error {err}\n")
        sys.exit(1)

    raw = (ctypes.c_char * total_size).from_address(ptr)
    data = bytes(raw)

    magic = struct.unpack_from('<I', data, 0)[0]
    active = struct.unpack_from('<i', data, 4)[0]
    f.write(f"Magic: 0x{magic:08X} (expect 0x5043414D)\n")
    f.write(f"Active slot: {active}\n")

    for i in range(2):
        off = 8 + i * 24
        w, h, seq = struct.unpack_from('<iii', data, off)
        ts = struct.unpack_from('<d', data, off + 12)[0]
        ds = struct.unpack_from('<i', data, off + 20)[0]
        expected_size = w * h * 3 if w > 0 and h > 0 else 0
        status = "OK" if ds == expected_size else f"MISMATCH (expected {expected_size})"
        f.write(f"Slot {i}: {w}x{h}, seq={seq}, ts={ts:.3f}, data_size={ds} [{status}]\n")

    kernel32.UnmapViewOfFile(ptr)
    kernel32.CloseHandle(handle)
