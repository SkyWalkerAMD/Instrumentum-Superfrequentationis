// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_oc.h"

namespace octool { namespace core {
enum class IntelTvbField { RatioClipping, VoltageOptimization, RingDownBin };
struct IntelTvbSnapshot {
    int error = 0;
    unsigned cpu = 0;
    CpuIdentity identity;
    bool valid = false;
    std::uint64_t flexRatio = 0;
    IntelOcResponse response;
};
struct IntelTvbUpdate {
    int error = 0;
    IntelOcStage stage = IntelOcStage::Validate;
    bool writeAttempted = false, verified = false, unchanged = false;
    std::uint32_t expected = 0;
    IntelOcResponse response;
    IntelTvbSnapshot readback;
};
bool intelTvbEditable(const IntelTvbSnapshot &snapshot);
bool intelTvbRatioClipping(std::uint32_t data);
bool intelTvbVoltageOptimization(std::uint32_t data);
bool intelRingDownBin(std::uint32_t data);
// Public values are 0 = disabled, 1 = enabled. The three mailbox bits are
// disable bits; change only the selected bit and preserve the entire rest.
int encodeIntelTvb(IntelTvbField field, unsigned enabled,
                   std::uint32_t previous, std::uint32_t &encoded);
// Raptor Lake-S (06_B7), bare metal, MSR capability, successful command 0x18.
// Query writes a mailbox request, never a settings-change command. A successful
// response describes configuration, not proof of TVB activity on a given SKU.
// All calls must execute inside one HardwareService transaction.
IntelTvbSnapshot readIntelTvb(HardwareSession &session, unsigned cpu);
IntelTvbUpdate applyIntelTvb(HardwareSession &session, const IntelTvbSnapshot &snapshot,
                            IntelTvbField field, unsigned enabled);

struct IntelTvbTableRow {
    unsigned activeCores = 0;
    IntelOcResponse response;
};
struct IntelTvbTableSnapshot {
    int error = 0;
    unsigned cpu = 0, selectedActiveCores = 0;
    CpuIdentity identity;
    bool scanCompleted = false;
    std::vector<IntelTvbTableRow> rows;
};
struct IntelTvbThresholds {
    unsigned temperatureA = 0, downBinsA = 0, temperatureB = 0, downBinsB = 0;
};
IntelTvbThresholds decodeIntelTvbThresholds(std::uint32_t data);
bool intelTvbTableRowValid(const IntelTvbTableRow &row);
// GET 0x24, parameter 1 = active-core selector, parameter 2 = zero.
// activeCores = 0 scans the eight candidates; 1..8 queries just that entry.
// Successful candidates do not enumerate physical cores or prove SKU support.
// This query never sends SET 0x25; per-core addressing is not supported.
// Execute inside one HardwareService transaction; no settings are changed.
IntelTvbTableSnapshot readIntelTvbTable(HardwareSession &session, unsigned cpu, unsigned activeCores = 0);

enum class IntelTvbTableField { TemperatureA, DownBinsA, TemperatureB, DownBinsB };
struct IntelTvbTableEditSnapshot {
    int error = 0;
    unsigned activeCores = 0;
    bool valid = false;
    IntelTvbSnapshot configuration;
    IntelOcResponse value;
};
struct IntelTvbTableUpdate {
    int error = 0;
    IntelOcStage stage = IntelOcStage::Validate;
    bool writeAttempted = false, verified = false, unchanged = false;
    std::uint32_t expected = 0;
    IntelOcResponse response;
    IntelTvbTableEditSnapshot readback;
};
// Conservative B7 editing profile: temperatures 0..100 C, down bins 0..10.
// All four existing bytes must fit this profile. Do not clamp or interpret
// sentinels. These FSP editor bounds are not a runtime SKU capability promise.
bool intelTvbTableValuesEditable(std::uint32_t data);
bool intelTvbTableEditable(const IntelTvbTableEditSnapshot &snapshot);
int encodeIntelTvbTable(IntelTvbTableField field, unsigned value,
                       std::uint32_t previous, std::uint32_t &encoded);
IntelTvbTableEditSnapshot readIntelTvbTableEdit(HardwareSession &session, unsigned cpu, unsigned activeCores);
// Compare the selected entry, complete TVB control word, identity and FLEX_RATIO
// before one SET 0x25. Preserve the other three bytes, verify all four afterward.
// A failed or uncertain write is never retried or rolled back automatically.
IntelTvbTableUpdate applyIntelTvbTable(HardwareSession &session, const IntelTvbTableEditSnapshot &snapshot,
                                      IntelTvbTableField field, unsigned value);
} }
