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
AmdPboCapabilities readShimadaPboCapabilities(HardwareSession &s, const SmuTarget &target) {
    AmdPboCapabilities out;
    if ((out.error = checkTarget(s, target))) return out;
    const auto reply = detail::exchangeShimadaRsmu(s, 0x6f, 0, true, true);
    out.error = reply.error; out.response = reply.response; out.messageAttempted = reply.messageAttempted;
    if (out.error) return out;
    out.raw = reply.raw; out.valid = true; return out;
}
bool amdPboFmaxAvailable(std::uint32_t caps) { return (caps & 4u) && !(caps & 1u); }
AmdPboFmaxUpdate setShimadaPboFmax(HardwareSession &s, const SmuTarget &target,
                                 std::uint32_t expectedCapabilities, unsigned mhz) {
    AmdPboFmaxUpdate out; out.requestedMHz = mhz;
    if (!mhz || mhz > 0xfffffu) { out.error = -ERANGE; return out; }
    if (!amdPboFmaxAvailable(expectedCapabilities)) { out.error = -ENOTSUP; return out; }
    out.phase = AmdPboPhase::Compare;
    out.before = readShimadaPboCapabilities(s, target);
    if ((out.error = out.before.error)) return out;
    if (out.before.raw != expectedCapabilities) { out.error = -EAGAIN; return out; }
    if ((out.error = checkTarget(s, target))) return out;
    out.phase = AmdPboPhase::Write;
    const auto reply = detail::exchangeShimadaRsmu(s, 0x70, mhz, false, true);
    out.writeAttempted = reply.messageAttempted; out.response = reply.response;
    out.accepted = reply.response == 1;
    if ((out.error = reply.error)) return out;
    // Complete refers only to the acknowledged command. No current Fmax
    // readback, fused-limit substitute, guessed PM offset or rollback.
    out.phase = AmdPboPhase::Complete; return out;
}
const char *amdPboLimitName(AmdPboLimit limit) {
    switch (limit) {
    case AmdPboLimit::Ppt: return "ppt";
    case AmdPboLimit::Tdc: return "tdc";
    case AmdPboLimit::Edc: return "edc";
    case AmdPboLimit::Temperature: return "temperature";
    }
    return "invalid";
}
const char *amdPboLimitUnit(AmdPboLimit limit) {
    switch (limit) {
    case AmdPboLimit::Ppt: return "W";
    case AmdPboLimit::Tdc: case AmdPboLimit::Edc: return "A";
    case AmdPboLimit::Temperature: return "C";
    }
    return "";
}
unsigned amdPboLimitMaximum(AmdPboLimit limit) {
    switch (limit) {
    case AmdPboLimit::Ppt: case AmdPboLimit::Tdc: case AmdPboLimit::Edc: return 2000;
    case AmdPboLimit::Temperature: return 100;
    }
    return 0;
}
int encodeAmdPboLimit(AmdPboLimit limit, unsigned value, std::uint32_t &command, std::uint32_t &argument) {
    command = argument = 0;
    if (!amdPboLimitMaximum(limit)) return -EINVAL;
    if (!value || value > amdPboLimitMaximum(limit)) return -ERANGE;
    switch (limit) {
    case AmdPboLimit::Ppt: command = 0x56; break;
    case AmdPboLimit::Tdc: command = 0x57; break;
    case AmdPboLimit::Edc: command = 0x58; break;
    case AmdPboLimit::Temperature: command = 0x59; break;
    }
    argument = limit == AmdPboLimit::Temperature ? value : value * 1000u;
    return 0;
}
bool amdPboLimitsAvailable(std::uint32_t caps) { return (caps & 2u) && !(caps & 1u); }
AmdPboLimitUpdate setShimadaPboLimit(HardwareSession &s, const SmuTarget &target,
                                    std::uint32_t expectedCapabilities, AmdPboLimit limit, unsigned value) {
    AmdPboLimitUpdate out; out.limit = limit; out.requested = value;
    if ((out.error = encodeAmdPboLimit(limit, value, out.command, out.argument))) return out;
    if (!amdPboLimitsAvailable(expectedCapabilities)) { out.error = -ENOTSUP; return out; }
    out.phase = AmdPboPhase::Compare;
    out.before = readShimadaPboCapabilities(s, target);
    if ((out.error = out.before.error)) return out;
    if (out.before.raw != expectedCapabilities) { out.error = -EAGAIN; return out; }
    if ((out.error = checkTarget(s, target))) return out;
    out.phase = AmdPboPhase::Write;
    const auto reply = detail::exchangeShimadaRsmu(s, out.command, out.argument, false, true);
    out.writeAttempted = reply.messageAttempted; out.response = reply.response;
    out.accepted = reply.response == 1;
    if ((out.error = reply.error)) return out;
    // Acknowledged only. Neither command completion nor a successful capability
    // comparison establishes the current setting or its effect on the package.
    out.phase = AmdPboPhase::Complete; return out;
}
AmdOcModeUpdate setShimadaOcMode(HardwareSession &s, const SmuTarget &target,
                                 std::uint32_t expectedCapabilities, bool enabled) {
    AmdOcModeUpdate out; out.requestedEnabled = enabled; out.command = enabled ? 0x5d : 0x5e;
    out.phase = AmdPboPhase::Compare;
    out.before = readShimadaPboCapabilities(s, target);
    if ((out.error = out.before.error)) return out;
    if (out.before.raw != expectedCapabilities) { out.error = -EAGAIN; return out; }
    if (bool(out.before.raw & 1u) == enabled) {
        if ((out.error = s.checkpoint())) return out;
        out.after = out.before; out.verified = out.unchanged = true;
        out.phase = AmdPboPhase::Complete; return out;
    }
    if ((out.error = checkTarget(s, target))) return out;
    out.phase = AmdPboPhase::Write;
    const auto reply = detail::exchangeShimadaRsmu(s, out.command, 0, false, true);
    out.writeAttempted = reply.messageAttempted; out.response = reply.response;
    out.accepted = reply.response == 1;
    if ((out.error = reply.error)) return out;
    out.phase = AmdPboPhase::Verify;
    out.after = readShimadaPboCapabilities(s, target);
    if ((out.error = out.after.error)) return out;
    // Other capability bits may legitimately change when switching OC mode.
    // Keep both complete words; verify only the requested mode bit.
    if (bool(out.after.raw & 1u) != enabled) { out.error = -EIO; return out; }
    if ((out.error = s.checkpoint())) return out;
    out.verified = true; out.phase = AmdPboPhase::Complete; return out;
}
const char *amdOcFrequencyScopeName(AmdOcFrequencyScope scope) {
    switch (scope) {
    case AmdOcFrequencyScope::AllCores: return "all-cores";
    case AmdOcFrequencyScope::Core: return "core";
    case AmdOcFrequencyScope::Cores: return "cores";
    }
    return "invalid";
}
int encodeShimadaOcFrequency(AmdOcFrequencyScope scope, unsigned ccd, unsigned core, unsigned mhz,
                            std::uint32_t &command, std::uint32_t &argument) {
    command = argument = 0;
    if (scope != AmdOcFrequencyScope::AllCores && scope != AmdOcFrequencyScope::Core) return -EINVAL;
    if (scope == AmdOcFrequencyScope::AllCores && (ccd || core)) return -EINVAL;
    std::uint32_t encoded = 0;
    const int error = encodeShimadaCoreFrequency(ccd, core, mhz, encoded);
    if (error) return error;
    command = scope == AmdOcFrequencyScope::AllCores ? 0x5f : 0x60;
    argument = encoded; return 0;
}
AmdOcFrequencyUpdate setShimadaOcFrequency(HardwareSession &s, const SmuTarget &target,
    std::uint32_t expectedCapabilities, AmdOcFrequencyScope scope, unsigned ccd, unsigned core, unsigned mhz) {
    AmdOcFrequencyUpdate out; out.scope = scope; out.ccd = ccd; out.core = core; out.requestedMHz = mhz;
    if ((out.error = encodeShimadaOcFrequency(scope, ccd, core, mhz, out.command, out.argument))) return out;
    if (!(expectedCapabilities & 1u)) { out.error = -ENOTSUP; return out; }
    out.phase = AmdPboPhase::Compare;
    out.before = readShimadaPboCapabilities(s, target);
    if ((out.error = out.before.error)) return out;
    if (out.before.raw != expectedCapabilities) { out.error = -EAGAIN; return out; }
    if ((out.error = checkTarget(s, target))) return out;
    out.phase = AmdPboPhase::Write;
    const auto reply = detail::exchangeShimadaRsmu(s, out.command, out.argument, false, true);
    out.writeAttempted = reply.messageAttempted; out.response = reply.response;
    out.accepted = reply.response == 1;
    if ((out.error = reply.error)) return out;
    out.phase = AmdPboPhase::Complete; return out;
}
AmdOcCoreFrequenciesUpdate setShimadaOcCoreFrequencies(HardwareSession &s, const SmuTarget &target,
    std::uint32_t expectedCapabilities, unsigned ccd, unsigned coreMask, unsigned mhz) {
    AmdOcCoreFrequenciesUpdate out; out.ccd = ccd; out.coreMask = coreMask; out.requestedMHz = mhz;
    if (!coreMask || (coreMask & ~0xffu)) { out.error = -EINVAL; return out; }
    std::uint32_t command = 0, argument = 0;
    if ((out.error = encodeShimadaOcFrequency(AmdOcFrequencyScope::Core, ccd, 0, mhz, command, argument))) return out;
    if (!(expectedCapabilities & 1u)) { out.error = -ENOTSUP; return out; }
    for (unsigned core = 0; core < 8; ++core) {
        const unsigned bit = 1u << core;
        if (!(coreMask & bit)) continue;
        out.visitedMask |= bit;
        auto &result = out.cores[core];
        result = setShimadaOcFrequency(s, target, expectedCapabilities, AmdOcFrequencyScope::Core, ccd, core, mhz);
        out.phase = result.phase;
        if (result.writeAttempted) out.attemptedMask |= bit;
        if (result.accepted) out.acceptedMask |= bit;
        if ((out.error = result.error)) { out.failedCore = core; return out; }
    }
    out.phase = AmdPboPhase::Complete; return out;
}
AmdPboRailsEncoding encodeLegacyAmdPboRails(unsigned a, unsigned b) {
    AmdPboRailsEncoding out;
    const auto valid = [](unsigned v) { return !v || (v >= 250 && v <= 2800 && v % 5 == 0); };
    if (!valid(a) || !valid(b)) { out.error = -ERANGE; return out; }
    if (!a && !b) { out.empty = true; return out; }
    out.mirrored = !a || !b;
    if (!a) a = b;
    if (!b) b = a;
    out.rail0Millivolts = a; out.rail1Millivolts = b;
    out.argument = ((a - 245) / 5) | (((b - 245) / 5) << 17);
    return out;
}
} }
