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
    Test() { target.cpu=130; target.profile=SmuProfile::Shimada; fake->allowPowerAdjustment=true; }
    AmdPowerAdjustment read(const std::atomic<bool> *cancel=nullptr) {
        AmdPowerAdjustment out;
        const int e=service.transaction([&](HardwareSession &s) { out=readShimadaPowerAdjustment(s,target); return out.error; },10000,cancel);
        if (e) out.error=e;
        return out;
    }
    AmdPowerAdjustmentUpdate set(int counts=-10, std::uint32_t raw=0, std::uint32_t caps=4,
                                 const std::atomic<bool> *cancel=nullptr) {
        AmdPowerAdjustmentUpdate out;
        const int e=service.transaction([&](HardwareSession &s) { out=setShimadaPowerAdjustment(s,target,caps,raw,counts); return out.error; },10000,cancel);
        if (e) out.error=e;
        return out;
    }
};
void signedWords() {
    for (int count=-128; count<=127; ++count) {
        int decoded=999;
        assert(decodeAmdPowerAdjustment(std::uint32_t(count),decoded) && decoded==count);
    }
    for (auto raw : {128u,255u,256u,0x80000000u,0xffffff7fu}) {
        int count=999; assert(!decodeAmdPowerAdjustment(raw,count) && count==0);
        Test t; t.fake->powerAdjustmentRaw=raw; const auto r=t.read();
        assert(r.error==-ERANGE && r.rawValid && r.raw==raw && !r.valid && !t.fake->settings);
    }
    for (int count : {-128,-10,-1,0,5,127}) {
        Test t; t.fake->powerAdjustmentRaw=std::uint32_t(count); const auto r=t.read();
        assert(!r.error && r.valid && r.counts==count && r.firmwareVersion==0x00732e00 && !t.fake->settings && !t.fake->wrongCpu);
        assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0xa8}));
        for (const auto &args : t.fake->commandArgs) for (auto arg : args) assert(!arg);
    }
}
void versionAndIdentity() {
    for (auto version : {0u,0x00732d00u,0x00732e01u,UINT32_MAX}) {
        Test t; t.fake->firmwareVersion=version; const auto r=t.read();
        assert(r.error==-ENOTSUP && r.firmwareValid && r.firmwareVersion==version && !r.messageAttempted && !r.rawValid);
        assert(t.fake->messages==std::vector<std::uint32_t>{2});
    }
    for (unsigned mode=0; mode<8; ++mode) {
        Test t;
        if (mode==0) t.target.profile=SmuProfile::Phoenix;
        if (mode==1) t.target.bus=1;
        if (mode==2) t.target.device=1;
        if (mode==3) t.target.function=1;
        if (mode==4) t.fake->amd=false;
        if (mode==5) t.fake->family=0x19;
        if (mode==6) t.fake->model=0x18;
        if (mode==7) t.fake->pciId^=0x10000;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    Test t; t.fake->afterRequest=[&] { if(t.fake->argumentReads==3) t.fake->model=0x18; };
    const auto r=t.read(); assert(r.error==-ENOTSUP && !r.rawValid && !r.valid);
}
void boundsAndComparisons() {
    for (int count : {-2147483647-1,-11,6,2147483647}) {
        Test t; assert(t.set(count).error==-ERANGE && !t.fake->calls);
    }
    Test raw; assert(raw.set(-1,0xff).error==-ERANGE && !raw.fake->calls);
    for (auto caps : {0u,2u,0x10004u,UINT32_MAX}) {
        Test t; assert(t.set(-1,0,caps).error==-ENOTSUP && !t.fake->calls);
    }
    Test changedRaw; changedRaw.fake->powerAdjustmentRaw=1;
    assert(changedRaw.set().error==-EAGAIN && !changedRaw.fake->settings);
    Test changedCaps; changedCaps.fake->capsRaw=6;
    assert(changedCaps.set().error==-EAGAIN && !changedCaps.fake->settings);
    Test race; race.fake->afterRequest=[&] { if(race.fake->argumentReads>=3) race.fake->capsRaw=6; };
    assert(race.set().error==-EAGAIN && !race.fake->settings);
}
void appliedAndUnchanged() {
    for (int count : {-10,-1,0,5}) {
        Test t; const auto r=t.set(count);
        assert(!r.error && r.verified && r.after.valid && r.after.counts==count && r.argument==std::uint32_t(count));
        assert(r.unchanged==(count==0) && t.fake->settings==(count==0?0u:1u) && !t.fake->wrongCpu);
        if(count) {
            assert(r.accepted && r.writeAttempted && r.phase==AmdPboPhase::Complete);
            assert((t.fake->messages==std::vector<std::uint32_t>{2,0x6f,0xa8,0x6f,0xa7,2,0x6f,0xa8}));
            assert(t.fake->commandArgs[4][0]==std::uint32_t(count));
            for(unsigned i=1;i<6;++i) assert(!t.fake->commandArgs[4][i]);
        }
    }
    Test drop; drop.fake->discardChange=true; const auto r=drop.set();
    assert(r.error==-EIO && r.accepted && !r.verified && r.after.valid && r.after.counts==0 && drop.fake->settings==1);
}
void repliesAndAfterWriteFailures() {
    for(auto response : {0u,0xfdu,0xfeu,0xffu}) {
        Test t; t.fake->writeCompletion=response; const auto r=t.set();
        assert(r.error==(response?-EIO:-ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified && t.fake->settings==1);
    }
    for(unsigned mode=0;mode<4;++mode) {
        Test t; std::atomic<bool> cancel{false};
        t.fake->afterRequest=[&] {
            if(!t.fake->settings) return;
            if(mode==0) t.fake->firmwareVersion=0x00732e01;
            if(mode==1) t.fake->capsRaw=6;
            if(mode==2) t.fake->failAt=int(t.fake->calls);
            if(mode==3) cancel.store(true);
        };
        const auto r=t.set(-10,0,4,&cancel);
        assert(r.error && r.writeAttempted && !r.verified && t.fake->settings==1);
    }
}
void readFaultsAndCancellation() {
    Test base; assert(base.read().valid);
    for(unsigned pos=0;pos<base.fake->calls;++pos) {
        Test t; t.fake->failAt=int(pos); const auto r=t.read();
        assert(r.error==-EACCES && !r.valid && !r.rawValid && !r.raw && !t.fake->settings && t.fake->calls==pos+1);
    }
    for(unsigned pos : {0u,1u,base.fake->calls/2,base.fake->calls}) {
        Test t; std::atomic<bool> cancel{pos==0};
        t.fake->afterRequest=[&] { if(t.fake->calls==pos) cancel.store(true); };
        const auto r=t.read(&cancel); assert(r.error==-ECANCELED && !r.valid && !r.rawValid && !t.fake->settings);
    }
    Test busy; busy.fake->busyBefore=true; assert(busy.read().error==-ETIMEDOUT && !busy.fake->commands);
}
void closedTransport() {
    for(auto command : {2u,0xa8u}) for(unsigned mode=0;mode<3;++mode) {
        Test t; assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeShimadaRsmu(s,command,mode==0?1:0,mode!=1,mode!=2).error;
        },1000)==-EINVAL && !t.fake->calls);
    }
    for(auto word : {6u,0x80u,0xffu,0xfffffff5u}) {
        Test t; assert(t.service.transaction([&](HardwareSession &s) {
            return detail::exchangeShimadaRsmu(s,0xa7,word,false,true).error;
        },1000)==-EINVAL && !t.fake->calls);
    }
    Test t; assert(t.service.transaction([&](HardwareSession &s) {
        return detail::exchangeShimadaRsmu(s,0xa7,0,true,true).error;
    },1000)==-EINVAL && !t.fake->calls);
}
}
int main() {
    signedWords(); versionAndIdentity(); boundsAndComparisons(); appliedAndUnchanged();
    repliesAndAfterWriteFailures(); readFaultsAndCancellation(); closedTransport();
    std::cout << "7 AMD power adjustment scenario groups passed\n";
}
