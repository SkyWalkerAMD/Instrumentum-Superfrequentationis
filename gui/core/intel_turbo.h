// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core {
enum class IntelTurboKind { Primary, Secondary };
struct IntelTurboSnapshot {
    int error = 0;
    unsigned cpu = 0;
    IntelTurboKind kind = IntelTurboKind::Primary;
    CpuIdentity identity;
    bool valid = false, hybrid = false, programmable = false, locked = true, layoutValid = false;
    std::uint64_t platformInfo = 0, flexRatio = 0, ratios = 0, coreCounts = 0;
};
struct IntelTurboUpdate : UpdateResult {
    bool verified = false, unchanged = false;
    std::uint64_t expected = 0;
    IntelTurboSnapshot after;
};
bool hasIntelTurboProfile(const CpuIdentity &identity);
std::uint32_t intelTurboRatioMsr(IntelTurboKind kind);
std::uint32_t intelTurboCountMsr(IntelTurboKind kind);
unsigned intelTurboByte(std::uint64_t word, unsigned group);
// Zero-count groups are unused. Active counts increase, ratios do not increase.
bool validIntelTurboLayout(std::uint64_t ratios, std::uint64_t coreCounts);
// One ratio byte only; active-core thresholds remain unchanged. The 1..85
// input policy is conservative for this client profile, not a stability claim.
int encodeIntelTurboRatio(const IntelTurboSnapshot &snapshot, unsigned group,
                          unsigned ratio, std::uint64_t &encoded);
// Raptor Lake-S B7 only. E-core table also requires CPUID's hybrid feature.
// Package tables are not per-physical-core overrides or measured frequency.
IntelTurboSnapshot readIntelTurbo(HardwareSession &session, unsigned cpu, IntelTurboKind kind);
IntelTurboUpdate applyIntelTurboRatio(HardwareSession &session, const IntelTurboSnapshot &snapshot,
                                     unsigned group, unsigned ratio);
} }
