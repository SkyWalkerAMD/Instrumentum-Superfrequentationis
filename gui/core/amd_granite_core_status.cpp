// SPDX-License-Identifier: GPL-2.0-only
#include "amd_granite_core_status.h"
#include <cerrno>

namespace octool { namespace core {
namespace {
std::uint32_t word(const std::vector<std::uint8_t> &bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
        (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
}
}
const char *amdGraniteCoreStateName(unsigned state) {
    switch (state) {
    case 0: return "fuse_disabled";
    case 1: return "runtime_enabled";
    case 2: return "runtime_not_enabled";
    default: return "invalid";
    }
}
AmdGraniteCoreStatus decodeAmdGraniteCoreStatus(const std::vector<std::uint8_t> &bytes) {
    AmdGraniteCoreStatus invalid; invalid.error = -EINVAL;
    if (bytes.size() != 0x210) return invalid;
    // CreateHob rounds the 524-byte request to an 8-byte boundary. The HOB
    // header records 528; bytes 524..527 are unspecified allocator padding.
    // EFI_HOB_GUID_TYPE: type 4, u16 length 0x210, reserved u32 zero.
    if (word(bytes,0) != 0x02100004u || word(bytes,4)) return invalid;
    const std::array<std::uint8_t,16> guid{{
        0xda,0xa7,0xdf,0x0c,0x0b,0x0d,0xf0,0x41,
        0xb9,0x8b,0x0f,0x63,0x59,0xc0,0x85,0x7f
    }};
    for (unsigned i=0;i<guid.size();++i) if (bytes[8+i] != guid[i]) return invalid;
    const std::size_t payload = 24;
    if (word(bytes,payload) != 2 || word(bytes,payload+4) != 8) return invalid;
    AmdGraniteCoreStatus out;
    out.fuseFlag = word(bytes,payload+8);
    out.presenceMask = word(bytes,payload+12);
    out.inverseControlFlag = word(bytes,payload+0x70);
    if (out.fuseFlag > 1 || out.inverseControlFlag > 1 || out.presenceMask > 3) return invalid;
    // Both flags are assigned only inside a present-target branch. With no
    // target the original producer leaves their zero-initialized words intact.
    if (!out.presenceMask && (out.fuseFlag || out.inverseControlFlag)) return invalid;
    for (unsigned target=0;target<12;++target) {
        const auto disabled = word(bytes,payload+0x10+4*target);
        const auto enabled = word(bytes,payload+0x40+4*target);
        if (target >= out.targets.size()) {
            if (disabled || enabled) return invalid;
            for (unsigned core=0;core<8;++core)
                if (word(bytes,payload+0x74+4*(8*target+core))) return invalid;
            continue;
        }
        if (disabled > 255 || enabled > 255) return invalid;
        auto &record = out.targets[target]; record.index = target;
        record.present = (out.presenceMask & (1u << target)) != 0;
        if (!record.present && (disabled != 255 || enabled)) return invalid;
        record.disabledMask = disabled; record.enabledMask = enabled;
        record.overlapMask = disabled & enabled;
        for (unsigned core=0;core<record.states.size();++core) {
            const auto state = word(bytes,payload+0x74+4*(8*target+core));
            const unsigned expected = disabled & (1u << core) ? 0 : enabled & (1u << core) ? 1 : 2;
            if (state != expected) return invalid;
            record.states[core] = state;
        }
    }
    out.valid = true; return out;
}
} }
