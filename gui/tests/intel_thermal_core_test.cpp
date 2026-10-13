// SPDX-License-Identifier: GPL-2.0-only
#include "intel_thermal.h"
#include "intel_controls.h"
#include "intel_thermal_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelThermalFixture *device;
    HardwareService service;
    explicit Test(IntelThermalFixture *backend=new IntelThermalFixture):device(backend),service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelThermalSnapshot read() {
        IntelThermalSnapshot out;
        const int error=service.transaction([&](HardwareSession &s) { out=readIntelThermal(s,130); return out.error; });
        assert(error==out.error); return out;
    }
    IntelThermalUpdate apply(const IntelThermalSnapshot &old,unsigned degrees=7) {
        IntelThermalUpdate out;
        const int error=service.transaction([&](HardwareSession &s) { out=applyIntelThermal(s,old,degrees); return out.error; });
        assert(error==out.error); return out;
    }
};
void valuesAndPreservation() {
    const auto mask=UINT64_C(0x3f)<<24;
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned degrees=0;degrees<=63;++degrees) {
        Test t; t.device->model=model; const auto old=t.read(); assert(intelThermalEditable(old));
        const auto out=t.apply(old,degrees); const auto expected=(old.raw & ~mask)|(std::uint64_t(degrees)<<24);
        assert(!out.error && out.verified && out.expectedValid && out.expected==expected && out.after.raw==expected);
        assert(intelDefaultTccTemperature(expected)==100 && intelTccOffset(expected)==degrees && intelTccTemperature(expected)==100-int(degrees));
        assert(t.device->writes==unsigned(old.raw!=expected) && out.unchanged==(old.raw==expected));
        assert(out.writeAttempted==bool(t.device->writes) && !t.device->wrongCpu);
        for (const auto &r:t.device->requests) assert(r.address==0xce || r.address==0x1a2);
    }
    for (unsigned bit=0;bit<64;++bit) {
        const auto old=(UINT64_C(100)<<16)|(UINT64_C(1)<<bit); std::uint64_t encoded=0;
        assert(!encodeIntelTccOffset(7,old,encoded) && (encoded & ~mask)==(old & ~mask));
    }
    for (unsigned bad : {64u,UINT32_MAX}) {
        Test t; const auto old=t.read(); const auto calls=t.device->cpuCalls;
        assert(t.apply(old,bad).error==-ERANGE && t.device->cpuCalls==calls);
    }
    for (unsigned tjmax : {0u,1u,30u,63u}) {
        std::uint64_t encoded=123; assert(encodeIntelTccOffset(tjmax,UINT64_C(1)*tjmax<<16,encoded)==-ERANGE && encoded==123);
    }
}
void gatesAndInvalidSnapshots() {
    for (unsigned model : {0xad,0xa5,0xbf,0x97,0}) {
        Test t; t.device->model=model; assert(t.read().error==-ENOTSUP && t.device->requests.empty());
    }
    for (unsigned mode=0;mode<6;++mode) {
        Test t;
        if (mode==0) t.device->amd=true;
        if (mode==1) t.device->hasMsr=false;
        if (mode==2) t.device->hypervisor=true;
        if (mode==3) t.device->dts=false;
        if (mode==4) t.device->maximumLeaf=1;
        if (mode==5) t.device->inconsistentSignature=true;
        assert(t.read().error && t.device->requests.empty());
    }
    for (unsigned mode=0;mode<3;++mode) {
        Test t;
        if (mode==0) t.device->regs[0x1a2]|=UINT64_C(1)<<31;
        if (mode==1) t.device->regs[0xce]&=~(UINT64_C(1)<<30);
        if (mode==2) t.device->regs[0x1a2]=UINT64_C(1)<<16 | UINT64_C(5)<<24;
        const auto old=t.read(); const auto calls=t.device->cpuCalls;
        assert(!old.error && old.valid && !intelThermalEditable(old));
        assert(t.apply(old,0).error==-EPERM && t.device->cpuCalls==calls);
    }
    Test t; const auto old=t.read(); const auto calls=t.device->cpuCalls;
    for (unsigned mode=0;mode<5;++mode) {
        auto bad=old;
        if (mode==0) bad.error=-EIO;
        if (mode==1) bad.valid=false;
        if (mode==2) bad.identity.model=0xad;
        if (mode==3) bad.identity.error=-EIO;
        if (mode==4) bad.raw &= ~(UINT64_C(255)<<16);
        assert(t.apply(bad).error==-EINVAL && t.device->cpuCalls==calls);
    }
    t.device->regs[0x1a2]&=~(UINT64_C(255)<<16); const auto invalid=t.read();
    assert(invalid.error==-ENODATA && !invalid.valid);
}
void oldValuesAndReadback() {
    for (unsigned bit=0;bit<64;++bit) for (auto address : {0x1a2u,0xceu}) {
        Test t; const auto old=t.read(); t.device->regs[address] ^= UINT64_C(1)<<bit;
        assert(t.apply(old).error && !t.device->writes);
        Test r; const auto before=r.read();
        r.device->afterRequest=[&](const Request &q) { if (q.write) r.device->regs[address]^=UINT64_C(1)<<bit; };
        const auto out=r.apply(before); assert(out.error && !out.verified && out.writeAttempted && r.device->writes==1);
    }
    Test t; const auto old=t.read(); t.device->discardChange=true;
    assert(t.apply(old).error==-EIO && t.device->writes==1);
}
void contextChanges() {
    for (bool afterWrite : {false,true}) for (unsigned mode=0;mode<5;++mode) {
        Test t; const auto old=t.read();
        auto change=[&] {
            if (mode==0) t.device->model=0x8f;
            if (mode==1) t.device->hasMsr=false;
            if (mode==2) t.device->hypervisor=true;
            if (mode==3) t.device->dts=false;
            if (mode==4) t.device->stepping=1;
        };
        if (afterWrite) t.device->afterRequest=[&](const Request &r) { if (r.write) change(); }; else change();
        const auto out=t.apply(old); assert(out.error && !out.verified && out.writeAttempted==afterWrite);
    }
    // Change the word after the preflight read; the register transaction must
    // compare it again immediately before the write.
    Test t; const auto old=t.read();
    t.device->afterRequest=[&](const Request &r) { if (r.address==0x1a2) t.device->regs[0x1a2]^=UINT64_C(1)<<63; };
    assert(t.apply(old).error==-EAGAIN && !t.device->writes);
}
void transportErrors() {
    Test baseline; const auto old=baseline.read(); const auto preCpu=baseline.device->cpuCalls,preRead=baseline.device->reads;
    assert(baseline.apply(old).verified);
    const auto totalCpu=baseline.device->cpuCalls-preCpu,totalRead=baseline.device->reads-preRead;
    for (unsigned i=0;i<preCpu;++i) { Test t; t.device->failCpuid=int(i); assert(t.read().error==-EACCES); }
    for (unsigned i=0;i<preRead;++i) { Test t; t.device->failRead=int(i); assert(t.read().error==-EACCES); }
    for (unsigned i=0;i<totalCpu;++i) {
        Test t; const auto before=t.read(); t.device->failCpuid=int(t.device->cpuCalls+i);
        const auto out=t.apply(before); assert(out.error==-EACCES && !out.verified && t.device->writes<=1);
    }
    for (unsigned i=0;i<totalRead;++i) {
        Test t; const auto before=t.read(); t.device->failRead=int(t.device->reads+i);
        const auto out=t.apply(before); assert(out.error==-EACCES && !out.verified && t.device->writes<=1);
    }
    Test t; const auto before=t.read(); t.device->failWrite=0;
    const auto out=t.apply(before); assert(out.error==-EIO && !out.verified && out.writeAttempted && t.device->writes==1);
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterWrite : {false,true}) {
        Test t; const auto old=t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest=[&](const Request &r) {
            if (afterWrite?!r.write:r.address!=0xce) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled=true;
        };
        IntelThermalUpdate out;
        const int error=t.service.transaction([&](HardwareSession &s) { out=applyIntelThermal(s,old,7); return out.error; },timeout?10:1000,&cancelled);
        assert(error==(timeout?-ETIMEDOUT:-ECANCELED) && !out.verified && out.writeAttempted==afterWrite);
    }
}
void temperatureSensors() {
    struct Sensors : IntelThermalFixture {
        bool package=true;
        CpuIdReply cpuid(unsigned cpu,std::uint32_t leaf,std::uint32_t subleaf) override {
            auto out=IntelThermalFixture::cpuid(cpu,leaf,subleaf);
            if (!out.error && leaf==6 && package) out.words[0]|=1u<<6;
            return out;
        }
    };
    for (unsigned model : {0xb7,0xba,0x8c,0x8d,0x97,0x9a,0x8f,0xcf,0x55}) for (unsigned delta : {0u,20u,127u,128u,255u}) {
        auto *device=new Sensors; Test t(device); device->model=model;
        device->regs[0x19c]=device->regs[0x1b1]=(UINT64_C(1)<<31)|(std::uint64_t(delta)<<16);
        IntelSnapshot snapshot;
        assert(!t.service.transaction([&](HardwareSession &s) { snapshot=readIntelControls(s,130); return snapshot.error; }));
        unsigned found=0;
        for (const auto &r:snapshot.readings) {
            if (r.msr==0x1a2) { assert(r.decoded && r.value==100 && !r.editable); ++found; }
            if (r.msr==0x19c || r.msr==0x1b1) {
                const unsigned mask=model==0x55 || ((model==0x8f || model==0xcf) && r.msr==0x19c)?127:255;
                assert(r.decoded && !r.editable && r.value==100-int(delta & mask)); ++found;
            }
        }
        assert(found==3 && !device->writes && !device->wrongCpu);
    }
    for (unsigned mode=0;mode<5;++mode) {
        auto *device=new Sensors; Test t(device);
        device->regs[0x19c]=device->regs[0x1b1]=(UINT64_C(1)<<31)|(UINT64_C(20)<<16);
        if (mode==0) device->regs[0x1a2]&=~(UINT64_C(255)<<16);
        if (mode==1) device->regs[0x19c]&=~(UINT64_C(1)<<31);
        if (mode==2) device->regs[0x1b1]&=~(UINT64_C(1)<<31);
        if (mode==3) device->dts=false;
        if (mode==4) device->package=false;
        IntelSnapshot snapshot;
        assert(!t.service.transaction([&](HardwareSession &s) { snapshot=readIntelControls(s,130); return snapshot.error; }));
        unsigned core=0,package=0;
        for (const auto &r:snapshot.readings) {
            if (r.msr==0x19c) { ++core; assert(r.decoded==(mode!=0 && mode!=1)); }
            if (r.msr==0x1b1) { ++package; assert(r.decoded==(mode!=0 && mode!=2)); }
        }
        assert(core==unsigned(mode!=3) && package==unsigned(mode!=4));
    }
}
}
int main() {
    valuesAndPreservation(); gatesAndInvalidSnapshots(); oldValuesAndReadback(); contextChanges(); transportErrors(); cancellationAndDeadline(); temperatureSensors();
    std::cout << "7 Intel thermal scenario groups passed\n";
}
