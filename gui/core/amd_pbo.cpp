// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_rsmu.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace octool { namespace core {
namespace {
int checkTarget(HardwareSession &s, const SmuTarget &target) {
    if (target.profile != SmuProfile::Shimada || target.bus || target.device || target.function) return -ENOTSUP;
    const int error = s.checkpoint(); if (error) return error;
    const auto probe = probeSmu(s, target);
    if (probe.error) return probe.error;
    return probe.identity.model == 0x08 ? 0 : -ENOTSUP;
}
std::uint32_t floatBits(unsigned multiplier) {
    const float value = float(multiplier); std::uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw)); return raw;
}
}
bool decodeAmdPboScalar(std::uint32_t raw, double &multiplier) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "Requires binary32");
    multiplier = 0;
    // Reject negative zero, negative values and all non-finite encodings before
    // floating-point arithmetic (including signaling NaN).
    if ((raw & 0x80000000u) || (raw & 0x7f800000u) == 0x7f800000u) return false;
    float value = 0; std::memcpy(&value, &raw, sizeof(raw));
    if (value > 10) return false;
    multiplier = value; return true;
}
const char *amdPboPhaseName(AmdPboPhase phase) {
    switch (phase) {
    case AmdPboPhase::Validate: return "validate";
    case AmdPboPhase::Compare: return "compare";
    case AmdPboPhase::Write: return "write";
    case AmdPboPhase::Verify: return "verify";
    case AmdPboPhase::Complete: return "complete";
    }
    return "invalid";
}
AmdPboScalar readShimadaPboScalar(HardwareSession &s, const SmuTarget &target) {
    AmdPboScalar out;
    if ((out.error = checkTarget(s, target))) return out;
    const auto reply = detail::exchangeShimadaRsmu(s, 0x6d, 0, true, true);
    out.error = reply.error; out.response = reply.response; out.messageAttempted = reply.messageAttempted;
    if (out.error) return out;
    out.raw = reply.raw; out.rawValid = true;
    if (!decodeAmdPboScalar(out.raw, out.multiplier)) { out.error = -ERANGE; return out; }
    out.valid = true; return out;
}
AmdPboScalarUpdate setShimadaPboScalar(HardwareSession &s, const SmuTarget &target,
                                     std::uint32_t expectedRaw, unsigned multiplier) {
    AmdPboScalarUpdate out; double previous = 0;
    if (multiplier < 1 || multiplier > 10 || !decodeAmdPboScalar(expectedRaw, previous)) {
        out.error = -ERANGE; return out;
    }
    out.argument = multiplier * 100;
    out.phase = AmdPboPhase::Compare;
    out.before = readShimadaPboScalar(s, target);
    if ((out.error = out.before.error)) return out;
    if (out.before.raw != expectedRaw) { out.error = -EAGAIN; return out; }
    const auto requested = floatBits(multiplier);
    if (out.before.raw == requested) {
        if ((out.error = s.checkpoint())) return out;
        out.after = out.before; out.verified = out.unchanged = true;
        out.phase = AmdPboPhase::Complete; return out;
    }
    // Revalidate identity immediately before mutating the shared setting.
    if ((out.error = checkTarget(s, target))) return out;
    out.phase = AmdPboPhase::Write;
    const auto sent = detail::exchangeShimadaRsmu(s, 0x5b, out.argument, false, true);
    out.writeAttempted = sent.messageAttempted; out.response = sent.response;
    out.accepted = sent.response == 1;
    if ((out.error = sent.error)) return out;
    out.phase = AmdPboPhase::Verify;
    out.after = readShimadaPboScalar(s, target);
    if ((out.error = out.after.error)) return out;
    if (out.after.raw != requested) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.phase = AmdPboPhase::Complete; return out;
}
} }
