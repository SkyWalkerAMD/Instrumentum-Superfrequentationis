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
    Test() { fake->useGraniteScalar(); target.profile=SmuProfile::GraniteRidge; target.cpu=130; }
    AmdPboScalar read() {
        AmdPboScalar r;
        const int e=service.transaction([&](HardwareSession &s){r=readAmdPboScalar(s,target);return r.error;});
        if(e) { r.error=e;r.valid=false; } return r;
    }
    AmdPboScalarUpdate set(unsigned value=5,unsigned expected=0x3f800000,unsigned caps=4,
        const std::atomic<bool> *cancel=nullptr,int timeout=10000) {
        AmdPboScalarUpdate r;
        const int e=service.transaction([&](HardwareSession &s){r=setAmdPboScalar(s,target,caps,expected,value);return r.error;},timeout,cancel);
        if(e) {r.error=e;r.verified=false;} return r;
    }
};
void guards() {
    for(unsigned mode=0;mode<8;++mode) {
        Test t;
        if(mode==0)t.target.bus=1;
        if(mode==1)t.target.device=1;
        if(mode==2)t.target.function=1;
        if(mode==3)t.fake->amd=false;
        if(mode==4)t.fake->model=8;
        if(mode==5)t.fake->family=0x19;
        if(mode==6)t.fake->pciId=0x153a1022;
        if(mode==7)t.fake->hypervisor=true;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    for(unsigned value:{0u,11u,UINT32_MAX}) {Test t;assert(t.set(value).error==-ERANGE && !t.fake->calls);}
    for(unsigned raw:{0x80000000u,0x7f800001u,0x41200001u}) {Test t;assert(t.set(5,raw).error==-ERANGE && !t.fake->calls);}
    for(unsigned caps:{0u,1u,0x10004u}) {Test t;assert(t.set(5,0x3f800000,caps).error==-ENOTSUP && !t.fake->calls);}
}
void reads() {
    for(unsigned version:{0u,0x00625300u,0x00625401u,0x00732e00u}) {
        Test t;t.fake->firmwareVersion=version;const auto r=t.read();
        assert(r.error==-ENOTSUP && r.firmwareValid && !r.rawValid);
        assert(t.fake->messages==std::vector<std::uint32_t>{2});
    }
    for(unsigned raw:{0u,1u,0x3f800000u,0x40200000u,0x41200000u,0x41200001u,0x80000000u,0x7f800001u}) {
        Test t;t.fake->scalarRaw=raw;const auto r=t.read();double ignored=0;
        assert(r.valid==decodeAmdPboScalar(raw,ignored) && r.rawValid && r.raw==raw);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6d,0x6f,2}));
        assert(!t.fake->wrongCpu && !t.fake->settings);
        for(const auto &args:t.fake->commandArgs)for(auto arg:args)assert(!arg);
    }
    Test oc;oc.fake->capsRaw=0x10004;assert(oc.read().raw==0 && !oc.fake->settings);
}
void writes() {
    for(unsigned value:{1u,2u,5u,10u}) {
        Test t;const auto r=t.set(value);
        assert(!r.error && r.verified && r.after.multiplier==value && r.unchanged==(value==1));
        assert(t.fake->settings==(value==1?0u:1u));
        if(value!=1) {
            assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0x6d,0x6f,2,2,0x6f,0x5b,2,0x6f,0x6d,0x6f,2}));
            assert(t.fake->commandArgs[7][0]==value*100);
            for(unsigned i=1;i<6;++i)assert(!t.fake->commandArgs[7][i]);
        }
    }
    Test unknown;unknown.fake->capsRaw=0x80000004;assert(unknown.set(5,0x3f800000,0x80000004).verified);
}
void clamping() {
    for(bool clamp:{false,true}) {
        Test t;t.fake->clampScalar=clamp;t.fake->discardChange=!clamp;const auto r=t.set();
        assert(r.error==-EIO && r.accepted && !r.verified && r.after.valid);
        assert(r.after.raw==(clamp?0x40200000u:0x3f800000u) && t.fake->settings==1);
    }
}
void stale() {
    Test old;old.fake->scalarRaw=0x40000000;assert(old.set().error==-EAGAIN && !old.fake->settings);
    Test caps;caps.fake->capsRaw=6;assert(caps.set().error==-EAGAIN && !caps.fake->settings);
    for(unsigned stage=0;stage<5;++stage) {
        Test t;t.fake->afterRequest=[&] {
            if(stage==0 && t.fake->argumentReads==3)t.fake->capsRaw=6;
            if(stage==1 && t.fake->argumentReads==4)t.fake->firmwareVersion=0x00625401;
            if(stage==2 && t.fake->argumentReads==5)t.fake->capsRaw=6;
            if(stage==3 && t.fake->settings)t.fake->firmwareVersion=0x00625401;
            if(stage==4 && t.fake->settings)t.fake->capsRaw=0x10004;
        };
        const auto r=t.set();assert(r.error && !r.verified && t.fake->settings==(stage>=3?1u:0u));
    }
}
void errors() {
    for(unsigned status:{0u,2u,0xfeu}) {
        Test t;t.fake->writeCompletion=status;const auto r=t.set();
        assert(r.error==(status?-EIO:-ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified && t.fake->settings==1);
        Test q;q.fake->completion=status;assert(q.read().error && !q.fake->settings);
    }
    Test b;b.fake->busyBefore=true;assert(b.read().error==-ETIMEDOUT && !b.fake->commands);
}
void faultsAndCancellation() {
    Test baseline;assert(baseline.set().verified);const auto total=baseline.fake->calls;
    for(unsigned at:{0u,1u,total/3,total/2,total-1}) {
        Test t;t.fake->failAt=int(at);const auto r=t.set();assert(r.error==-EACCES && !r.verified && t.fake->calls==at+1);
        Test c;std::atomic<bool> cancelled{at==0};
        c.fake->afterRequest=[&]{if(c.fake->calls==at)cancelled.store(true);};
        const auto v=c.set(5,0x3f800000,4,&cancelled);assert(v.error==-ECANCELED && !v.verified && c.fake->calls==at);
    }
    Test timeout;timeout.fake->busyBefore=true;assert(timeout.set(5,0x3f800000,4,nullptr,1).error==-ETIMEDOUT);
}
void closedTransport() {
    for(unsigned command=0;command<256;++command) {
        if(command==2 || command==0x6d || command==0x6f || command==0x5b)continue;
        Test t;assert(t.service.transaction([&](HardwareSession &s){return detail::exchangeGraniteScalar(s,command).error;})==-EINVAL);
        assert(!t.fake->calls && !allowedSmuMessage(SmuProfile::GraniteRidge,command));
    }
    for(unsigned argument:{0u,99u,101u,1001u,UINT32_MAX}) {
        Test t;assert(t.service.transaction([&](HardwareSession &s){return detail::exchangeGraniteScalar(s,0x5b,argument).error;})==-EINVAL && !t.fake->calls);
    }
}
}
int main() {
    guards();reads();writes();clamping();stale();errors();faultsAndCancellation();closedTransport();
    std::cout<<"8 Granite scalar scenario groups passed\n";
}
