// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace octool { namespace core {
// Firmware-target/local-core coordinates from one exact GUID HOB layout.
// Neither the target ordinal nor these masks identify PM slots, APIC IDs or
// OS CPUs. The two opaque flags do not establish SMT or physical identities.
// Absent targets use ff/0/zero-state sentinel rows, not measured fuse states.
struct AmdGraniteCoreStatusTarget {
    unsigned index = 0;
    bool present = false;
    std::uint32_t disabledMask = 0, enabledMask = 0, overlapMask = 0;
    std::array<unsigned, 8> states{};
};
struct AmdGraniteCoreStatus {
    int error = 0;
    bool valid = false;
    std::uint32_t presenceMask = 0, fuseFlag = 0, inverseControlFlag = 0;
    std::array<AmdGraniteCoreStatusTarget, 2> targets{};
};
// Strict 528-byte HOB, including type/length/reserved/GUID. The 500-byte payload
// follows the 24-byte GUID header; ignore the final four allocator padding
// bytes, whose contents are unspecified. All unused target fields in the
// payload must be zero. Retain overlapping disabled/enabled bits; disabled has
// priority in the stored state. A malformed record publishes no partial data.
AmdGraniteCoreStatus decodeAmdGraniteCoreStatus(const std::vector<std::uint8_t> &bytes);
const char *amdGraniteCoreStateName(unsigned state);
} }
