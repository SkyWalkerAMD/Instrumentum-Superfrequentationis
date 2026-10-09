// SPDX-License-Identifier: GPL-2.0-only
#include "register_update.h"
#include <cerrno>
#include <tuple>
#include <set>

namespace octool { namespace core {
Request msrRequest(unsigned cpu, std::uint32_t index) {
    Request r; r.cpu = cpu; r.address = index; return r;
}
CpuIdentity identifyCpu(HardwareSession &session, unsigned cpu) {
    CpuIdentity id;
    auto r = session.cpuid(cpu, 0);
    if ((id.error = r.error)) return id;
    id.maximum = r.words[0];
    id.amd = r.words[1] == 0x68747541u && r.words[3] == 0x69746e65u && r.words[2] == 0x444d4163u;
    id.intel = r.words[1] == 0x756e6547u && r.words[3] == 0x49656e69u && r.words[2] == 0x6c65746eu;
    if (id.maximum < 1) { id.error = -ENOTSUP; return id; }
    r = session.cpuid(cpu, 1);
    if ((id.error = r.error)) return id;
    id.signature = r.words[0];
    const unsigned base = (id.signature >> 8) & 15;
    id.family = base + (base == 15 ? (id.signature >> 20) & 255 : 0);
    id.model = ((id.signature >> 4) & 15) | ((base == 6 || base == 15) ? (id.signature >> 12) & 240 : 0);
    id.stepping = id.signature & 15;
    return id;
}
UpdateResult updateRegisters(HardwareSession &s, const std::vector<RegisterUpdate> &plan) {
    UpdateResult out;
    if (plan.empty() || plan.size() > 4096) { out.error = -EINVAL; return out; }
    std::set<std::tuple<int, unsigned, unsigned, unsigned, unsigned, std::uint64_t>> seen;
    for (unsigned i = 0; i < plan.size(); ++i) {
        const auto &u = plan[i]; const auto &r = u.target;
        const auto full = r.width == 8 ? UINT64_MAX : r.width > 0 && r.width < 8 ? (UINT64_C(1) << (8*r.width))-1 : 0;
        out.failedIndex = i;
        if (validateRequest(r) != ValidationError::None || r.write || r.value || !u.mask ||
            (u.mask & ~full) || (u.bits & ~u.mask) || (u.compareMask & ~full) ||
            (u.lockMask & ~full) || (u.mask & u.lockMask) ||
            !seen.emplace(int(r.space), r.space == Space::Msr ? r.cpu : 0,
                r.space == Space::Pci ? r.bus : 0, r.space == Space::Pci ? r.device : 0,
                r.space == Space::Pci ? r.function : 0, r.address).second) {
            out.error = -EINVAL; return out;
        }
    }
    std::vector<std::uint64_t> values;
    for (unsigned i = 0; i < plan.size(); ++i) {
        const auto &u = plan[i]; out.failedIndex = i;
        auto r = s.execute(u.target);
        if (r.error) { out.error = r.error; return out; }
        if (r.value & u.lockMask) { out.error = -EPERM; return out; }
        if ((r.value ^ u.expected) & u.compareMask) { out.error = -EAGAIN; return out; }
        values.push_back((r.value & ~u.mask) | u.bits);
    }
    for (unsigned i = 0; i < plan.size(); ++i) {
        out.failedIndex = i;
        if ((out.error = s.checkpoint())) return out;
        auto r = plan[i].target; r.write = true; r.value = values[i];
        out.writeAttempted = true;
        const auto reply = s.execute(r);
        if ((out.error = reply.error)) return out;
        ++out.completed; out.submitted.push_back(r.value);
    }
    return out;
}
} }
