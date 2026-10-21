// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_curve.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu=130;target.profile=SmuProfile::Shimada;fake->capsRaw=7;fake->allowManualOc=true; }
    AmdOcCoreFrequenciesUpdate run(unsigned mask=0x85, unsigned ccd=15, unsigned mhz=5000,
                                  std::uint32_t caps=7, const std::atomic<bool> *cancel=nullptr, int timeout=10000) {
        AmdOcCoreFrequenciesUpdate out;
        const int e=service.transaction([&](HardwareSession &s){out=setShimadaOcCoreFrequencies(s,target,caps,ccd,mask,mhz);return out.error;},timeout,cancel);
        if(e)out.error=e;
        return out;
    }
};
void selectionSyntax() {
    for(unsigned mask=1;mask<256;++mask) {
        std::string text;
        for(unsigned core=0;core<8;++core)if(mask & (1u<<core)){if(!text.empty())text+=',';text+=char('0'+core);}
        unsigned parsed=0;assert(parseAmdCurveCores(text,parsed) && parsed==mask);
    }
    for(const char *text:{"","0,0","2,0","0,8","0,","0,,2"," 0","0, 2","00","0-7","all","0x1","-1","0,2,7,"}) {
        unsigned mask=255;assert(!parseAmdCurveCores(text,mask) && !mask);
    }
}
void explicitMasks() {
    for(unsigned mask:{1u,0x80u,0x85u,0xffu}) {
        Test t;t.fake->discardChange=true;const auto r=t.run(mask);
        assert(!r.error && r.phase==AmdPboPhase::Complete && r.failedCore==8);
        assert(r.visitedMask==mask && r.attemptedMask==mask && r.acceptedMask==mask);
        unsigned count=0;
        for(unsigned core=0;core<8;++core)if(mask & (1u<<core)) {
            const auto &v=r.cores[core];
            assert(v.accepted && v.writeAttempted && !v.verified && v.before.valid && v.before.raw==7);
            assert(v.argument==((15u<<28)|(core<<20)|5000) && v.command==0x60);
            assert(t.fake->messages[count*2]==0x6f && t.fake->messages[count*2+1]==0x60);
            const auto &args=t.fake->commandArgs[count*2+1];assert(args[0]==v.argument);
            for(unsigned i=1;i<6;++i)assert(!args[i]);
            ++count;
        } else assert(!r.cores[core].writeAttempted && !r.cores[core].accepted);
        assert(t.fake->settings==count && t.fake->messages.size()==count*2 && !t.fake->wrongCpu);
        assert(!t.fake->ocCoreArgument && !t.fake->ocAllMHz && t.fake->capsRaw==7 && t.fake->scalarRaw==0x3f800000);
    }
}
void invalidBeforeAccess() {
    for(unsigned mask:{0u,256u,0x101u,UINT32_MAX}) { Test t;assert(t.run(mask).error==-EINVAL && !t.fake->calls); }
    for(unsigned ccd:{16u,UINT32_MAX}) { Test t;assert(t.run(1,ccd).error==-ERANGE && !t.fake->calls); }
    for(unsigned mhz:{0u,0x100000u,UINT32_MAX}) { Test t;assert(t.run(1,0,mhz).error==-ERANGE && !t.fake->calls); }
    for(unsigned caps:{0u,2u,6u,0xfffffffeu}) { Test t;assert(t.run(1,0,5000,caps).error==-ENOTSUP && !t.fake->calls); }
    Test stale;stale.fake->capsRaw=0x80000007;
    const auto r=stale.run();assert(r.error==-EAGAIN && !r.attemptedMask && !r.acceptedMask && r.visitedMask==1 && r.failedCore==0);
}
void identities() {
    for(unsigned mode=0;mode<8;++mode) {
        Test t;
        if(mode==0)t.target.profile=SmuProfile::Phoenix;
        if(mode==1)t.target.bus=1;
        if(mode==2)t.target.device=1;
        if(mode==3)t.target.function=1;
        if(mode==4)t.fake->amd=false;
        if(mode==5)t.fake->model=0x18;
        if(mode==6)t.fake->family=0x19;
        if(mode==7)t.fake->pciId^=0x10000;
        const auto r=t.run();assert(r.error && !r.attemptedMask && !r.acceptedMask && !t.fake->commands);
    }
}
void stopOnFirstFailure() {
    const unsigned cores[]={0,2,7},previous[]={0,1,5};
    for(unsigned failure=0;failure<4;++failure)for(unsigned position=0;position<3;++position) {
        Test t;
        const auto change=[&]{
            if(failure==0)t.fake->writeCompletion=0xfd;
            if(failure==1)t.fake->capsRaw=0x80000007;
            if(failure==2)t.fake->model=0x18;
            if(failure==3)t.fake->completion=0xfe;
        };
        if(!position)change();
        else t.fake->afterRequest=[&]{if(t.fake->settings==position)change();};
        const auto r=t.run();const auto bit=1u<<cores[position];
        assert(r.error && r.failedCore==cores[position] && r.visitedMask==(previous[position]|bit));
        assert(r.acceptedMask==previous[position] && r.attemptedMask==(previous[position]|(failure==0?bit:0u)));
        assert(t.fake->settings==position+(failure==0?1u:0u));
        for(unsigned n=position+1;n<3;++n)assert(!r.cores[cores[n]].writeAttempted && !r.cores[cores[n]].before.valid);
    }
}
void middleCoreFaults() {
    Test base;assert(!base.run().error);const auto count=base.fake->calls/3;
    assert(base.fake->calls==count*3);
    for(unsigned n=count;n<count*2;++n) {
        Test t;t.fake->failAt=int(n);const auto r=t.run();
        assert(r.error==-EACCES && t.fake->calls==n+1 && r.failedCore==2);
        assert(r.acceptedMask==1 && !(r.visitedMask & 0x80) && t.fake->settings<=2);
    }
}
void cancellationAndDeadline() {
    Test base;assert(!base.run().error);const auto count=base.fake->calls/3;
    for(unsigned n=count;n<=count*2;++n) {
        Test t;std::atomic<bool> cancel{false};
        t.fake->afterRequest=[&]{if(t.fake->calls==n)cancel.store(true);};
        const auto r=t.run(0x85,15,5000,7,&cancel);
        assert(r.error==-ECANCELED && t.fake->calls==n && (r.acceptedMask & 1u) && !(r.visitedMask & 0x80));
        assert(t.fake->settings<=2 && !r.cores[7].before.valid);
    }
    Test expired;expired.fake->busyBefore=true;const auto r=expired.run(0x85,15,5000,7,nullptr,1);
    assert(r.error==-ETIMEDOUT && !r.attemptedMask && !expired.fake->settings);
}
}
int main() {
    selectionSyntax();explicitMasks();invalidBeforeAccess();identities();stopOnFirstFailure();middleCoreFaults();cancellationAndDeadline();
    std::cout<<"7 AMD selected-core frequency scenario groups passed\n";
}
