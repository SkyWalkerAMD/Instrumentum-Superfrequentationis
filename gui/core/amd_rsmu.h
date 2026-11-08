// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware.h"
#include <cerrno>
#include <chrono>
#include <thread>

// Internal transports for individually verified RSMU commands. Callers
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
    if ((command != 2 && command != 6 && command != 7 && command != 0xa3 && command != 0xa7 && command != 0xa8 && command != 0x5b && command != 0x6d && command != 0x6e && command != 0x6f && command != 0x70 &&
         command != 0x56 && command != 0x57 && command != 0x58 && command != 0x59 && command != 0x5c && command != 0xa6 &&
         command != 0x5d && command != 0x5e && command != 0x5f && command != 0x60) ||
        ((command != 6 && command != 0xa3) != sixArguments) ||
        ((command == 0x5c || command == 0xa6) && (!readArgument || (command == 0xa6 ? argument != 0 : argument > 127))) ||
        ((command == 2 || command == 0x6e || command == 0xa8) && (!readArgument || argument)) ||
        (command == 0xa7 && (readArgument || !(argument <= 5 || argument >= 0xfffffff6u)))) {
        out.error = -EINVAL; return out;
    }
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
// Granite 98.84.0 uses a different message/response pair. Keep this transport
// closed to the individually checked version/capability, Fmax, scalar, limit,
// all-core CO and zero-argument OC-mode commands. The wrappers
// below further restrict each operation's command family.
inline AmdRsmuReply exchangeGranitePbo(HardwareSession &s, std::uint32_t command,
                                     std::uint32_t argument = 0) {
    AmdRsmuReply out;
    const bool limit = command >= 0x56 && command <= 0x59;
    const bool mode = command == 0x5d || command == 0x5e;
    const bool curve = command == 7;
    const bool setting = command == 0x70 || command == 0x5b || limit || mode || curve;
    if ((command != 2 && command != 0x6d && command != 0x6e && command != 0x6f && !setting) ||
        (command == 0x70 ? !argument || argument > 0xfffffu :
         command == 0x5b ? argument < 100 || argument > 1000 || argument % 100 :
         curve ? !(argument <= 30 || (argument >= 0xffce && argument <= 0xffff)) :
         limit ? !argument || (command == 0x59 ? argument > 100 : argument > 2000000 || argument % 1000) : argument != 0)) {
        out.error = -EINVAL; return out;
    }
    const auto wait = [&]() {
        for (unsigned n = 0; n < 100; ++n) {
            auto r = rsmuIndirect(s,0x3b10570,false);
            if (r.error || r.value) return r;
            rsmuDelay();
        }
        Reply r; r.error = -ETIMEDOUT; return r;
    };
    auto r = wait(); if ((out.error = r.error)) return out;
    r = rsmuIndirect(s,0x3b10570,true,0); if ((out.error = r.error)) return out;
    for (unsigned i = 0; i < 6; ++i) {
        r = rsmuIndirect(s,0x3b10a40+4*i,true,i ? 0 : argument);
        if ((out.error = r.error)) return out;
    }
    out.messageAttempted = true;
    r = rsmuIndirect(s,0x3b10524,true,command); if ((out.error = r.error)) return out;
    r = wait(); if ((out.error = r.error)) return out;
    out.response = std::uint32_t(r.value);
    if (out.response != 1) { out.error = -EIO; return out; }
    if (!setting) {
        r = rsmuIndirect(s,0x3b10a40,false); if ((out.error = r.error)) return out;
        out.raw = std::uint32_t(r.value);
    }
    if ((out.error = s.checkpoint())) { out.raw = 0; return out; }
    out.valid = true; return out;
}
inline AmdRsmuReply exchangeGraniteFmax(HardwareSession &s, std::uint32_t command,
                                      std::uint32_t argument = 0) {
    if (command != 2 && command != 0x6e && command != 0x6f && command != 0x70) {
        AmdRsmuReply out; out.error = -EINVAL; return out;
    }
    return exchangeGranitePbo(s,command,argument);
}
inline AmdRsmuReply exchangeGraniteScalar(HardwareSession &s, std::uint32_t command,
                                        std::uint32_t argument = 0) {
    if (command != 2 && command != 0x6d && command != 0x6f && command != 0x5b) {
        AmdRsmuReply out; out.error = -EINVAL; return out;
    }
    return exchangeGranitePbo(s,command,argument);
}
inline AmdRsmuReply exchangeGraniteLimits(HardwareSession &s, std::uint32_t command,
                                        std::uint32_t argument = 0) {
    if (command != 2 && command != 0x6f && (command < 0x56 || command > 0x59)) {
        AmdRsmuReply out; out.error = -EINVAL; return out;
    }
    return exchangeGranitePbo(s,command,argument);
}
inline AmdRsmuReply exchangeGraniteMode(HardwareSession &s, std::uint32_t command,
                                      std::uint32_t argument = 0) {
    if (command != 2 && command != 0x6f && command != 0x5d && command != 0x5e) {
        AmdRsmuReply out; out.error = -EINVAL; return out;
    }
    return exchangeGranitePbo(s,command,argument);
}
inline AmdRsmuReply exchangeGraniteCurve(HardwareSession &s, std::uint32_t command,
                                       std::uint32_t argument = 0) {
    if (command != 2 && command != 0x6f && command != 7) {
        AmdRsmuReply out; out.error = -EINVAL; return out;
    }
    return exchangeGranitePbo(s,command,argument);
}
} } }
