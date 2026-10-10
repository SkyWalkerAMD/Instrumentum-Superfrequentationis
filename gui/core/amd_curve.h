// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_smu.h"

namespace octool { namespace core {
struct AmdCurveReply {
    int error = 0;
    std::uint32_t response = 0, raw = 0;
    bool messageAttempted = false, valid = false;
};
// The recovered Shimada query uses B8/BC, not the BIOS F8/FC mailbox.
// Only domain 0/BDF 0 and AMD family 1Ah/PCI 1022:153a are admitted.
// CCD/core are explicit firmware indices (0..15 / 0..7), not logical CPUs.
// A successful reply preserves the 32-bit firmware encoding; no mV conversion.
AmdCurveReply readShimadaCurve(HardwareSession &session, const SmuTarget &target,
                              unsigned ccd, unsigned core);
} }
