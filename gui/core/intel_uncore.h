// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core {
struct IntelUncoreSnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t raw = 0;
};
struct IntelUncoreUpdate : UpdateResult {
    bool verified = false, unchanged = false;
    std::uint64_t expected = 0;
    IntelUncoreSnapshot after;
};
// Explicit B7 (Raptor Lake-S) and 8F (Sapphire Rapids) profiles. The selected
// CPU locates a shared LLC/ring domain, not a per-core frequency setting.
bool hasIntelUncoreProfile(const CpuIdentity &identity);
unsigned intelUncoreMinimum(std::uint64_t raw);
unsigned intelUncoreMaximum(std::uint64_t raw);
bool intelUncoreEditable(const IntelUncoreSnapshot &snapshot);
// Ratios only. Zero/default semantics and conversion to MHz are not inferred.
int encodeIntelUncoreRange(std::uint64_t previous, unsigned minimum,
                          unsigned maximum, std::uint64_t &encoded);
IntelUncoreSnapshot readIntelUncore(HardwareSession &session, unsigned cpu);
// Change both bounds in one MSR 620 write, preserving all other bits. Fresh
// identity/value checks and full readback; no OC mailbox changes or retries.
IntelUncoreUpdate applyIntelUncoreRange(HardwareSession &session,
    const IntelUncoreSnapshot &snapshot, unsigned minimum, unsigned maximum);
} }
