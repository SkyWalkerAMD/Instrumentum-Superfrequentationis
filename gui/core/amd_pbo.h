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
} }
