// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "amd_smu.h"

namespace octool { namespace core {
// Exact Shimada 1Ah/08h, 1022:153a at domain 0 / BDF 0 only. Returned raw is
// binary32, whereas the setter accepts integer multiplier * 100. Never reuse
// the query's raw word as the setter argument.
struct AmdPboScalar {
    int error = 0;
    std::uint32_t raw = 0, response = 0;
    double multiplier = 0;
    bool valid = false, rawValid = false, messageAttempted = false;
};
enum class AmdPboPhase { Validate, Compare, Write, Verify, Complete };
struct AmdPboScalarUpdate {
    int error = 0;
    AmdPboPhase phase = AmdPboPhase::Validate;
    AmdPboScalar before, after;
    std::uint32_t argument = 0, response = 0;
    bool writeAttempted = false, accepted = false, verified = false, unchanged = false;
};
// Preserve nonnegative finite 0..10 query values; zero's policy meaning is not
// inferred. High-level writes deliberately support integer 1..10 only.
bool decodeAmdPboScalar(std::uint32_t raw, double &multiplier);
const char *amdPboPhaseName(AmdPboPhase phase);
AmdPboScalar readShimadaPboScalar(HardwareSession &session, const SmuTarget &target);
AmdPboScalarUpdate setShimadaPboScalar(HardwareSession &session, const SmuTarget &target,
                                     std::uint32_t expectedRaw, unsigned multiplier);
// Capability flags are firmware reports, not a guarantee that a setting will
// take effect. Preserve unknown bits and compare the complete word on apply.
struct AmdPboCapabilities {
    int error = 0;
    std::uint32_t raw = 0, response = 0;
    bool valid = false, messageAttempted = false;
};
struct AmdPboFmaxUpdate {
    int error = 0;
    AmdPboPhase phase = AmdPboPhase::Validate;
    AmdPboCapabilities before;
    unsigned requestedMHz = 0;
    std::uint32_t response = 0;
    bool writeAttempted = false, accepted = false;
    // No verified current-limit query exists for this exact profile. A mailbox
    // acknowledgement must never be presented as readback or measured clocks.
    bool verified = false;
};
AmdPboCapabilities readShimadaPboCapabilities(HardwareSession &session, const SmuTarget &target);
bool amdPboFmaxAvailable(std::uint32_t capabilities);
// 1..1048575 is the verified 20-bit encoding range, not a safe operating range.
// Requires PBO available and manual OC disabled; never enables OC or retries.
AmdPboFmaxUpdate setShimadaPboFmax(HardwareSession &session, const SmuTarget &target,
                                 std::uint32_t expectedCapabilities, unsigned mhz);
struct AmdPboRailsEncoding {
    int error = 0;
    unsigned rail0Millivolts = 0, rail1Millivolts = 0;
    std::uint32_t argument = 0;
    bool empty = false, mirrored = false;
};
// Offline original encoding only. Does not establish physical rail identity or
// authorize a hardware command. Reject truncation/wraparound and require 5 mV
// steps, 250..2800 mV for nonzero inputs; zero mirrors the other input.
AmdPboRailsEncoding encodeLegacyAmdPboRails(unsigned rail0Millivolts, unsigned rail1Millivolts);
} }
