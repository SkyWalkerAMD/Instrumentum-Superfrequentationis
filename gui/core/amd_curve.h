// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_smu.h"
#include <string>

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

// An explicitly selected subset of firmware cores in one CCD. No fused-core
// enumeration or Linux-CPU mapping is inferred from the mask.
struct AmdCurveSnapshot {
    int error = 0;
    unsigned ccd = 0, coreMask = 0;
    std::array<std::uint32_t, 8> raw{};
    bool valid = false;
};
enum class AmdCurvePhase { Validate, Preflight, Recheck, Write, Verify, Complete };
struct AmdCurveUpdate {
    int error = 0;
    AmdCurvePhase phase = AmdCurvePhase::Validate;
    unsigned core = 8; // 8 means no individual core has been selected yet.
    unsigned attemptedMask = 0, acceptedMask = 0, verifiedMask = 0, unchangedMask = 0;
    unsigned beforeMask = 0, afterMask = 0;
    std::uint32_t response = 0;
    std::array<std::uint32_t, 8> before{}, after{};
};
// Strict ascending decimal indices, e.g. "0,2,7"; rejects duplicates and holes
// in the syntax. A sparse core mask itself is allowed.
bool parseAmdCurveCores(const std::string &text, unsigned &mask);
const char *amdCurvePhaseName(AmdCurvePhase phase);
int encodeShimadaCurve(unsigned ccd, unsigned core, int margin, std::uint32_t &argument);
AmdCurveSnapshot readShimadaCurves(HardwareSession &session, const SmuTarget &target,
                                   unsigned ccd, unsigned coreMask);
// Family 1Ah/model 08h, PCI 1022:153a, BDF 0 only. Margin is a signed CO count
// (-50..50), NOT mV. Each expected value must be a canonical signed 32-bit
// count in that range. Recheck all selected cores before any setting command,
// then recheck each immediately before changing it. RSMU 06 is sent once per
// changed core, followed by A3 readback. Stop on the first failure; no retry,
// rollback, all-core command, automatic OC enable or physical mapping.
AmdCurveUpdate setShimadaCurves(HardwareSession &session, const SmuTarget &target,
                                const AmdCurveSnapshot &expected, int margin);
} }
