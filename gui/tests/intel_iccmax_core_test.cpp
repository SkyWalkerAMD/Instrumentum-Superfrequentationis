// SPDX-License-Identifier: GPL-2.0-only
#include "intel_iccmax.h"
#include "intel_iccmax_fixture.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelIccMaxFixture *device;
    HardwareService service;
    Test() : device(new IntelIccMaxFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelIccMaxSnapshot read() {
        IntelIccMaxSnapshot out;
        const int error=service.transaction([&](HardwareSession &s) { out=readIntelIccMax(s,130); return out.error; });
        assert(error==out.error); return out;
    }
    IntelIccMaxUpdate apply(const IntelIccMaxSnapshot &old, double amps=125.25) {
        IntelIccMaxUpdate out;
        const int error=service.transaction([&](HardwareSession &s) { out=applyIntelIccMax(s,old,amps); return out.error; });
        assert(error==out.error); return out;
    }
};
void encoding() {
    for (unsigned code=0;code<2048;++code) for (auto high : {0u,0xfffff800u,0x25c32000u}) {
        assert(intelIccMaxAmps(high|code)==code/4.0);
        assert(intelIccMaxModeBit31(high|code)==bool(high & 0x80000000u));
        std::uint32_t result=123;
        assert(encodeIntelIccMax(code/4.0,high,result)==(code<4?-ERANGE:0));
        assert(result==(code<4?123:(high|code)));
    }
    for (double bad : {-1.0,0.0,0.75,1.01,100.1,511.76,512.0,std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        Test t; const auto old=t.read(); const auto calls=t.device->calls;
        assert(t.apply(old,bad).error==-ERANGE && t.device->calls==calls);
    }
}
void profileGates() {
    for (unsigned model=0;model<256;++model) for (unsigned step=0;step<16;++step) {
        CpuIdentity id; id.intel=true; id.family=6; id.model=model; id.stepping=step;
        const bool expected=(model==0x97 && (step<=2 || step==4 || step==5)) || (model==0x9a && step>=1 && step<=4);
        assert(hasIntelIccMaxProfile(id)==expected);
        if (expected || model==0xb7 || model==0x97 || model==0x9a) {
            Test t; t.device->model=model; t.device->stepping=step; const auto result=t.read();
            assert(result.valid==expected && result.error==(expected?0:-ENOTSUP));
            assert(expected || t.device->requests.empty());
        }
    }
    for (unsigned mode=0;mode<4;++mode) {
        Test t;
        if (mode==0) t.device->amd=true;
        if (mode==1) t.device->hypervisor=true;
        if (mode==2) t.device->msr=false;
        if (mode==3) t.device->maximum=0;
        assert(t.read().error==-ENOTSUP && t.device->requests.empty());
    }
}
void railRouting() {
    for (unsigned address=0;address<16;++address) {
        Test t; t.device->topology=(t.device->topology & ~0xf00u)|(address<<8);
        const auto old=t.read(); assert(old.valid && old.address==address && !t.device->wrongCpu);
        assert((t.device->commands==std::vector<unsigned>{4,0x16,4}));
        const auto update=t.apply(old); assert(update.verified && t.device->settingsAttempts()==1 && !t.device->wrongCpu);
        for (const auto &r:t.device->requests) assert(r.address==0x150 || (r.address==0x194 && !r.write));
    }
}
void readOnlyStates() {
    for (unsigned mode=0;mode<7;++mode) {
        Test t;
        if (mode==0) t.device->locked=true;
        if (mode==1) t.device->data|=0x80000000u;
        if (mode>=2 && mode<=5) t.device->data=(t.device->data&~0x7ffu)|(mode-2);
        if (mode==6) t.device->topology|=0x1000u;
        const auto old=t.read(); const auto calls=t.device->calls;
        assert(!intelIccMaxEditable(old));
        if (mode==6) assert(old.error==-ENOTSUP && !old.valid && t.device->commands==std::vector<unsigned>{4});
        else assert(old.valid && !old.error);
        assert(t.apply(old).error==(mode==6?-EINVAL:-EPERM));
        assert(t.device->calls==calls && !t.device->settingsAttempts());
    }
}
void invalidSnapshots() {
    Test t; const auto old=t.read(); const auto calls=t.device->calls;
    for (unsigned mode=0;mode<12;++mode) {
        auto bad=old;
        if (mode==0) bad.error=-EIO;
        if (mode==1) bad.valid=false;
        if (mode==2) bad.identity.model=0xb7;
        if (mode==3) bad.address=16;
        auto &response=mode<8?bad.topology:bad.response;
        if (mode==4 || mode==8) response.commandAttempted=false;
        if (mode==5 || mode==9) response.completed=false;
        if (mode==6 || mode==10) response.error=-EIO;
        if (mode==7 || mode==11) response.firmwareStatus=255;
        assert(t.apply(bad).error==-EINVAL && t.device->calls==calls);
    }
}
void readContextChanges() {
    for (unsigned bit=0;bit<32;++bit) {
        Test t; t.device->afterRequest=[&] { const auto &q=t.device->requests.back();
            if (q.write && ((q.value>>32)&255)==0x16) t.device->topology^=1u<<bit; };
        const auto out=t.read(); assert(!out.valid && out.error && !t.device->settingsAttempts());
    }
    for (unsigned mode=0;mode<5;++mode) {
        Test t; t.device->afterRequest=[&] { const auto &q=t.device->requests.back();
            if (!q.write || ((q.value>>32)&255)!=0x16) return;
            if (mode==0) t.device->stepping=4;
        if (mode==1) t.device->hypervisor=true;
            if (mode==2) t.device->msr=false;
        if (mode==3) t.device->locked=true;
            if (mode==4) t.device->flexExtra^=1;
        };
        assert(!t.read().valid && !t.device->settingsAttempts());
    }
}
void staleContexts() {
    for (unsigned mode=0;mode<3;++mode) for (unsigned bit=0;bit<(mode==2?64u:32u);++bit) {
        Test t; const auto old=t.read();
        if (mode==0) t.device->topology^=1u<<bit;
        if (mode==1) t.device->data^=1u<<bit;
        if (mode==2) t.device->flexExtra^=UINT64_C(1)<<bit;
        const auto out=t.apply(old); assert(out.error && !out.writeAttempted && !t.device->settingsAttempts());
    }
}
void updatesAndReadback() {
    for (unsigned code=4;code<2048;++code) {
        Test t; const auto old=t.read(); const auto out=t.apply(old,code/4.0);
        assert(out.verified && !out.error && out.expectedValid);
        assert(out.expected==((old.response.data&~0x7ffu)|code) && out.after.response.data==out.expected);
        assert(out.unchanged==(code==400) && out.writeAttempted==(code!=400));
        assert(t.device->settingsAttempts()==unsigned(code!=400));
    }
    for (unsigned mode=0;mode<3;++mode) for (unsigned bit=0;bit<(mode==2?64u:32u);++bit) {
        Test t; const auto old=t.read();
        t.device->afterRequest=[&] { const auto &q=t.device->requests.back();
            if (!q.write || ((q.value>>32)&255)!=0x17) return;
            if (mode==0) t.device->data^=1u<<bit;
            if (mode==1) t.device->topology^=1u<<bit;
            if (mode==2) t.device->flexExtra^=UINT64_C(1)<<bit;
        };
        const auto out=t.apply(old); assert(out.error && !out.verified && out.writeAttempted && t.device->settingsAttempts()==1);
    }
    Test t; const auto old=t.read(); t.device->discardChange=true;
    assert(t.apply(old).error==-EIO && t.device->settingsAttempts()==1);
}
void transportFailures() {
    Test success; assert(success.read().valid); const auto readCalls=success.device->calls;
    for (unsigned i=0;i<readCalls;++i) {
        Test t; t.device->failAt=int(i); const auto out=t.read();
        assert(out.error==-EACCES && !out.valid && !t.device->settingsAttempts());
    }
    Test sample; const auto old=sample.read(); sample.device->clearTrace(); assert(sample.apply(old).verified);
    for (unsigned i=0;i<sample.device->calls;++i) {
        Test t; const auto before=t.read(); t.device->clearTrace(); t.device->failAt=int(i);
        const auto out=t.apply(before); assert(out.error==-EACCES && !out.verified && t.device->settingsAttempts()<=1);
        assert(out.writeAttempted==(t.device->settingsAttempts()!=0));
    }
}
void firmwareFailures() {
    for (unsigned command : {4u,0x16u,0x17u}) for (unsigned status=1;status<=255;++status) {
        Test t; const auto old=t.read(); t.device->failCommand=command; t.device->status=status;
        const auto out=t.apply(old); assert(out.error==-EIO && !out.verified);
        assert(out.writeAttempted==(command==0x17));
        assert((command==0x17?out.response.firmwareStatus:0)==(command==0x17?status:0));
    }
}
void boundedBusy() {
    Test before; before.device->busyBefore=true;
    assert(before.read().error==-ETIMEDOUT && before.device->commands.empty());
    for (unsigned command : {4u,0x16u,0x17u}) {
        Test t; const auto old=t.read(); t.device->busyCommand=command;
        const auto out=t.apply(old); assert(out.error==-ETIMEDOUT && !out.verified && out.writeAttempted==(command==0x17));
        assert(t.device->settingsAttempts()<=1);
    }
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterWrite : {false,true}) {
        Test t; const auto old=t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest=[&] { const auto &q=t.device->requests.back();
            if (!q.write || ((q.value>>32)&255)!=(afterWrite?0x17u:4u)) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled.store(true);
        };
        IntelIccMaxUpdate out;
        const auto error=t.service.transaction([&](HardwareSession &s) { out=applyIntelIccMax(s,old,125.25); return out.error; },timeout?10:1000,&cancelled);
        assert(error==(timeout?-ETIMEDOUT:-ECANCELED) && !out.verified && out.writeAttempted==afterWrite);
        assert(t.device->settingsAttempts()==unsigned(afterWrite));
    }
}
}
int main() {
    encoding(); profileGates(); railRouting(); readOnlyStates(); invalidSnapshots(); readContextChanges();
    staleContexts(); updatesAndReadback(); transportFailures(); firmwareFailures(); boundedBusy(); cancellationAndDeadline();
    std::cout << "12 Intel ICCmax scenario groups passed\n";
}
