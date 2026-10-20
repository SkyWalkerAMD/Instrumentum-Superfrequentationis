// SPDX-License-Identifier: GPL-2.0-only
#include "amd_curve.h"
#include <cerrno>
#include <chrono>
#include <thread>

namespace octool { namespace core {
namespace {
const std::uint32_t Response = 0x3b10970, Command = 0x3b10924, Argument = 0x3b10a40;
void delay() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
Reply indirect(HardwareSession &s, std::uint32_t address, bool write, std::uint32_t value = 0) {
    Reply reply;
    if ((reply.error = s.checkpoint())) return reply;
    Request r; r.space = Space::Pci; r.width = 4; r.address = 0xb8; r.write = true; r.value = address;
    reply = s.execute(r); if (reply.error) return reply;
    delay();
    if ((reply.error = s.checkpoint())) return reply;
    r.address = 0xbc; r.write = write; r.value = value;
    reply = s.execute(r);
    if (write && !reply.error) delay();
    return reply;
}
Reply wait(HardwareSession &s) {
    for (unsigned poll = 0; poll < 100; ++poll) {
        auto reply = indirect(s, Response, false);
        if (reply.error || reply.value) return reply;
        delay();
    }
    Reply reply; reply.error = -ETIMEDOUT; return reply;
}
bool targetAllowed(const SmuTarget &target) {
    return target.profile == SmuProfile::Shimada && !target.bus && !target.device && !target.function;
}
bool countInRange(std::uint32_t raw) {
    return raw <= 50 || raw >= UINT32_MAX - 49; // canonical sign-extended -50..-1
}
AmdCurveReply exchange(HardwareSession &s, std::uint32_t command, std::uint32_t argument, bool readArgument) {
    AmdCurveReply out;
    // Do not erase a pending response. A previous nonzero completion is idle;
    // only response 1 for THIS query permits publication of its argument.
    auto reply = wait(s); if ((out.error = reply.error)) return out;
    reply = indirect(s, Response, true, 0); if ((out.error = reply.error)) return out;
    reply = indirect(s, Argument, true, argument);
    if ((out.error = reply.error)) return out;
    // Conservatively set before the transport call; completion can be unknown.
    out.messageAttempted = true;
    reply = indirect(s, Command, true, command); if ((out.error = reply.error)) return out;
    reply = wait(s); if ((out.error = reply.error)) return out;
    out.response = std::uint32_t(reply.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    if (readArgument) {
        reply = indirect(s, Argument, false); if ((out.error = reply.error)) return out;
        out.raw = std::uint32_t(reply.value);
    }
    // Catch cancellation/deadline expiry during the final transport operation.
    if ((out.error = s.checkpoint())) { out.raw = 0; return out; }
    out.valid = true;
    return out;
}
}
AmdCurveReply readShimadaCurve(HardwareSession &s, const SmuTarget &target, unsigned ccd, unsigned core) {
    AmdCurveReply out;
    if (ccd > 15 || core > 7) { out.error = -ERANGE; return out; }
    if (!targetAllowed(target)) { out.error = -ENOTSUP; return out; }
    if ((out.error = s.checkpoint())) return out;
    const auto probe = probeSmu(s, target);
    if ((out.error = probe.error)) return out;
    return exchange(s, 0xa3, (std::uint32_t(ccd) << 28) | (std::uint32_t(core) << 20), true);
}
bool parseAmdCurveCores(const std::string &text, unsigned &mask) {
    mask = 0;
    if (text.empty() || text.size() > 15 || !(text.size() & 1)) return false;
    unsigned parsed = 0;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        if (text[i] < '0' || text[i] > '7' || (i && (text[i-1] != ',' || text[i] <= text[i-2]))) return false;
        parsed |= 1u << (text[i] - '0');
    }
    mask = parsed; return true;
}
const char *amdCurvePhaseName(AmdCurvePhase phase) {
    switch (phase) {
    case AmdCurvePhase::Validate: return "validate";
    case AmdCurvePhase::Preflight: return "preflight";
    case AmdCurvePhase::Recheck: return "recheck";
    case AmdCurvePhase::Write: return "write";
    case AmdCurvePhase::Verify: return "verify";
    case AmdCurvePhase::Complete: return "complete";
    }
    return "invalid";
}
int encodeShimadaCurve(unsigned ccd, unsigned core, int margin, std::uint32_t &argument) {
    argument = 0;
    if (ccd > 15 || core > 7 || margin < -50 || margin > 50) return -ERANGE;
    argument = (std::uint32_t(ccd) << 28) | (std::uint32_t(core) << 20) | (std::uint32_t(margin) & 0xffffu);
    return 0;
}
AmdCurveSnapshot readShimadaCurves(HardwareSession &s, const SmuTarget &target, unsigned ccd, unsigned coreMask) {
    AmdCurveSnapshot out; out.ccd = ccd; out.coreMask = coreMask;
    if (ccd > 15 || !coreMask || coreMask > 255) { out.error = -ERANGE; return out; }
    for (unsigned core = 0; core < 8; ++core) if (coreMask & (1u << core)) {
        const auto reply = readShimadaCurve(s, target, ccd, core);
        if ((out.error = reply.error)) { out.raw = {}; return out; }
        out.raw[core] = reply.raw;
    }
    if ((out.error = s.checkpoint())) { out.raw = {}; return out; }
    out.valid = true; return out;
}
AmdCurveUpdate setShimadaCurves(HardwareSession &s, const SmuTarget &target,
                               const AmdCurveSnapshot &expected, int margin) {
    AmdCurveUpdate out;
    if (!expected.valid || expected.error) { out.error = -EINVAL; return out; }
    if (expected.ccd > 15 || !expected.coreMask || expected.coreMask > 255 || margin < -50 || margin > 50) {
        out.error = -ERANGE; return out;
    }
    for (unsigned core = 0; core < 8; ++core) if ((expected.coreMask & (1u << core)) && !countInRange(expected.raw[core])) {
        out.error = -ERANGE; return out;
    }
    if (!targetAllowed(target)) { out.error = -ENOTSUP; return out; }
    if ((out.error = s.checkpoint())) return out;
    const auto probe = probeSmu(s, target);
    if ((out.error = probe.error)) return out;
    if (probe.identity.model != 0x08) { out.error = -ENOTSUP; return out; }
    // Validate every old value before the first mutation. A later conflict can
    // still leave a partial update: the service lock cannot exclude other tools.
    const auto observe = [&](unsigned core) {
        const auto r = readShimadaCurve(s, target, expected.ccd, core);
        out.response = r.response;
        if ((out.error = r.error)) return false;
        out.before[core] = r.raw; out.beforeMask |= 1u << core;
        if (r.raw != expected.raw[core]) { out.error = -EAGAIN; return false; }
        return true;
    };
    out.phase = AmdCurvePhase::Preflight;
    for (unsigned core = 0; core < 8; ++core) if (expected.coreMask & (1u << core)) {
        out.core = core; if (!observe(core)) return out;
    }
    for (unsigned core = 0; core < 8; ++core) if (expected.coreMask & (1u << core)) {
        const unsigned bit = 1u << core; out.core = core; out.phase = AmdCurvePhase::Recheck;
        if (!observe(core)) return out;
        if (expected.raw[core] == std::uint32_t(margin)) {
            out.unchangedMask |= bit; out.verifiedMask |= bit;
            out.after[core] = expected.raw[core]; out.afterMask |= bit;
            continue;
        }
        std::uint32_t argument = 0; encodeShimadaCurve(expected.ccd, core, margin, argument);
        out.phase = AmdCurvePhase::Write;
        const auto current = probeSmu(s, target);
        if ((out.error = current.error)) return out;
        if (current.identity.model != 0x08) { out.error = -ENOTSUP; return out; }
        const auto sent = exchange(s, 6, argument, false);
        if (sent.messageAttempted) out.attemptedMask |= bit;
        out.response = sent.response;
        if ((out.error = sent.error)) return out;
        out.acceptedMask |= bit;
        out.phase = AmdCurvePhase::Verify;
        const auto readback = readShimadaCurve(s, target, expected.ccd, core);
        out.response = readback.response;
        if ((out.error = readback.error)) return out;
        out.after[core] = readback.raw; out.afterMask |= bit;
        if (readback.raw != std::uint32_t(margin)) { out.error = -EIO; return out; }
        out.verifiedMask |= bit;
    }
    if ((out.error = s.checkpoint())) return out;
    out.phase = AmdCurvePhase::Complete; out.core = 8; return out;
}
} }
