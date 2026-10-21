// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware.h"
#include <cerrno>
#include <chrono>
#include <thread>

// Internal transport for individually verified Shimada RSMU commands. Callers
// must validate the CPU/model/PCI identity first and hold a service transaction.
// This is neither the BIOS SMU mailbox nor the MP1 mailbox.
namespace octool { namespace core { namespace detail {
struct AmdRsmuReply {
    int error = 0;
    std::uint32_t response = 0, raw = 0;
    bool messageAttempted = false, valid = false;
};
inline void rsmuDelay() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
inline Reply rsmuIndirect(HardwareSession &s, std::uint32_t address, bool write, std::uint32_t value = 0) {
    Reply reply;
    if ((reply.error = s.checkpoint())) return reply;
    Request r; r.space = Space::Pci; r.width = 4; r.address = 0xb8; r.write = true; r.value = address;
    reply = s.execute(r); if (reply.error) return reply;
    rsmuDelay();
    if ((reply.error = s.checkpoint())) return reply;
    r.address = 0xbc; r.write = write; r.value = value;
    reply = s.execute(r);
    if (write && !reply.error) rsmuDelay();
    return reply;
}
inline Reply rsmuWait(HardwareSession &s) {
    for (unsigned poll = 0; poll < 100; ++poll) {
        auto reply = rsmuIndirect(s, 0x3b10970, false);
        if (reply.error || reply.value) return reply;
        rsmuDelay();
    }
    Reply reply; reply.error = -ETIMEDOUT; return reply;
}
inline AmdRsmuReply exchangeShimadaRsmu(HardwareSession &s, std::uint32_t command,
                                       std::uint32_t argument, bool readArgument, bool sixArguments = false) {
    AmdRsmuReply out;
    // Keep the internal entry point closed to arbitrary message IDs as well.
    if ((command != 6 && command != 0xa3 && command != 0x5b && command != 0x6d && command != 0x6f && command != 0x70) ||
        ((command != 6 && command != 0xa3) != sixArguments)) { out.error = -EINVAL; return out; }
    auto reply = rsmuWait(s); if ((out.error = reply.error)) return out;
    reply = rsmuIndirect(s, 0x3b10970, true, 0); if ((out.error = reply.error)) return out;
    for (unsigned i = 0; i < (sixArguments ? 6u : 1u); ++i) {
        reply = rsmuIndirect(s, 0x3b10a40 + 4*i, true, i ? 0 : argument);
        if ((out.error = reply.error)) return out;
    }
    // Transport errors can occur after a command has reached the device.
    out.messageAttempted = true;
    reply = rsmuIndirect(s, 0x3b10924, true, command); if ((out.error = reply.error)) return out;
    reply = rsmuWait(s); if ((out.error = reply.error)) return out;
    out.response = std::uint32_t(reply.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    if (readArgument) {
        reply = rsmuIndirect(s, 0x3b10a40, false); if ((out.error = reply.error)) return out;
        out.raw = std::uint32_t(reply.value);
    }
    if ((out.error = s.checkpoint())) { out.raw = 0; return out; }
    out.valid = true; return out;
}
} } }
