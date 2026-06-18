import struct, ctypes, sys

# Open shared memory
name = "PhoneCamSharedFrame"
kernel32 = ctypes.windll.kernel32
handle = kernel32.OpenFileMappingW(0x0004, False, name)  # FILE_MAP_READ
if not handle:
    print(f"OpenFileMapping failed: {ctypes.get_last_error()}")
    sys.exit(1)

# Read enough for header + some data
total_size = 100
ptr = kernel32.MapViewOfFile(handle, 0x0004, 0, 0, total_size)
if not ptr:
    print(f"MapViewOfFile failed: {ctypes.get_last_error()}")
    sys.exit(1)

# Read raw bytes
raw = (ctypes.c_char * total_size).from_address(ptr)
data = bytes(raw)

# Parse header
magic = struct.unpack_from('<I', data, 0)[0]
active = struct.unpack_from('<i', data, 4)[0]
print(f"Magic: 0x{magic:08X} (expect 0x5043414D)")
print(f"Active slot: {active}")

for i in range(2):
    off = 8 + i * 24
    w, h, seq = struct.unpack_from('<iii', data, off)
    ts = struct.unpack_from('<d', data, off + 12)[0]
    ds = struct.unpack_from('<i', data, off + 20)[0]
    expected_size = w * h * 3 if w > 0 and h > 0 else 0
    status = "OK" if ds == expected_size else f"MISMATCH (expected {expected_size})"
    print(f"Slot {i}: {w}x{h}, seq={seq}, ts={ts:.3f}, data_size={ds} [{status}]")

# Dump raw hex
print(f"\nRaw hex (first 64 bytes):")
for i in range(0, 64, 4):
    if i + 4 <= len(data):
        val = struct.unpack_from('<I', data, i)[0]
        print(f"  offset {i:3d}: 0x{val:08X} = {val}")

kernel32.UnmapViewOfFile(ptr)
kernel32.CloseHandle(handle)
