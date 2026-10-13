// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_oc.h"

namespace octool { namespace core {
struct IntelAvxSnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t flexRatio = 0;
    IntelOcResponse response;
};
struct IntelAvxUpdate {
    int error = 0;
    IntelOcStage stage = IntelOcStage::Validate;
    bool writeAttempted = false, verified = false, unchanged = false, expectedValid = false;
    std::uint32_t expected = 0;
    IntelOcResponse response;
    IntelAvxSnapshot after;
};
unsigned intelAvx2Offset(std::uint32_t word);
int encodeIntelAvx2Offset(unsigned bins, std::uint32_t previous, std::uint32_t &encoded);
bool intelAvxEditable(const IntelAvxSnapshot &snapshot);
// 06_B7 bare metal, MSR + AVX/AVX2 capability. Shared ratio offset configuration,
// not a physical-core selector or an observation of the current clock.
IntelAvxSnapshot readIntelAvx(HardwareSession &session, unsigned cpu);
// Query 1A / set 1B, AVX2 field 9:5. All other 27 bits are preserved.
// Check identity, OC lock and entire previous word before one write, then
// verify the entire word. Failed or uncertain writes are never retried.
IntelAvxUpdate applyIntelAvx(HardwareSession &session, const IntelAvxSnapshot &snapshot, unsigned bins);
} }
