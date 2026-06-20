#pragma once
#include <cstdint>
#include <cstring>

namespace phonecam {

// PCP (PhoneCam Protocol) constants
// Protocol spec: see docs/protocol.md.
constexpr uint32_t PCP_MAGIC = 0x4D434850; // 'PHCM' little-endian
constexpr uint8_t PCP_VERSION_V1 = 0x01;
constexpr uint8_t PCP_VERSION_V2 = 0x02;
constexpr int PCP_HEADER_SIZE_V1 = 24;
constexpr int PCP_HEADER_SIZE_V2 = 32;

// Channel types
enum class PcpType : uint8_t {
    Video   = 0x01,
    Audio   = 0x02,
    Control = 0x03
};

// Codec types
enum class PcpCodec : uint8_t {
    RawRGB = 0x01,
    H264   = 0x02,
    AAC    = 0x03
};

// Frame flags
enum PcpFlag : uint8_t {
    FLAG_KEYFRAME      = 0x01,
    FLAG_ROTATION_MASK = 0x06  // bits 1-2 encode rotation
};

// Parsed PCP header
struct PcpHeader {
    uint32_t magic;
    uint8_t version;
    PcpType type;
    PcpCodec codec;
    uint8_t flags;
    uint32_t sequence;
    uint64_t pts_us;
    uint64_t pts_ns;     // v2 only, Camera2 timestamp in nanoseconds
    uint32_t payload_len;
};

// Parse PCP header from raw bytes.
// Returns false on invalid magic/version or insufficient data.
bool parse_pcp_header(const uint8_t* data, int len, PcpHeader& out);

// Extract rotation from flags (0/90/180/270 degrees)
int decode_rotation(uint8_t flags);

// Get header size for a given version
inline int header_size_for_version(uint8_t version) {
    return (version == PCP_VERSION_V2) ? PCP_HEADER_SIZE_V2 : PCP_HEADER_SIZE_V1;
}

} // namespace phonecam
