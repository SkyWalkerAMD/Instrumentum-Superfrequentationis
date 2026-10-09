// SPDX-License-Identifier: GPL-2.0-only
#include "amd_smu.h"
#include <cerrno>
#include <chrono>
#include <thread>

namespace octool { namespace core {
namespace {
struct Mailbox { std::uint32_t device, message, response, args; unsigned family; };
Mailbox mailbox(SmuProfile profile) {
    switch (profile) {
    case SmuProfile::Shimada: return {0x153a, 0x3b10930, 0x3b1097c, 0x3b109c4, 0x1a};
    case SmuProfile::Phoenix: return {0x14e8, 0x3b10528, 0x3b10578, 0x3b10998, 0x19};
    case SmuProfile::Gpt: return {0x1122, 0x3b10928, 0x3b10978, 0x3b10998, 0x1a};
    default: return {0, 0, 0, 0, 0};
    }
}
Request pci(const SmuTarget &t, unsigned offset) {
    Request r; r.space = Space::Pci; r.width = 4; r.bus = t.bus;
    r.device = t.device; r.function = t.function; r.address = offset; return r;
}
// Original index/data pair F8/FC, NOT the 60/64 SMN pair. No TSC-MSR writes,
// global flags, port 4D0 locks or ignored transport errors from the old code.
Reply indirect(HardwareSession &s, const SmuTarget &t, std::uint32_t address, bool write, std::uint32_t value = 0) {
    auto r = pci(t, 0xf8); r.write = true; r.value = address;
    auto reply = s.execute(r); if (reply.error) return reply;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    r = pci(t, 0xfc); r.write = write; r.value = value;
    reply = s.execute(r);
    if (write && !reply.error) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return reply;
}
}
bool allowedSmuMessage(SmuProfile profile, std::uint32_t message) {
    if (profile == SmuProfile::None) return false;
    if (message == 1 || message == 2) return true;
    // Only individually recovered controls, no reset/USB/firmware/table-DMA
    // commands from the original large table. Unknown arguments stay encoded.
    if (profile != SmuProfile::Shimada) return false;
    switch (message) {
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
    case 0x2b: case 0x2c: case 0x2f: case 0x35: case 0x36:
    case 0x39: case 0x3a: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: return true;
    default: return false;
    }
}
SmuProbe probeSmu(HardwareSession &s, const SmuTarget &t) {
    SmuProbe out; out.target = t;
    const auto m = mailbox(t.profile);
    if (!m.device || validateRequest(pci(t, 0)) != ValidationError::None) { out.error = -EINVAL; return out; }
    out.identity = identifyCpu(s, t.cpu);
    if ((out.error = out.identity.error)) return out;
    if (!out.identity.amd || out.identity.family != m.family) { out.error = -ENOTSUP; return out; }
    const auto id = s.execute(pci(t, 0));
    if ((out.error = id.error)) return out;
    out.pciIdentity = std::uint32_t(id.value);
    if (out.pciIdentity != (m.device << 16 | 0x1022u)) out.error = -ENODEV;
    return out;
}
SmuReply sendSmu(HardwareSession &s, const SmuTarget &t, const SmuCommand &c) {
    SmuReply out;
    if (!allowedSmuMessage(t.profile, c.message)) { out.error = -EINVAL; return out; }
    const auto probe = probeSmu(s, t);
    if ((out.error = probe.error)) return out;
    const auto m = mailbox(t.profile);
    auto wait = [&]() {
        // Also bounded independently of caller-provided session implementations.
        for (unsigned poll = 0; poll < 200; ++poll) {
            auto r = indirect(s, t, m.response, false);
            if (r.error) return r;
            if (r.value) return r;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Reply r; r.error = -ETIMEDOUT; return r;
    };
    // Do not erase a pending firmware command's response.
    auto r = wait(); if ((out.error = r.error)) return out;
    r = indirect(s, t, m.response, true, 0); if ((out.error = r.error)) return out;
    for (unsigned i = 0; i < c.args.size(); ++i) {
        r = indirect(s, t, m.args + i*4, true, c.args[i]);
        if ((out.error = r.error)) return out;
    }
    out.messageAttempted = true;
    r = indirect(s, t, m.message, true, c.message); if ((out.error = r.error)) return out;
    r = wait(); if ((out.error = r.error)) return out;
    out.response = std::uint32_t(r.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    out.completedCommands = 1;
    std::array<std::uint32_t, 6> returned{};
    for (unsigned i = 0; i < returned.size(); ++i) {
        r = indirect(s, t, m.args + i*4, false);
        if ((out.error = r.error)) return out;
        returned[i] = std::uint32_t(r.value);
    }
    out.args = returned;
    return out;
}
int encodeShimadaCoreFrequency(unsigned ccd, unsigned core, unsigned mhz, std::uint32_t &argument) {
    if (ccd > 15 || core > 7 || !mhz || mhz > 0xfffff) return -ERANGE;
    argument = (std::uint32_t(ccd) << 28) | (std::uint32_t(core) << 20) | mhz;
    return 0;
}
} }
