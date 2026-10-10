// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"
#include <array>

namespace octool { namespace core {
struct AmdTopologyLevel {
    std::array<std::uint32_t,4> words{};
    unsigned type=0, shift=0, logicalCount=0;
    std::uint32_t globalId=0;
};
struct AmdTopology {
    int error=0;
    unsigned cpu=0;
    CpuIdentity identity;
    std::uint32_t apicId=0, socketId=0, ccdInSocket=0, coreInCcd=0, threadInCore=0;
    std::vector<AmdTopologyLevel> levels;
};
// CPUID Fn80000026, per AMD PPR 57238 pp142-144 and Linux x86 topology docs.
// No core-count heuristics or renumbering of sparse APIC IDs. These are CPUID
// topology IDs, NOT an assertion that SMU firmware uses the same target IDs.
AmdTopology readAmdTopology(HardwareSession &session,unsigned cpu);
} }
