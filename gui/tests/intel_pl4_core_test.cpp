// SPDX-License-Identifier: GPL-2.0-only
#include "intel_controls.h"
#include "intel_controls_fixture.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelControlsFixture *device;
    HardwareService service;
    Test() : device(new IntelControlsFixture), service(std::unique_ptr<HardwareBackend>(device)) { device->expectedCpu=130; }
    IntelSnapshot read() {
        IntelSnapshot out;
        service.transaction([&](HardwareSession &s) { out=readIntelControls(s,130); return out.error; });
        return out;
    }
    IntelControlUpdate apply(const IntelSnapshot &old,double watts=100) {
        IntelControlUpdate out;
        service.transaction([&](HardwareSession &s) { out=applyIntelControl(s,old,IntelField::Pl4,watts); return out.error; });
        return out;
    }
};
const IntelReading &row(const IntelSnapshot &out) {
    for (const auto &r:out.readings) if (r.field==IntelField::Pl4) return r;
    assert(false); return out.readings[0];
}
void unitsAndPreservation() {
    for (unsigned exponent=0;exponent<16;++exponent) for (unsigned code : {1u,2u,8191u})
        for (std::uint64_t raw : {UINT64_C(0),UINT64_C(0xffffffff7fffffff),UINT64_C(0x8765432140006800)}) {
            Test t; t.device->regs[0x606]=(t.device->regs[0x606]&~UINT64_C(15))|exponent;
            t.device->regs[0x601]=raw; const auto old=t.read();
            assert(!old.error && old.pl4 && row(old).decoded && row(old).editable && row(old).msr==0x601 && row(old).unit=="W");
            assert(row(old).value==std::ldexp(double(raw&0x1fff),-int(exponent)));
            const double watts=std::ldexp(double(code),-int(exponent));
            const auto result=t.apply(old,watts); const auto expected=(raw&~UINT64_C(0x1fff))|code;
            assert(!result.error && result.verified && result.readbackValid && result.msr==0x601 && result.expected==expected);
            assert(result.readbackValue==watts && t.device->regs[0x601]==expected && !t.device->wrongCpu);
            assert(result.writeAttempted==(raw!=expected) && result.unchanged==(raw==expected));
            const unsigned writes=t.device->writes; const auto again=t.apply(result.after,watts);
            assert(again.verified && again.unchanged && !again.writeAttempted && t.device->writes==writes);
            for (const auto &r:t.device->requests) if (r.write) assert(r.address==0x601 && r.value==expected);
        }
    Test t; const auto result=t.apply(t.read(),100.12);
    assert(result.verified && result.readbackValue==100 && (result.expected&0x1fff)==800);
}
void boundsAndGates() {
    Test t; const auto old=t.read();
    for (double invalid : {-1.0,0.0,0.124,1024.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        assert(t.apply(old,invalid).error && !t.device->writes);
    }
    for (unsigned model : {0x55,0x97,0xba,0xbf,0x8f,0xcf,0xff}) {
        Test other; other.device->model=model; const auto snapshot=other.read();
        assert(!snapshot.pl4 && other.apply(snapshot).error && !other.device->writes);
        for (const auto &r:other.device->requests) assert(r.address!=0x601);
    }
    for (unsigned mode=0;mode<4;++mode) {
        Test other;
        if (mode==0) other.device->amd=true;
        if (mode==1) other.device->msr=false;
        if (mode==2) other.device->virtualized=true;
        if (mode==3) other.device->failCpuid=3;
        const auto snapshot=other.read();
        if (snapshot.pl4) assert(!row(snapshot).decoded && !row(snapshot).editable);
        for (const auto &r:other.device->requests) assert(r.address!=0x601);
        assert(other.apply(snapshot).error && !other.device->writes);
    }
    auto invalid=old; invalid.pl4=false; assert(t.apply(invalid).error && !t.device->writes);
    invalid=old; invalid.peakPowerLimit.error=-EIO; assert(t.apply(invalid).error && !t.device->writes);
}
void locksAndStale() {
    for (unsigned bit=0;bit<64;++bit) {
        Test t; const auto old=t.read(); t.device->regs[0x601]^=UINT64_C(1)<<bit;
        assert(t.apply(old).error && !t.device->writes);
        Test units; const auto before=units.read(); units.device->regs[0x606]^=UINT64_C(1)<<bit;
        assert(units.apply(before).error && !units.device->writes);
    }
    Test locked; locked.device->regs[0x601]|=UINT64_C(1)<<31; const auto old=locked.read();
    assert(row(old).decoded && !row(old).editable && locked.apply(old).error==-EPERM && !locked.device->writes);
    Test race; const auto before=race.read();
    race.device->afterRequest=[&](const Request &r) { if (!r.write && r.address==0x601) race.device->regs[0x601]^=UINT64_C(1)<<13; };
    assert(race.apply(before).error==-EAGAIN && !race.device->writes);
}
void fullReadback() {
    for (unsigned bit=0;bit<64;++bit) {
        Test t; const auto old=t.read();
        t.device->afterRequest=[&](const Request &r) { if (r.write) t.device->regs[0x601]^=UINT64_C(1)<<bit; };
        const auto result=t.apply(old);
        assert(result.error==-EIO && !result.verified && result.writeAttempted && t.device->writes==1);
    }
    for (unsigned mode=0;mode<6;++mode) {
        Test t; const auto old=t.read();
        t.device->afterRequest=[&](const Request &r) { if (r.write) {
            if (mode==0) t.device->regs[0x606]^=1;
            if (mode==1) t.device->model=0xba;
            if (mode==2) t.device->msr=false;
            if (mode==3) t.device->virtualized=true;
            if (mode==4) t.device->failRead=int(t.device->reads+9);
            if (mode==5) t.device->regs[0x601]=old.peakPowerLimit.value;
        } };
        const auto result=t.apply(old); assert(result.error && !result.verified && result.writeAttempted && t.device->writes==1);
    }
}
void faults() {
    for (unsigned position : {0u,9u,10u,11u,20u}) {
        Test t; const auto old=t.read(); t.device->failRead=int(t.device->reads+position);
        const auto result=t.apply(old); assert(result.error && !result.verified && t.device->writes<=1);
    }
    for (unsigned position=0;position<8;++position) {
        Test t; const auto old=t.read(); t.device->failCpuid=int(t.device->cpuCalls+position);
        const auto result=t.apply(old); assert(result.error && !result.verified && t.device->writes<=1);
    }
    Test t; const auto old=t.read(); t.device->failWrite=0;
    const auto result=t.apply(old); assert(result.error==-EIO && !result.verified && result.writeAttempted && t.device->writes==1);
    Test noUnit; noUnit.device->failRead=0; const auto failed=noUnit.read();
    assert(!row(failed).decoded && row(failed).error && !row(failed).editable);
    for (const auto &r:noUnit.device->requests) assert(r.address!=0x601);
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterWrite : {false,true}) {
        Test t; const auto old=t.read(); std::atomic<bool> cancelled{false};
        t.device->afterRequest=[&](const Request &r) {
            if (afterWrite ? !r.write : r.write || r.address!=0x601) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled=true;
        };
        IntelControlUpdate result;
        const auto error=t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,old,IntelField::Pl4,100); return result.error; },timeout?10:1000,&cancelled);
        assert(error==(timeout?-ETIMEDOUT:-ECANCELED) && !result.verified && result.writeAttempted==afterWrite);
        assert(t.device->writes==unsigned(afterWrite));
    }
}
}
int main() {
    unitsAndPreservation(); boundsAndGates(); locksAndStale(); fullReadback(); faults(); cancellationAndDeadline();
    std::cout << "6 Intel PL4 scenario groups passed\n";
}
