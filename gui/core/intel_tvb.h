// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intel_oc.h"

namespace octool { namespace core {
enum class IntelTvbField { RatioClipping, VoltageOptimization };
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
// Public values are 0 = disabled, 1 = enabled. The two mailbox bits are
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
} }
