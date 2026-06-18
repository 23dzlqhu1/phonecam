#include "core/pcp_protocol.h"

namespace phonecam {

bool parse_pcp_header(const uint8_t* data, int len, PcpHeader& out) {
    if (len < PCP_HEADER_SIZE_V1) return false;

    uint32_t magic;
    std::memcpy(&magic, data, 4);
    if (magic != PCP_MAGIC) return false;

    out.magic = magic;
    out.version = data[4];
    out.type = static_cast<PcpType>(data[5]);
    out.codec = static_cast<PcpCodec>(data[6]);
    out.flags = data[7];
    std::memcpy(&out.sequence, data + 8, 4);
    std::memcpy(&out.pts_us, data + 12, 8);

    if (out.version == PCP_VERSION_V2) {
        if (len < PCP_HEADER_SIZE_V2) return false;
        std::memcpy(&out.pts_ns, data + 20, 8);
        std::memcpy(&out.payload_len, data + 28, 4);
    } else if (out.version == PCP_VERSION_V1) {
        out.pts_ns = 0;
        std::memcpy(&out.payload_len, data + 20, 4);
    } else {
        return false;  // Unsupported version
    }
    return true;
}

int decode_rotation(uint8_t flags) {
    switch (flags & FLAG_ROTATION_MASK) {
        case 0x02: return 90;
        case 0x04: return 180;
        case 0x06: return 270;
        default:   return 0;
    }
}

} // namespace phonecam
