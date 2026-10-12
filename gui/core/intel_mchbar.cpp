// SPDX-License-Identifier: GPL-2.0-only
#include "intel_mchbar.h"
#include <cerrno>

namespace octool { namespace core { namespace intel_mchbar {
int identify(HardwareSession &s, unsigned cpu, CpuIdentity &id) {
    id = identifyCpu(s,cpu); if (id.error) return id.error;
    if (!id.intel || id.family != 6 || id.model != 0xb7) return -ENOTSUP;
    const auto leaf = s.cpuid(cpu,1); if (leaf.error) return leaf.error;
    if (leaf.words[0] != id.signature) return -EAGAIN;
    return leaf.words[2] & (1u<<31) ? -ENOTSUP : 0;
}
int readContext(HardwareSession &s, std::uint64_t &bar) {
    const auto pci = [](unsigned offset) { Request r; r.space = Space::Pci; r.width = 4; r.address = offset; return r; };
    auto r = s.execute(pci(0)); if (r.error) return r.error;
    if (r.value != 0xa7008086) return -ENODEV;
    r = s.execute(pci(8)); if (r.error) return r.error;
    if ((r.value >> 8) != 0x060000) return -ENODEV;
    r = s.execute(pci(0x4c)); if (r.error) return r.error;
    const auto high = r.value;
    r = s.execute(pci(0x48)); if (r.error) return r.error;
    bar = (high << 32) | r.value;
    r = s.execute(pci(0x4c)); if (r.error) return r.error;
    if (high != r.value) return -EAGAIN;
    if (!(bar & 1) || !(bar & baseMask)) return -ENODEV;
    if (bar & ~(baseMask | 1)) return -ENOTSUP;
    return 0;
}
} } }
