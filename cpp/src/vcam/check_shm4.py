import ctypes, struct

kernel32 = ctypes.windll.kernel32
handle = kernel32.OpenFileMappingW(0x0004, False, "PhoneCamSharedFrame")
out = []
if not handle:
    out.append("SHM_NOT_FOUND error=%d" % ctypes.get_last_error())
else:
    out.append("SHM_FOUND handle=%s" % str(handle))
    ptr = kernel32.MapViewOfFile(handle, 0x0004, 0, 0, 100)
    if not ptr:
        out.append("MAP_FAILED error=%d" % ctypes.get_last_error())
    else:
        raw = (ctypes.c_char * 100).from_address(ptr)
        data = bytes(raw)
        magic = struct.unpack_from('<I', data, 0)[0]
        active = struct.unpack_from('<i', data, 4)[0]
        out.append("MAGIC=0x%08X ACTIVE=%d" % (magic, active))
        for i in range(2):
            off = 8 + i * 24
            w, h, seq = struct.unpack_from('<iii', data, off)
            ds = struct.unpack_from('<i', data, off + 20)[0]
            out.append("SLOT%d %dx%d seq=%d ds=%d" % (i, w, h, seq, ds))
        kernel32.UnmapViewOfFile(ptr)
    kernel32.CloseHandle(handle)

with open("D:/PhoneCam/tests/output/shm_out.txt", "w") as f:
    for line in out:
        f.write(line + "\n")
