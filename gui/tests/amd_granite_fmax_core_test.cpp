// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
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
    Test() { target.cpu=130; target.profile=SmuProfile::GraniteRidge; fake->useGraniteFmax(); fake->fmaxMHz=5500; }
    AmdPboFmax read() {
        AmdPboFmax out;
        const int e=service.transaction([&](HardwareSession &s) { out=readAmdPboFmax(s,target); return out.error; });
        if(e) { out.error=e; out.valid=false; } return out;
    }
    AmdPboFmaxUpdate set(unsigned mhz=5000, unsigned caps=4, unsigned expected=5500,
                         const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdPboFmaxUpdate out;
        const int e=service.transaction([&](HardwareSession &s) { out=setAmdPboFmax(s,target,caps,mhz,expected); return out.error; },timeout,cancel);
        if(e) { out.error=e; out.verified=false; } return out;
    }
};
void targetGuards() {
    for(unsigned mode=0;mode<10;++mode) {
        Test t;
        if(mode==0) t.target.profile=SmuProfile::Shimada;
        if(mode==1) t.target.bus=1;
        if(mode==2) t.target.device=1;
        if(mode==3) t.target.function=1;
        if(mode==4) t.fake->amd=false;
        if(mode==5) t.fake->model=8;
        if(mode==6) t.fake->family=0x19;
        if(mode==7) t.fake->pciId=0x153a1022;
        if(mode==8) t.fake->hypervisor=true;
        if(mode==9) t.target.profile=SmuProfile::Phoenix;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    for(unsigned mhz:{0u,0x100000u,UINT32_MAX}) { Test t; assert(t.set(mhz).error==-ERANGE && !t.fake->calls); }
    for(unsigned caps:{0u,1u,3u,0x10004u,UINT32_MAX}) { Test t; assert(t.set(5000,caps).error==-ENOTSUP && !t.fake->calls); }
    Test t; assert(t.set(5000,4,0x100000).error==-ERANGE && !t.fake->calls);
}
void queries() {
    for(unsigned version:{0u,0x00625300u,0x00625401u,0x00732e00u,UINT32_MAX}) {
        Test t; t.fake->firmwareVersion=version; const auto r=t.read();
        assert(r.error==-ENOTSUP && r.firmwareValid && !r.valid && !r.rawValid);
        assert(t.fake->messages==std::vector<std::uint32_t>{2} && !t.fake->settings);
    }
    for(unsigned mhz:{0u,1u,5500u,0xfffffu,0x100000u,UINT32_MAX}) {
        Test t; t.fake->fmaxMHz=mhz; const auto r=t.read();
        assert(r.valid==(mhz<=0xfffff) && r.rawValid && r.raw==mhz && !t.fake->wrongCpu);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6e,2}));
        for(const auto &args:t.fake->commandArgs) for(auto arg:args) assert(!arg);
    }
}
void writes() {
    for(unsigned mhz:{1u,5000u,5500u,0xfffffu}) {
        Test t; const auto r=t.set(mhz);
        assert(!r.error && r.verified && r.after.raw==mhz && r.unchanged==(mhz==5500));
        assert(t.fake->settings==(mhz==5500?0u:1u) && !t.fake->wrongCpu);
        if(mhz!=5500) {
            assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6e,2,2,0x6f,0x70,2,0x6f,0x6e,2}));
            assert(t.fake->commandArgs[6][0]==mhz);
            for(unsigned i=1;i<6;++i) assert(!t.fake->commandArgs[6][i]);
        }
    }
    Test unknown; unknown.fake->capsRaw=0x80000004; assert(unknown.set(5000,0x80000004).verified);
    for(bool discard:{false,true}) {
        Test t; t.fake->discardChange=discard; t.fake->clampFmax=!discard;
        const auto r=t.set(); assert(r.error==-EIO && r.accepted && !r.verified && r.after.valid);
        assert(r.after.raw==(discard?5500u:6000u) && t.fake->settings==1);
    }
}
void stateChanges() {
    Test stale; stale.fake->fmaxMHz=5600; assert(stale.set().error==-EAGAIN && !stale.fake->settings);
    for(unsigned mode=0;mode<6;++mode) {
        Test t;
        t.fake->afterRequest=[&] {
            if(mode<2 && t.fake->argumentReads==3+mode) t.fake->firmwareVersion=0x00625401;
            if(mode==2 && t.fake->argumentReads==5) t.fake->capsRaw=6;
            if(mode==3 && t.fake->argumentReads==6) t.fake->model=8;
            if(mode==4 && t.fake->settings) t.fake->firmwareVersion=0x00625401;
            if(mode==5 && t.fake->settings) t.fake->capsRaw=6;
        };
        const auto r=t.set(); assert(r.error && !r.verified && t.fake->settings==(mode>=4?1u:0u));
    }
}
void transportErrors() {
    for(unsigned status:{0u,2u,0xfcu,0xfdu,0xfeu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion=status; const auto r=t.set();
        assert(r.error==(status?-EIO:-ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified);
        assert(t.fake->settings==1 && t.fake->commands==7);
        Test q; q.fake->completion=status; assert(q.read().error && !q.fake->settings && q.fake->commands==1);
    }
    Test busy; busy.fake->busyBefore=true; assert(busy.read().error==-ETIMEDOUT && !busy.fake->commands);
}
void faults() {
    Test base; assert(base.read().valid);
    for(unsigned n=0;n<base.fake->calls;++n) {
        Test t; t.fake->failAt=int(n); const auto r=t.read();
        assert(r.error==-EACCES && !r.valid && !r.rawValid && !t.fake->settings && t.fake->calls==n+1);
    }
    Test write; assert(write.set().verified);
    for(unsigned n:{0u,1u,write.fake->calls/2,write.fake->calls-1}) {
        Test t; t.fake->failAt=int(n); const auto r=t.set();
        assert(r.error==-EACCES && !r.verified && t.fake->calls==n+1 && t.fake->settings<=1);
    }
}
void cancellation() {
    Test base; assert(base.set().verified);
    for(unsigned n:{0u,1u,base.fake->calls/2,base.fake->calls}) {
        Test t; std::atomic<bool> cancel{n==0};
        t.fake->afterRequest=[&] { if(t.fake->calls==n) cancel.store(true); };
        const auto r=t.set(5000,4,5500,&cancel);
        assert(r.error==-ECANCELED && !r.verified && t.fake->calls==n && t.fake->settings<=1);
    }
    Test t; t.fake->busyBefore=true; assert(t.set(5000,4,5500,nullptr,1).error==-ETIMEDOUT && !t.fake->settings);
}
void closedCommands() {
    for(unsigned command=0;command<256;++command) {
        if(command==2 || command==0x6e || command==0x6f || command==0x70) continue;
        Test t; assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteFmax(s,command).error;
        })==-EINVAL && !t.fake->calls);
        assert(!allowedSmuMessage(SmuProfile::GraniteRidge,command));
    }
    for(unsigned command:{2u,0x6eu,0x6fu,0x70u}) {
        Test t; assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeGraniteFmax(s,command,command==0x70?0:1).error;
        })==-EINVAL && !t.fake->calls);
    }
    Test t;
    assert(t.service.transaction([&](HardwareSession &s) {
        assert(readShimadaPboFmax(s,t.target).error==-ENOTSUP);
        assert(setShimadaPboFmax(s,t.target,4,5000,5500).error==-ENOTSUP);
        assert(readShimadaPboScalar(s,t.target).error==-ENOTSUP);
        assert(setShimadaPboLimit(s,t.target,6,AmdPboLimit::Ppt,100).error==-ENOTSUP);
        for(unsigned c:{1u,2u,0x29u,0x70u}) assert(!allowedSmuMessage(t.target.profile,c));
        return 0;
    })==0 && !t.fake->calls);
}
}
int main() {
    targetGuards(); queries(); writes(); stateChanges(); transportErrors(); faults(); cancellation(); closedCommands();
    std::cout << "8 Granite Fmax scenario groups passed\n";
}
