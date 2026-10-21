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
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; fake->capsRaw = 6; fake->allowManualOc = true; }
    AmdOcModeUpdate mode(bool enabled = true, std::uint32_t expected = 6,
                         const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdOcModeUpdate out;
        const int e = service.transaction([&](HardwareSession &s) { out = setShimadaOcMode(s,target,expected,enabled); return out.error; },timeout,cancel);
        if(e) out.error=e;
        return out;
    }
    AmdOcFrequencyUpdate frequency(AmdOcFrequencyScope scope = AmdOcFrequencyScope::Core,
                                   unsigned ccd = 3, unsigned core = 7, unsigned mhz = 5000,
                                   std::uint32_t expected = 7, const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdOcFrequencyUpdate out;
        const int e = service.transaction([&](HardwareSession &s) { out = setShimadaOcFrequency(s,target,expected,scope,ccd,core,mhz); return out.error; },timeout,cancel);
        if(e) out.error=e;
        return out;
    }
};
void encoding() {
    for(unsigned mhz=1;mhz<=0xfffff;++mhz) {
        std::uint32_t cmd=99,arg=99;
        assert(!encodeShimadaOcFrequency(AmdOcFrequencyScope::AllCores,0,0,mhz,cmd,arg) && cmd==0x5f && arg==mhz);
    }
    for(unsigned ccd=0;ccd<16;++ccd) for(unsigned core=0;core<8;++core)
        for(unsigned mhz : {1u,255u,256u,5000u,65535u,65536u,0xfffffu}) {
            std::uint32_t cmd=0,arg=0;
            assert(!encodeShimadaOcFrequency(AmdOcFrequencyScope::Core,ccd,core,mhz,cmd,arg));
            assert(cmd==0x60 && arg==((ccd<<28)|(core<<20)|mhz));
        }
    for(unsigned mode=0;mode<7;++mode) {
        Test t;
        const auto r=t.frequency(mode==6?AmdOcFrequencyScope(99):mode==5?AmdOcFrequencyScope::AllCores:AmdOcFrequencyScope::Core,
            mode==0?16:3,mode==1?8:7,mode==2?0:mode==3?0x100000:mode==4?UINT32_MAX:5000);
        assert(r.error && !r.command && !r.argument && !t.fake->calls);
    }
}
void identities() {
    for(unsigned mode=0;mode<8;++mode) for(bool frequency : {false,true}) {
        Test t;t.fake->capsRaw=7;
        if(mode==0)t.target.profile=SmuProfile::Phoenix;
        if(mode==1)t.target.bus=1;
        if(mode==2)t.target.device=1;
        if(mode==3)t.target.function=1;
        if(mode==4)t.fake->amd=false;
        if(mode==5)t.fake->model=0x18;
        if(mode==6)t.fake->family=0x19;
        if(mode==7)t.fake->pciId^=0x10000;
        const int e=frequency?t.frequency().error:t.mode(false,7).error;
        assert(e && !t.fake->commands && !t.fake->settings);
    }
}
void modeChanges() {
    for(bool enabled : {false,true}) for(std::uint32_t extra : {0u,2u,6u,0x80000006u}) {
        Test t;const auto before=extra|unsigned(!enabled);t.fake->capsRaw=before;
        const auto r=t.mode(enabled,before);
        assert(!r.error && r.verified && r.accepted && r.writeAttempted && !r.unchanged);
        assert(r.before.raw==before && bool(r.after.raw&1u)==enabled && r.phase==AmdPboPhase::Complete);
        assert((t.fake->messages==std::vector<std::uint32_t>{0x6f,enabled?0x5du:0x5eu,0x6f}));
        assert(t.fake->settings==1 && t.fake->scalarRaw==0x3f800000 && !t.fake->wrongCpu);
        for(const auto &args:t.fake->commandArgs)for(auto v:args)assert(!v);
    }
    Test changed;changed.fake->overrideModeCaps=true;changed.fake->modeCapsAfter=0x80000001;
    const auto r=changed.mode();assert(r.verified && r.after.raw==0x80000001 && r.before.raw==6);
}
void unchanged() {
    for(bool enabled : {false,true}) {
        Test t;t.fake->capsRaw=6u|unsigned(enabled);
        const auto r=t.mode(enabled,t.fake->capsRaw);
        assert(r.verified && r.unchanged && !r.accepted && !r.writeAttempted && !t.fake->settings);
        assert((t.fake->messages==std::vector<std::uint32_t>{0x6f}));
    }
}
void staleContexts() {
    for(bool frequency : {false,true}) for(std::uint32_t caps : {0u,1u,2u,4u,0x80000007u}) {
        Test t;t.fake->capsRaw=caps;
        const int e=frequency?t.frequency().error:t.mode(false,7).error;
        assert(e==-EAGAIN && !t.fake->settings && t.fake->commands==1);
    }
    for(bool frequency : {false,true}) {
        Test t;t.fake->capsRaw=frequency?7:6;
        t.fake->afterRequest=[&] {if(t.fake->argumentReads)t.fake->model=0x18;};
        const int e=frequency?t.frequency().error:t.mode().error;
        assert(e==-ENOTSUP && !t.fake->settings);
    }
}
void frequencyAcknowledgement() {
    for(bool all : {false,true})for(bool drop : {false,true}) {
        Test t;t.fake->capsRaw=0x80000007;t.fake->discardChange=drop;
        const auto r=t.frequency(all?AmdOcFrequencyScope::AllCores:AmdOcFrequencyScope::Core,all?0:15,all?0:7,5000,0x80000007);
        assert(!r.error && r.accepted && !r.verified && r.writeAttempted && r.requestedMHz==5000);
        assert((t.fake->messages==std::vector<std::uint32_t>{0x6f,all?0x5fu:0x60u}));
        assert(r.argument==(all?5000u:0xf0701388u) && t.fake->commandArgs.back()[0]==r.argument);
        assert(t.fake->capsRaw==0x80000007 && t.fake->scalarRaw==0x3f800000 && t.fake->settings==1);
        assert(t.fake->ocAllMHz==((all && !drop)?5000u:0u));
        assert(t.fake->ocCoreArgument==((!all && !drop)?0xf0701388u:0u));
        for(unsigned i=1;i<6;++i)assert(!t.fake->commandArgs.back()[i]);
    }
    for(std::uint32_t caps : {0u,2u,4u,6u,0xfffffffeu}) {
        Test t;assert(t.frequency(AmdOcFrequencyScope::Core,0,0,5000,caps).error==-ENOTSUP && !t.fake->calls);
    }
}
void modeReadbackFailure() {
    Test drop;drop.fake->discardChange=true;const auto r=drop.mode();
    assert(r.error==-EIO && r.accepted && !r.verified && r.after.valid && r.phase==AmdPboPhase::Verify);
    for(unsigned mode=0;mode<2;++mode) {
        Test t;t.fake->afterRequest=[&] {if(t.fake->settings){if(mode==0)t.fake->completion=0xfe;else t.fake->model=0x18;}};
        const auto failed=t.mode();assert(failed.error && failed.accepted && !failed.verified && t.fake->settings==1);
        assert(t.fake->commands==(mode==0?3u:2u));
    }
}
void firmwareRejections() {
    for(bool frequency : {false,true})for(std::uint32_t response:{0u,2u,0xfcu,0xfdu,0xfeu,0xffu,UINT32_MAX}) {
        Test t;t.fake->capsRaw=frequency?7:6;t.fake->writeCompletion=response;
        const int e=frequency?t.frequency().error:t.mode().error;
        assert(e==(response?-EIO:-ETIMEDOUT) && t.fake->settings==1 && t.fake->commands==2);
    }
    Test query;query.fake->completion=0xfd;assert(query.mode().error==-EIO && !query.fake->settings);
    Test busy;busy.fake->busyBefore=true;assert(busy.mode().error==-ETIMEDOUT && !busy.fake->commands);
}
void faultsAndCancellation() {
    for(bool frequency:{false,true}) {
        Test base;base.fake->capsRaw=frequency?7:6;assert(!(frequency?base.frequency().error:base.mode().error));
        for(unsigned n=0;n<base.fake->calls;++n) {
            Test t;t.fake->capsRaw=frequency?7:6;t.fake->failAt=int(n);
            const int e=frequency?t.frequency().error:t.mode().error;
            assert(e==-EACCES && t.fake->calls==n+1 && t.fake->settings<=1);
        }
        for(unsigned n=0;n<=base.fake->calls;++n) {
            Test t;t.fake->capsRaw=frequency?7:6;std::atomic<bool> cancel{n==0};
            t.fake->afterRequest=[&] {if(t.fake->calls==n)cancel.store(true);};
            const int e=frequency?t.frequency(AmdOcFrequencyScope::Core,3,7,5000,7,&cancel).error:t.mode(true,6,&cancel).error;
            assert(e==-ECANCELED && t.fake->calls==n && t.fake->settings<=1);
        }
    }
    Test deadline;deadline.fake->busyBefore=true;
    assert(deadline.mode(true,6,nullptr,1).error==-ETIMEDOUT && !deadline.fake->settings);
}
void transportAllowlist() {
    for(std::uint32_t command:{0x53u,0x61u,0x92u,0xa8u,0x26u,0x27u}) {
        Test t;
        assert(t.service.transaction([&](HardwareSession &s){return detail::exchangeShimadaRsmu(s,command,0,false,true).error;})==-EINVAL);
        assert(!t.fake->calls);
    }
    for(std::uint32_t command:{0x5du,0x5eu,0x5fu,0x60u}) {
        Test t;
        assert(t.service.transaction([&](HardwareSession &s){return detail::exchangeShimadaRsmu(s,command,0,false,false).error;})==-EINVAL);
        assert(!t.fake->calls);
    }
}
}
int main() {
    encoding();identities();modeChanges();unchanged();staleContexts();frequencyAcknowledgement();
    modeReadbackFailure();firmwareRejections();faultsAndCancellation();transportAllowlist();
    std::cout << "10 AMD manual OC scenario groups passed\n";
}
