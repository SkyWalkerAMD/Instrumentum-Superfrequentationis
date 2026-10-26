// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_pm.h"
#include "amd_rsmu.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu=130; target.profile=SmuProfile::Shimada; fake->allowFmaxReadback=true; fake->fmaxMHz=5500; }
    AmdPboFmax read(const std::atomic<bool> *cancel=nullptr) {
        AmdPboFmax out;
        const int e=service.transaction([&](HardwareSession &s) { out=readShimadaPboFmax(s,target); return out.error; },10000,cancel);
        if(e) { out.error=e; out.valid=false; } return out;
    }
    AmdPboFmaxUpdate set(unsigned mhz=5000, std::uint32_t caps=4, unsigned expected=5500,
                        const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdPboFmaxUpdate out;
        const int e=service.transaction([&](HardwareSession &s) { out=setShimadaPboFmax(s,target,caps,mhz,expected); return out.error; },timeout,cancel);
        if(e) { out.error=e; out.verified=false; } return out;
    }
};
void eligibility() {
    for(unsigned mode=0;mode<8;++mode) {
        Test t;
        if(mode==0) t.target.profile=SmuProfile::Phoenix;
        if(mode==1) t.target.bus=1;
        if(mode==2) t.target.device=1;
        if(mode==3) t.target.function=1;
        if(mode==4) t.fake->amd=false;
        if(mode==5) t.fake->model=0x18;
        if(mode==6) t.fake->family=0x19;
        if(mode==7) t.fake->pciId^=0x10000;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    for(unsigned value : {0u,0x100000u,UINT32_MAX}) { Test t; assert(t.set(value).error==-ERANGE && !t.fake->calls); }
    for(unsigned value : {0u,1u,2u,3u,0x10004u,0x10005u,UINT32_MAX}) { Test t; assert(t.set(5000,value).error==-ENOTSUP && !t.fake->calls); }
    Test invalid; assert(invalid.set(5000,4,0x100000).error==-ERANGE && !invalid.fake->calls);
}
void versionAndReadback() {
    for(auto version : {0u,0x00732d00u,0x00732e01u,UINT32_MAX}) {
        Test t; t.fake->firmwareVersion=version; const auto r=t.read();
        assert(r.error==-ENOTSUP && r.firmwareValid && r.firmwareVersion==version && !r.rawValid && !r.messageAttempted);
        assert(t.fake->messages==std::vector<std::uint32_t>{2});
    }
    for(auto mhz : {0u,1u,5500u,0xfffffu,0x100000u,0x7fffffffu,0x80000000u,UINT32_MAX}) {
        Test t; t.fake->fmaxMHz=mhz; const auto r=t.read();
        assert(r.rawValid && r.raw==mhz && !t.fake->settings && !t.fake->wrongCpu);
        assert(r.valid==(mhz<=0xfffff) && r.error==(mhz<=0xfffff?0:-ERANGE));
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6e}));
        for(const auto &args:t.fake->commandArgs) for(auto arg:args) assert(!arg);
    }
}
void verifiedAndUnchanged() {
    for(unsigned mhz : {1u,4999u,5000u,5500u,0xfffffu}) {
        Test t; const auto r=t.set(mhz);
        assert(!r.error && r.verified && r.phase==AmdPboPhase::Complete && r.after.valid && r.after.raw==mhz);
        assert(r.unchanged==(mhz==5500) && r.writeAttempted==(mhz!=5500) && r.accepted==(mhz!=5500));
        assert(t.fake->settings==(mhz==5500?0u:1u) && !t.fake->wrongCpu);
        if(mhz!=5500) {
            assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6e,0x6f,0x70,2,0x6f,0x6e}));
            assert(t.fake->commandArgs[4][0]==mhz);
            for(unsigned n=1;n<6;++n) assert(!t.fake->commandArgs[4][n]);
        }
    }
    Test unknown; unknown.fake->capsRaw=0x80000004;
    assert(unknown.set(5000,0x80000004).verified);
}
void changedCapabilityAndIdentity() {
    for(auto caps : {0u,5u,6u,0x80000004u}) {
        Test t; t.fake->capsRaw=caps; const auto r=t.set();
        assert(r.error==-EAGAIN && !r.writeAttempted && !t.fake->settings && r.before.raw==caps);
    }
    Test stale; stale.fake->fmaxMHz=5600; assert(stale.set().error==-EAGAIN && !stale.fake->settings);
    Test race; race.fake->afterRequest=[&] { if(race.fake->argumentReads>=3) race.fake->capsRaw=6; };
    assert(race.set().error==-EAGAIN && !race.fake->settings);
    Test identity; identity.fake->afterRequest=[&] { if(identity.fake->argumentReads==3) identity.fake->model=0x18; };
    assert(identity.set().error==-ENOTSUP && !identity.fake->settings);
}
void clampingAndAfterWriteFailures() {
    for(unsigned mode=0;mode<6;++mode) {
        Test t;
        if(mode==0) t.fake->discardChange=true;
        if(mode==1) t.fake->clampFmax=true;
        if(mode>=2) t.fake->afterRequest=[&] {
            if(!t.fake->settings) return;
            if(mode==2) t.fake->firmwareVersion=0x00732e01;
            if(mode==3) t.fake->capsRaw=6;
            if(mode==4) t.fake->failAt=int(t.fake->calls);
            if(mode==5) t.fake->fmaxMHz=UINT32_MAX;
        };
        const auto r=t.set(); assert(r.error && r.writeAttempted && !r.verified && t.fake->settings==1);
        if(mode<2) assert(r.accepted && r.after.valid && r.after.raw==(mode==0?5500u:6000u) && r.error==-EIO);
    }
}
void rejections() {
    for(auto status : {0u,2u,0xfcu,0xfdu,0xfeu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion=status; const auto r=t.set();
        assert(r.error==(status?-EIO:-ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified);
        assert(t.fake->settings==1 && t.fake->commands==5);
        Test q; q.fake->completion=status; assert(q.set().error && !q.fake->settings && q.fake->commands==1);
    }
    Test busy; busy.fake->busyBefore=true; assert(busy.set().error==-ETIMEDOUT && !busy.fake->commands);
}
void faultsAndCancellation() {
    Test query; assert(query.read().valid);
    for(unsigned n=0;n<query.fake->calls;++n) {
        Test t; t.fake->failAt=int(n); const auto r=t.read();
        assert(r.error==-EACCES && !r.valid && !r.rawValid && t.fake->calls==n+1 && !t.fake->settings);
    }
    Test base; assert(base.set().verified);
    for(unsigned n : {0u,1u,base.fake->calls/2,base.fake->calls-1}) {
        Test t; t.fake->failAt=int(n); const auto r=t.set();
        assert(r.error==-EACCES && !r.verified && t.fake->calls==n+1 && t.fake->settings<=1);
    }
    for(unsigned n : {0u,1u,base.fake->calls/2,base.fake->calls}) {
        Test t; std::atomic<bool> cancel{n==0}; t.fake->afterRequest=[&] { if(t.fake->calls==n) cancel.store(true); };
        const auto r=t.set(5000,4,5500,&cancel);
        assert(r.error==-ECANCELED && !r.verified && t.fake->calls==n && t.fake->settings<=1);
    }
    Test deadline; deadline.fake->busyBefore=true; assert(deadline.set(5000,4,5500,nullptr,1).error==-ETIMEDOUT && !deadline.fake->settings);
}
void closedReadTransport() {
    for(unsigned mode=0;mode<3;++mode) {
        Test t; assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeShimadaRsmu(s,0x6e,mode==0?1:0,mode!=1,mode!=2).error;
        },1000)==-EINVAL && !t.fake->calls);
    }
}
void railsEncoding() {
    const auto none = encodeLegacyAmdPboRails(0,0); assert(!none.error && none.empty && !none.argument);
    for (unsigned mv = 250; mv <= 2800; mv += 5) {
        const auto r = encodeLegacyAmdPboRails(mv,0), b = encodeLegacyAmdPboRails(0,mv);
        assert(!r.error && r.mirrored && !r.empty && r.rail0Millivolts == mv && r.rail1Millivolts == mv);
        assert(r.argument == b.argument && (r.argument & 511) * 5 + 245 == mv && (r.argument >> 17) * 5 + 245 == mv);
    }
    for (unsigned bad : {1u,244u,245u,249u,251u,2801u,UINT32_MAX}) {
        assert(encodeLegacyAmdPboRails(bad,1000).error == -ERANGE);
        assert(encodeLegacyAmdPboRails(1000,bad).error == -ERANGE);
    }
    assert(encodeLegacyAmdPboRails(700,1250).argument == (91u | (201u << 17)));
}
void layoutAudit() {
    const auto small = auditLegacyAmdPmLayout(0x730204,2844,2);
    assert(!small.error && small.coreSlots == 16 && small.ccdSlots == 2 && small.outsideWords == 22);
    assert(small.ranges.size() == 26 && small.ranges.front().first == 0x634 && small.ranges[15].first == 0x1fb4);
    const auto large = auditLegacyAmdPmLayout(0x731004,11636,16);
    assert(!large.error && !large.outsideWords && large.overlappingWords == 176 && large.publicFieldConflicts == 2);
    const auto six = auditLegacyAmdPmLayout(0x730604,5356,6);
    assert(six.constructorFallback && six.coreSlots == 16 && six.ccdSlots == 2);
    assert(auditLegacyAmdPmLayout(0x730204,0,2).outsideWords == 262);
    assert(auditLegacyAmdPmLayout(0x730204,2843,2).error == -EMSGSIZE);
    assert(auditLegacyAmdPmLayout(0x730205,2844,2).error == -ENOTSUP);
    for (unsigned n : {0u,17u,UINT32_MAX}) assert(auditLegacyAmdPmLayout(0x730204,2844,n).error == -EINVAL);
    for (const auto &layout : amdPmLayouts()) for (unsigned n = 1; n <= 16; ++n) {
        const auto r = auditLegacyAmdPmLayout(layout.version,layout.bytes,n); assert(!r.error && r.ranges.size() == 26);
        assert(amdPmLayoutAuditJson(r).find("\"decoder_enabled\":false") != std::string::npos);
    }
}
}
int main() {
    eligibility(); versionAndReadback(); verifiedAndUnchanged(); changedCapabilityAndIdentity();
    clampingAndAfterWriteFailures(); rejections(); faultsAndCancellation(); closedReadTransport(); railsEncoding(); layoutAudit();
    std::cout << "10 AMD Fmax readback, rails and PM layout scenario groups passed\n";
}
