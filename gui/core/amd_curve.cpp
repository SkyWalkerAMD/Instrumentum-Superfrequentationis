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
}
AmdCurveReply readShimadaCurve(HardwareSession &s, const SmuTarget &target, unsigned ccd, unsigned core) {
    AmdCurveReply out;
    if (ccd > 15 || core > 7) { out.error = -ERANGE; return out; }
    if (target.profile != SmuProfile::Shimada || target.bus || target.device || target.function) {
        out.error = -ENOTSUP; return out;
    }
    if ((out.error = s.checkpoint())) return out;
    const auto probe = probeSmu(s, target);
    if ((out.error = probe.error)) return out;
    // Do not erase a pending response. A previous nonzero completion is idle;
    // only response 1 for THIS query permits publication of its argument.
    auto reply = wait(s); if ((out.error = reply.error)) return out;
    reply = indirect(s, Response, true, 0); if ((out.error = reply.error)) return out;
    reply = indirect(s, Argument, true, (std::uint32_t(ccd) << 28) | (std::uint32_t(core) << 20));
    if ((out.error = reply.error)) return out;
    // Conservatively set before the transport call; completion can be unknown.
    out.messageAttempted = true;
    reply = indirect(s, Command, true, 0xa3); if ((out.error = reply.error)) return out;
    reply = wait(s); if ((out.error = reply.error)) return out;
    out.response = std::uint32_t(reply.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    reply = indirect(s, Argument, false); if ((out.error = reply.error)) return out;
    // Catch cancellation/deadline expiry during the final transport operation.
    if ((out.error = s.checkpoint())) return out;
    out.raw = std::uint32_t(reply.value); out.valid = true;
    return out;
}
} }
