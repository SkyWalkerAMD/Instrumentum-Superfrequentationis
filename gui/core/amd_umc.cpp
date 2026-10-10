// SPDX-License-Identifier: GPL-2.0-only
#include "amd_umc.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <map>
#include <thread>

namespace octool { namespace core {
namespace {
std::uint32_t effectiveOffset(const UmcField &f, unsigned slot) {
    return f.offset + ((f.offset == 0x260 || f.offset == 0x2c0) ?
        (slot/4)*0x100+(slot%4)*4 : 0);
}
Request pci(const UmcTarget &t, unsigned offset) {
    Request r; r.space=Space::Pci; r.width=4; r.bus=t.bus; r.device=t.device;
    r.function=t.function; r.address=offset; return r;
}
Reply indirect(HardwareSession &s, const UmcTarget &t, std::uint32_t offset) {
    auto r=pci(t,0xe0); r.write=true; r.value=0x50000+t.bank*0x100000+offset;
    auto result=s.execute(r); if(result.error) return result;
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    return s.execute(pci(t,0xe4));
}
}
const std::vector<UmcField> &amdUmcFields() {
    // Generated from the fixed original ELF, with unique occurrence binding.
    // See analysis/contracts/amd-umc-fields.json and docs/amd-umc-recovery.md.
    static const std::vector<UmcField> fields = {
#include "amd_umc_fields.inc"
    };
    return fields;
}
const char *amdUmcGroupName(unsigned group) {
    static const char *const names[] = {"Clock / primary", "Refresh / activation", "Turnaround",
        "Precharge / mode", "Power / PHY latency", "Clock / power policy", "Self refresh / mode",
        "Refresh policy", "Alerts / delays", "PHY / WCK", "Clock bypass / states", "DF / alternate fields"};
    return group < 12 ? names[group] : "Unknown";
}
std::vector<std::uint32_t> amdUmcOffsets(unsigned slot) {
    std::vector<std::uint32_t> out;
    if(slot>15) return out;
    for(const auto &f:amdUmcFields()) out.push_back(effectiveOffset(f,slot));
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
    return out;
}
UmcDecode decodeAmdUmc(const std::vector<UmcRegister> &registers,unsigned slot) {
    UmcDecode out;
    if(slot>15 || registers.size()>56) { out.error=-EINVAL; return out; }
    const auto offsets=amdUmcOffsets(slot);
    std::map<std::uint32_t,std::uint32_t> raw;
    for(const auto &r:registers) {
        if(!std::binary_search(offsets.begin(),offsets.end(),r.offset) || !raw.emplace(r.offset,r.value).second) {
            out.error=-EINVAL; return out;
        }
    }
    for(const auto &f:amdUmcFields()) {
        UmcValue v; v.id=f.id; v.offset=effectiveOffset(f,slot);
        const auto it=raw.find(v.offset);
        if(it==raw.end()) v.error=-ENODATA;
        else {
            v.raw=it->second;
            v.encoded=std::uint32_t((std::uint64_t(v.raw)>>f.low)&((UINT64_C(1)<<(f.high-f.low+1))-1));
        }
        out.values.push_back(v);
    }
    return out;
}
UmcSnapshot readAmdUmc(HardwareSession &s,const UmcTarget &t) {
    UmcSnapshot out; out.target=t;
    // The legacy search reaches these encoded banks; the user selects one.
    // Presence is not inferred from an OS CPU, DIMM label or marketing name.
    if(t.bank>22 || t.refreshSlot>15 || validateRequest(pci(t,0))!=ValidationError::None) {
        out.error=-EINVAL; return out;
    }
    out.identity=identifyCpu(s,t.cpu);
    if((out.error=out.identity.error)) return out;
    if(!out.identity.amd || out.identity.family!=0x1a) { out.error=-ENOTSUP; return out; }
    auto r=s.execute(pci(t,0)); if((out.error=r.error)) return out;
    out.pciIdentity=std::uint32_t(r.value);
    if(out.pciIdentity!=0x153a1022 && out.pciIdentity!=0x14d81022) { out.error=-ENODEV; return out; }
    r=indirect(s,t,0x200); if((out.error=r.error)) return out;
    const auto clock=std::uint32_t(r.value);
    if(clock==UINT32_MAX || !(clock&0xffff) || (clock&0xffff)>65000) { out.error=-ENODATA; return out; }
    for(auto offset:amdUmcOffsets(t.refreshSlot)) {
        UmcRegister reg; reg.offset=offset;
        if(offset==0x200) reg.value=clock;
        else {
            r=indirect(s,t,offset); if((out.error=r.error)) return out;
            reg.value=std::uint32_t(r.value);
        }
        out.registers.push_back(reg);
    }
    out.decoded=decodeAmdUmc(out.registers,t.refreshSlot); out.error=out.decoded.error;
    return out;
}
} }
