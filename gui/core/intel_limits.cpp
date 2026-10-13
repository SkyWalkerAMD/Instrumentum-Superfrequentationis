// SPDX-License-Identifier: GPL-2.0-only
#include "intel_limits.h"
#include <cerrno>
namespace octool { namespace core {
const std::array<IntelLimitReason,11> &intelLimitReasons() {
    static const std::array<IntelLimitReason,11> rows{{
        {"prochot","External PROCHOT",0},{"thermal","Thermal event",1},
        {"residency","Residency regulation",4},{"ratl","Average thermal limit",5},
        {"vr-thermal","Voltage regulator temperature",6},{"vr-current","Voltage regulator current",7},
        {"other","Electrical or other constraint",8},{"pl1","Package/platform PL1",10},
        {"pl2","Package/platform PL2/PL3",11},{"turbo","Maximum turbo ratio",12},
        {"transition","Turbo transition attenuation",13}
    }};
    return rows;
}
std::uint64_t intelLimitLogMask(const std::string &reason) {
    std::uint64_t mask=0;
    for (const auto &r:intelLimitReasons()) if (reason=="all" || reason==r.key) mask|=UINT64_C(1)<<(r.bit+16);
    return mask;
}
bool intelLimitsValid(const IntelLimitsSnapshot &s) {
    return !s.error && s.valid && !s.identity.error && s.identity.intel && s.identity.family==6 && s.identity.model==0xb7;
}
IntelLimitsSnapshot readIntelLimits(HardwareSession &s,unsigned cpu) {
    IntelLimitsSnapshot out; out.cpu=cpu; out.identity=identifyCpu(s,cpu);
    if ((out.error=out.identity.error)) return out;
    if (!out.identity.intel || out.identity.family!=6 || out.identity.model!=0xb7) { out.error=-ENOTSUP; return out; }
    const auto features=s.cpuid(cpu,1);
    if ((out.error=features.error)) return out;
    if (features.words[0]!=out.identity.signature) { out.error=-ENODEV; return out; }
    if (!(features.words[3] & (1u<<5)) || (features.words[2] & (1u<<31))) { out.error=-ENOTSUP; return out; }
    const auto value=s.execute(msrRequest(cpu,0x64f));
    if ((out.error=value.error)) return out;
    if ((out.error=s.checkpoint())) return out;
    out.raw=value.value; out.valid=true; return out;
}
IntelLimitsClear clearIntelLimitLogs(HardwareSession &s,const IntelLimitsSnapshot &old,const std::string &reason) {
    IntelLimitsClear out; out.selectedMask=intelLimitLogMask(reason);
    if (!intelLimitsValid(old) || !out.selectedMask) { out.error=-EINVAL; return out; }
    const auto logMask=intelLimitLogMask("all"),statusMask=logMask>>16;
    const auto fresh=readIntelLimits(s,old.cpu);
    if ((out.error=fresh.error)) return out;
    if (fresh.identity.signature!=old.identity.signature) { out.error=-ENODEV; return out; }
    // Live status is volatile; history and reserved bits must still match the
    // snapshot the user saw. This is not atomic against hardware/other tools.
    if ((fresh.raw^old.raw) & ~statusMask) { out.error=-EAGAIN; return out; }
    if (!(fresh.raw & out.selectedMask)) { out.after=fresh; out.unchanged=true; out.verified=true; return out; }
    out.submitted=(fresh.raw|logMask) & ~out.selectedMask;
    if ((out.error=s.checkpoint())) return out;
    auto request=msrRequest(old.cpu,0x64f); request.write=true; request.value=out.submitted;
    out.writeAttempted=true; const auto result=s.execute(request);
    if ((out.error=result.error)) return out;
    out.after=readIntelLimits(s,old.cpu);
    if ((out.error=out.after.error)) return out;
    if (out.after.identity.signature!=old.identity.signature) { out.error=-ENODEV; return out; }
    const auto preservedLogs=fresh.raw & logMask & ~out.selectedMask;
    if (((out.after.raw^fresh.raw) & ~(logMask|statusMask)) || (preservedLogs & ~out.after.raw)) { out.error=-EIO; return out; }
    out.stillSetMask=out.after.raw & out.selectedMask;
    if (out.stillSetMask) { out.error=-EAGAIN; return out; }
    if ((out.error=s.checkpoint())) return out;
    out.verified=true; return out;
}
} }
