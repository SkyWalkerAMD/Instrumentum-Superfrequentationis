// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "register_update.h"

namespace octool { namespace core {
struct IntelThermalSnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t raw = 0, platformInfo = 0;
};
struct IntelThermalUpdate : UpdateResult {
    bool verified = false, unchanged = false, expectedValid = false;
    std::uint64_t expected = 0;
    IntelThermalSnapshot after;
};
// Linux v6.12 intel_tcc.c: six-bit TCC offset for 06_B7, 06_8F, 06_CF.
bool hasIntelThermalProfile(const CpuIdentity &identity);
unsigned intelDefaultTccTemperature(std::uint64_t raw);
unsigned intelTccOffset(std::uint64_t raw);
int intelTccTemperature(std::uint64_t raw);
bool intelThermalEditable(const IntelThermalSnapshot &snapshot);
// Only bits 29:24. Values 0..63 C below TjMax; require positive threshold.
int encodeIntelTccOffset(unsigned degrees, std::uint64_t previous, std::uint64_t &encoded);
IntelThermalSnapshot readIntelThermal(HardwareSession &session, unsigned cpu);
// Recheck PLATFORM_INFO bit 30 and target bit 31 lock, compare every old bit,
// one write and full readback. Do not modify clamp, time window or lock.
IntelThermalUpdate applyIntelThermal(HardwareSession &session,
    const IntelThermalSnapshot &snapshot, unsigned degrees);
} }
