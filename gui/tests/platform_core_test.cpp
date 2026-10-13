// SPDX-License-Identifier: GPL-2.0-only
#include "intel_controls.h"
#include "amd_smu.h"
#include "amd_pstates.h"
#include "spd.h"
#include "intel_controls_fixture.h"
#include <algorithm>
#include <iterator>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <map>
#include <thread>
#include <future>
#include <iostream>
#include <limits>

using namespace octool::core;
namespace {
using Fake = IntelControlsFixture;
struct TestService {
    Fake *fake;
    HardwareService service;
    TestService() : fake(new Fake), service(std::unique_ptr<HardwareBackend>(fake)) {}
};
void maskedUpdates() {
    TestService t; t.fake->regs[0x100]=0xffff0000; t.fake->regs[0x101]=0x12340000;
    RegisterUpdate a; a.target=msrRequest(3,0x100); a.mask=255; a.bits=0x5a;
    a.expected=0xffff0000; a.compareMask=UINT64_MAX;
    RegisterUpdate b=a; b.target.address=0x101; b.expected=0x12340000;
    UpdateResult result;
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a,b}); return result.error; });
    assert(!result.error && result.completed==2 && result.submitted[0]==0xffff005a && result.submitted[1]==0x1234005a);
    assert(t.fake->requests.size()==4 && !t.fake->requests[0].write && !t.fake->requests[1].write);
    t.fake->requests.clear(); t.fake->writes=0;
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a,a}); return result.error; });
    assert(result.error==-EINVAL && t.fake->requests.empty());
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a}); return result.error; });
    assert(result.error==-EAGAIN && t.fake->writes==0);
    a.expected=t.fake->regs[0x100]; a.lockMask=UINT64_C(1)<<31;
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a}); return result.error; });
    assert(result.error==-EPERM && t.fake->writes==0);
}
void partialFailure() {
    TestService t; RegisterUpdate a; a.target=msrRequest(0,0x100); a.mask=255; a.bits=3;
    RegisterUpdate b=a; b.target.address=0x101;
    UpdateResult result; t.fake->failRead=1;
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a,b}); return result.error; });
    assert(result.error==-EACCES && !result.writeAttempted && t.fake->writes==0);
    t.fake->failRead=-1; t.fake->failWrite=1;
    t.service.transaction([&](HardwareSession &s) { result=updateRegisters(s,{a,b}); return result.error; });
    assert(result.error==-EIO && result.completed==1 && result.failedIndex==1 && result.writeAttempted);
    assert(t.fake->writes==2 && t.fake->regs[0x101]==0);
}
void transactionBoundaries() {
    TestService t; std::promise<void> entered, release; auto ready=entered.get_future(); auto resume=release.get_future();
    auto first=std::async(std::launch::async,[&] { return t.service.transaction([&](HardwareSession &s) {
        s.execute(msrRequest(0,1)); entered.set_value(); resume.wait(); s.execute(msrRequest(0,2)); return 0;
    }); });
    ready.wait(); std::atomic<bool> cancel{true}; bool called=false;
    assert(t.service.transaction([&](HardwareSession &) { called=true; return 0; },10,&cancel)==-ECANCELED && !called);
    assert(t.service.transaction([&](HardwareSession &) { called=true; return 0; },2)==-ETIMEDOUT && !called);
    auto second=std::async(std::launch::async,[&] { return t.service.execute(msrRequest(0,3)).error; });
    release.set_value(); assert(first.get()==0 && second.get()==0);
    assert(t.fake->requests.size()==3 && t.fake->requests[0].address==1 && t.fake->requests[1].address==2 && t.fake->requests[2].address==3);
    const auto count=t.fake->requests.size();
    assert(t.service.transaction([&](HardwareSession &s) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); return s.execute(msrRequest(0,4)).error; },1)==-ETIMEDOUT);
    assert(t.fake->requests.size()==count);
}
void intelRAPL() {
    TestService t; IntelSnapshot snapshot; UpdateResult result;
    t.service.transaction([&](HardwareSession &s) { snapshot=readIntelControls(s,3); return snapshot.error; });
    assert(!snapshot.error && snapshot.rapl && snapshot.readings[0].value==125 && snapshot.readings[1].value==250);
    const auto original=t.fake->regs[0x610];
    t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,snapshot,IntelField::Pl1,100); return result.error; });
    assert(!result.error && (t.fake->regs[0x610]&0x7fff)==800 && (t.fake->regs[0x610]&~UINT64_C(0x7fff))==(original&~UINT64_C(0x7fff)));
    const auto writes=t.fake->writes;
    t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,snapshot,IntelField::Pl2,150); return result.error; });
    assert(result.error==-EAGAIN && t.fake->writes==writes);
    assert(raplWindowSeconds(0x6a,10)==1.75);
    unsigned code=999; assert(!encodeRaplWindow(1.76,10,code) && code==0x6a);
    assert(encodeRaplWindow(0,10,code)==-ERANGE);
    t.fake->regs[0x610] |= UINT64_C(1)<<63;
    t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,snapshot,IntelField::Pl1,100); return result.error; });
    assert(result.error==-EPERM && t.fake->writes==writes);
}
void intelHwpAndGating() {
    TestService t; IntelSnapshot snap; UpdateResult result;
    t.service.transaction([&](HardwareSession &s) { snap=readIntelControls(s,0); return snap.error; });
    const auto initial=t.fake->regs[0x774];
    t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,snap,IntelField::HwpEpp,192); return result.error; });
    assert(!result.error && t.fake->regs[0x774]==((initial & ~UINT64_C(0xff000000))|UINT64_C(0xc0000000)));
    t.service.transaction([&](HardwareSession &s) { result=applyIntelControl(s,snap,IntelField::HwpMin,65); return result.error; });
    assert(result.error==-ERANGE && t.fake->writes==1);
    t.fake->model=0xff; t.fake->hwp=false; t.fake->requests.clear();
    t.service.transaction([&](HardwareSession &s) { snap=readIntelControls(s,0); return snap.error; });
    assert(!snap.rapl && t.fake->requests.empty());
    t.fake->amd=true;
    t.service.transaction([&](HardwareSession &s) { snap=readIntelControls(s,0); return snap.error; });
    assert(snap.error==-ENOTSUP && t.fake->requests.empty());
}
IntelSnapshot snapshot(TestService &t) {
    IntelSnapshot out;
    assert(!t.service.transaction([&](HardwareSession &s) { out = readIntelControls(s, 130); return out.error; }));
    return out;
}
IntelControlUpdate apply(TestService &t, const IntelSnapshot &old, IntelField field, double value) {
    IntelControlUpdate out;
    t.service.transaction([&](HardwareSession &s) { out = applyIntelControl(s, old, field, value); return out.error; });
    return out;
}
void intelVerifiedFields() {
    struct Case { IntelField field; double input, expected; std::uint64_t mask; };
    const Case cases[] = {
        {IntelField::Pl1, 100.12, 100, 0x7fff}, {IntelField::Pl2, 150.12, 150, UINT64_C(0x7fff) << 32},
        {IntelField::Pl1Enable, 0, 0, 1u << 15}, {IntelField::Pl2Enable, 0, 0, UINT64_C(1) << 47},
        {IntelField::Pl1Clamp, 1, 1, 1u << 16}, {IntelField::Pl2Clamp, 1, 1, UINT64_C(1) << 48},
        {IntelField::Pl1Window, 1.76, 1.75, 127u << 17}, {IntelField::Pl2Window, 1.76, 1.75, UINT64_C(127) << 49},
        {IntelField::HwpMin, 16, 16, 255}, {IntelField::HwpMax, 48, 48, 255u << 8},
        {IntelField::HwpDesired, 32, 32, 255u << 16}, {IntelField::HwpEpp, 192, 192, UINT64_C(255) << 24},
        {IntelField::HwpActivityWindow, 15333, 15000, UINT64_C(1023) << 32}
    };
    for (const auto &c : cases) {
        TestService t; t.fake->expectedCpu = 130;
        const auto old = snapshot(t); const auto r = apply(t, old, c.field, c.input);
        const auto before = r.msr == 0x610 ? old.powerLimit.value : old.hwpRequest.value;
        assert(!r.error && r.verified && r.readbackValid && !r.unchanged && r.writeAttempted);
        assert(r.readbackValue == c.expected && r.completed == 1 && r.submitted.size() == 1);
        assert(r.expected == t.fake->regs[r.msr] && !((r.expected ^ before) & ~c.mask));
        const auto again = apply(t, r.after, c.field, c.input);
        assert(!again.error && again.verified && again.unchanged && !again.writeAttempted && again.completed == 0);
        assert(again.submitted.empty() && again.readbackValue == c.expected && t.fake->writes == 1 && !t.fake->wrongCpu);
    }
}
void intelVerificationFailures() {
    for (int kind = 0; kind < 10; ++kind) {
        TestService t; const auto old = snapshot(t);
        const bool hwp = kind >= 5;
        t.fake->afterRequest = [&](const Request &r) {
            if (!r.write) return;
            switch (kind) {
            case 0: t.fake->regs[r.address] = old.powerLimit.value; break; // Ignored write.
            case 1: t.fake->regs[r.address] ^= UINT64_C(1) << 59; break; // Unrelated bit changed.
            case 2: t.fake->regs[0x606] ^= 1; break; // Unit changed.
            case 3: t.fake->model = 0xba; break;
            case 4: t.fake->failRead = int(t.fake->reads + 1); break; // Target readback fails.
            case 5: t.fake->regs[0x771] ^= 1; break;
            case 6: t.fake->regs[0x770] = 0; break;
            case 7: t.fake->epp = false; break;
            case 8: t.fake->regs[r.address] ^= 1; break; // Another HWP field changed.
            case 9: t.fake->hwp = false; break;
            }
        };
        const auto r = apply(t, old, hwp ? IntelField::HwpEpp : IntelField::Pl1, hwp ? 192 : 100);
        assert(r.error && !r.verified && !r.unchanged && r.writeAttempted && r.completed == 1 && t.fake->writes == 1);
        if (kind == 4 || kind == 9) assert(!r.readbackValid);
    }
    TestService t; const auto old = snapshot(t); t.fake->failWrite = 0;
    const auto r = apply(t, old, IntelField::Pl1, 100);
    assert(r.error == -EIO && r.writeAttempted && !r.verified && !r.readbackValid && !r.completed && t.fake->writes == 1);
}
void intelVerificationFaults() {
    for (bool hwp : {false, true}) {
        // Inject a failure into each identity/feature read, including readback.
        for (unsigned pos = 0; pos < 8; ++pos) {
            TestService t; const auto old = snapshot(t); t.fake->failCpuid = int(t.fake->cpuCalls + pos);
            const auto r = apply(t, old, hwp ? IntelField::HwpEpp : IntelField::Pl1, hwp ? 192 : 100);
            assert(r.error && !r.verified && t.fake->writes <= 1);
        }
        // Target and unit/capability read failures must never become zero data.
        const std::vector<unsigned> positions = hwp ? std::vector<unsigned>{2,3,4,10,13,14,15} : std::vector<unsigned>{0,1,10,11,12};
        for (unsigned pos : positions) {
            TestService t; const auto old = snapshot(t); t.fake->failRead = int(t.fake->reads + pos);
            const auto r = apply(t, old, hwp ? IntelField::HwpEpp : IntelField::Pl1, hwp ? 192 : 100);
            assert(r.error && !r.verified && t.fake->writes <= 1);
        }
    }
}
void intelVerificationGuards() {
    TestService t; const auto old = snapshot(t);
    for (auto field : {IntelField(-1), IntelField(14)}) {
        const auto calls = t.fake->cpuCalls, reads = t.fake->reads;
        assert(apply(t, old, field, 1).error == -EINVAL);
        assert(t.fake->cpuCalls == calls && t.fake->reads == reads && !t.fake->writes);
    }
    assert(apply(t, old, IntelField::Pl1, std::numeric_limits<double>::infinity()).error == -EINVAL);
    t.fake->regs[0x610] ^= UINT64_C(1) << 59;
    assert(apply(t, old, IntelField::Pl1, 125).error == -EAGAIN && !t.fake->writes); // Stale no-op.
    t.fake->regs[0x610] = old.powerLimit.value;
    std::atomic<bool> cancelled{false};
    t.fake->afterRequest = [&](const Request &r) { if (r.write) cancelled = true; };
    IntelControlUpdate r;
    assert(t.service.transaction([&](HardwareSession &s) { r = applyIntelControl(s, old, IntelField::Pl1, 100); return r.error; }, 1000, &cancelled) == -ECANCELED);
    assert(!r.verified && r.writeAttempted && r.completed == 1 && t.fake->writes == 1);
}
void intelActivityWindow() {
    unsigned code = 9999;
    assert(!encodeHwpActivityWindow(0, code) && code == 0);
    assert(!encodeHwpActivityWindow(1, code) && code == 1);
    assert(!encodeHwpActivityWindow(1270000000, code) && code == 1023);
    assert(encodeHwpActivityWindow(1270000001, code) == -ERANGE && code == 1023);
    // Exhaust every encoding, including duplicate values and zero mantissas.
    for (unsigned c = 0; c < 1024; ++c) {
        const auto us = hwpActivityWindowMicroseconds(c);
        assert(!encodeHwpActivityWindow(us, code) && hwpActivityWindowMicroseconds(code) == us);
        if (us > 1) {
            assert(!encodeHwpActivityWindow(us - 1, code));
            const auto rounded = hwpActivityWindowMicroseconds(code);
            assert(rounded < us && rounded > 0);
            for (unsigned other = 0; other < 1024; ++other) {
                const auto candidate = hwpActivityWindowMicroseconds(other);
                assert(candidate >= us || candidate <= rounded);
            }
        }
    }
    TestService t; auto old = snapshot(t);
    for (double invalid : {-1.0, 0.5, 1270000001.0})
        assert(apply(t, old, IntelField::HwpActivityWindow, invalid).error == -ERANGE && !t.fake->writes);
    t.fake->activityWindow = false;
    assert(apply(t, old, IntelField::HwpActivityWindow, 100).error == -EPERM && !t.fake->writes);
    t.fake->activityWindow = true; t.fake->regs[0x774] |= UINT64_C(32) << 16;
    old = snapshot(t);
    assert(apply(t, old, IntelField::HwpActivityWindow, 100).error == -EPERM && !t.fake->writes);
    t.fake->regs[0x774] &= ~(UINT64_C(255) << 16); old = snapshot(t);
    t.fake->afterRequest = [&](const Request &r) { if (r.write) t.fake->activityWindow = false; };
    const auto r = apply(t, old, IntelField::HwpActivityWindow, 100);
    assert(r.error == -EAGAIN && !r.verified && r.completed == 1 && t.fake->writes == 1);
}
void intelNonTurboRatio() {
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned ratio=0;ratio<256;++ratio) {
        TestService t; t.fake->model=model; t.fake->expectedCpu=130;
        t.fake->regs[0xce]=UINT64_C(0x876543214000005a) | (std::uint64_t(ratio)<<8);
        const auto result=snapshot(t); const auto &row=result.readings.back();
        assert(row.name=="Maximum non-turbo ratio" && row.msr==0xce && !row.editable && row.unit=="ratio");
        assert(row.error==(ratio?0:-ENODATA) && row.decoded==bool(ratio));
        if (ratio) assert(row.raw==t.fake->regs[0xce] && row.value==ratio);
        assert(!t.fake->writes && !t.fake->wrongCpu);
    }
}
void intelNonTurboRatioFailures() {
    TestService t; t.fake->failRead=5; const auto result=snapshot(t); const auto &row=result.readings.back();
    assert(row.msr==0xce && row.error==-EACCES && !row.decoded && !row.editable);
    for (unsigned model : {0x55,0xa5,0xad,0xbf,0}) {
        TestService other; other.fake->model=model; const auto r=snapshot(other);
        for (const auto &value:r.readings) assert(value.msr!=0xce);
        for (const auto &request:other.fake->requests) assert(request.address!=0xce);
    }
}
const IntelReading &named(const IntelSnapshot &s, const char *name) {
    for (const auto &r:s.readings) if (r.name==name) return r;
    assert(false); return s.readings[0];
}

void intelHwpContextFields() {
    TestService t; t.fake->enableHwpContext(); t.fake->expectedCpu=130;
    const auto out=snapshot(t);
    struct Check { const char *name; double value; const char *unit; unsigned msr; };
    const Check cases[] = {
        {"HWP package minimum",16,"performance level",0x772}, {"HWP package maximum",56,"performance level",0x772},
        {"HWP package desired (0 = autonomous)",0,"performance level",0x772},
        {"HWP package energy preference",136,"0..255",0x772}, {"HWP package activity window (0 = automatic)",20000,"us",0x772},
        {"HWP package control bit",0,"0 / 1",0x774}, {"HWP minimum valid bit",1,"0 / 1",0x774},
        {"HWP maximum valid bit",0,"0 / 1",0x774}, {"HWP desired valid bit",0,"0 / 1",0x774},
        {"HWP EPP valid bit",0,"0 / 1",0x774}, {"HWP activity window valid bit",0,"0 / 1",0x774},
        {"HWP PECI minimum",18,"performance level",0x775}, {"HWP PECI maximum",48,"performance level",0x775},
        {"HWP PECI energy preference",195,"0..255",0x775}, {"HWP PECI minimum override",0,"0 / 1",0x775},
        {"HWP PECI maximum override",1,"0 / 1",0x775}, {"HWP PECI EPP override",1,"0 / 1",0x775},
        {"HWP package control polarity",0,"0 / 1",0x776},
        {"HWP guaranteed change interrupt enabled",1,"0 / 1",0x773},
        {"HWP minimum excursion interrupt enabled",1,"0 / 1",0x773},
        {"HWP highest change interrupt enabled",0,"0 / 1",0x773},
        {"HWP PECI override interrupt enabled",1,"0 / 1",0x773},
        {"HWP highest performance change history",0,"0 / 1",0x777},
        {"HWP PECI override entry history",0,"0 / 1",0x777}, {"HWP PECI override exit history",0,"0 / 1",0x777}
    };
    for (const auto &c:cases) {
        const auto &v=named(out,c.name);
        assert(v.decoded && !v.error && !v.editable && v.value==c.value && v.unit==c.unit && v.msr==c.msr && v.raw==t.fake->regs[c.msr]);
    }
    assert(named(out,"HWP minimum").editable && !named(out,"HWP maximum").editable && !named(out,"HWP energy preference").editable);
    assert(t.fake->reads==14 && !t.fake->writes && !t.fake->wrongCpu);
    // Every bit of the PECI source, including the reserved fields, is isolated.
    for (unsigned bit=0;bit<64;++bit) {
        t.fake->regs[0x775]=UINT64_C(1)<<bit; const auto s=snapshot(t);
        assert(named(s,"HWP PECI minimum").value==(bit<8?double(1u<<bit):0));
        assert(named(s,"HWP PECI maximum").value==(bit>=8 && bit<16?double(1u<<(bit-8)):0));
        assert(named(s,"HWP PECI energy preference").value==(bit>=24 && bit<32?double(1u<<(bit-24)):0));
        assert(named(s,"HWP PECI minimum override").value==(bit==63?1.0:0.0));
        assert(named(s,"HWP PECI maximum override").value==(bit==62?1.0:0.0));
        assert(named(s,"HWP PECI EPP override").value==(bit==60?1.0:0.0));
    }
}
void intelHwpContextCapabilityGates() {
    for (unsigned mask=0;mask<512;++mask) {
        TestService t; t.fake->model=0xff; t.fake->hwp=mask&1;
        t.fake->packageRequest=mask&2; t.fake->flexible=mask&4; t.fake->peci=mask&8;
        t.fake->control=mask&16; t.fake->notifications=mask&32; t.fake->highestChange=mask&64;
        t.fake->epp=mask&128; t.fake->activityWindow=mask&256;
        const auto out=snapshot(t);
        auto reads=[&](unsigned msr) { return std::count_if(t.fake->requests.begin(),t.fake->requests.end(),[&](const Request &r) { return r.address==msr && !r.write; }); };
        assert(reads(0x772)==int(t.fake->hwp && t.fake->packageRequest));
        assert(reads(0x775)==int(t.fake->hwp && t.fake->peci));
        assert(reads(0x776)==int(t.fake->hwp && t.fake->control));
        assert(reads(0x773)==int(t.fake->hwp && t.fake->notifications));
        auto count=[&](unsigned msr) { return std::count_if(out.readings.begin(),out.readings.end(),[&](const IntelReading &r) { return r.msr==msr; }); };
        assert(count(0x772)==(t.fake->hwp && t.fake->packageRequest?3+int(t.fake->epp)+int(t.fake->activityWindow):0));
        assert(count(0x775)==(t.fake->hwp && t.fake->peci?6:0));
        assert(count(0x773)==(t.fake->hwp && t.fake->notifications?2+int(t.fake->highestChange)+int(t.fake->peci):0));
        assert(count(0x777)==(t.fake->hwp?2+int(t.fake->highestChange)+2*int(t.fake->peci):0));
        assert(!t.fake->writes);
    }
    for (bool failed : {false,true}) {
        TestService t; t.fake->enableHwpContext(); t.fake->regs[0x770]=0; if (failed) t.fake->failRead=2;
        const auto s=snapshot(t); assert(!s.hwpEnabled && s.hwpControlValue.error==(failed?-EACCES:-EPERM));
        for (const auto &r:t.fake->requests) assert(r.address!=0x776);
        for (const auto &r:s.readings) if (r.msr==0x774) assert(!r.editable);
    }
}
void intelHwpContextReadFailures() {
    for (unsigned pos=5;pos<9;++pos) {
        TestService t; t.fake->enableHwpContext(); t.fake->failRead=int(pos); const auto out=snapshot(t);
        const unsigned msr=pos==5?0x772:pos==6?0x775:pos==7?0x776:0x773;
        unsigned rows=0;
        for (const auto &r:out.readings) if (r.msr==msr) { ++rows; assert(r.error==-EACCES && !r.decoded && !r.editable); }
        assert(rows==(pos==5?5u:pos==6?6u:pos==7?1u:4u));
        assert(named(out,"Package PL1").editable && named(out,"HWP highest capability").decoded);
        if (pos==6 || pos==7) for (const auto &r:out.readings) if (r.msr==0x774) assert(!r.editable);
        assert(!t.fake->writes);
    }
}
void intelHwpPeciAndPolarityGuards() {
    for (unsigned mask=0;mask<8;++mask) {
        TestService t; t.fake->enableHwpContext(); t.fake->regs[0x775]=
            ((mask&1)?UINT64_C(1)<<63:0)|((mask&2)?UINT64_C(1)<<62:0)|((mask&4)?UINT64_C(1)<<60:0);
        const auto s=snapshot(t);
        assert(named(s,"HWP minimum").editable==!(mask&1)); assert(named(s,"HWP maximum").editable==!(mask&2));
        assert(named(s,"HWP energy preference").editable==!(mask&4));
    }
    for (unsigned polarity=0;polarity<2;++polarity) for (unsigned bit=0;bit<2;++bit) {
        TestService t; t.fake->enableHwpContext(); t.fake->regs[0x775]=0; t.fake->regs[0x776]=polarity;
        t.fake->regs[0x774]=(t.fake->regs[0x774]&~(UINT64_C(1)<<42))|(std::uint64_t(bit)<<42);
        const auto s=snapshot(t); const auto result=apply(t,s,IntelField::HwpEpp,192);
        assert(named(s,"HWP energy preference").editable==(polarity==bit));
        assert(result.verified==(polarity==bit) && result.writeAttempted==(polarity==bit));
        assert(t.fake->writes==unsigned(polarity==bit));
        for (const auto &r:t.fake->requests) if (r.write) assert(r.address==0x774);
    }
    for (IntelField field : {IntelField::HwpMin,IntelField::HwpMax,IntelField::HwpEpp}) {
        TestService t; t.fake->enableHwpContext(); t.fake->regs[0x775]=UINT64_MAX;
        const auto r=apply(t,snapshot(t),field,32); assert(r.error==-EPERM && !r.writeAttempted && !t.fake->writes);
    }
}
void intelHwpContextChangesRejectVerification() {
    for (bool afterWrite : {false,true}) for (unsigned change=0;change<8;++change) {
        TestService t; t.fake->enableHwpContext(); t.fake->regs[0x775]=0;
        const auto old=snapshot(t);
        auto mutate=[&] {
            if (change==0) t.fake->regs[0x775]^=1; // Values matter even if override is inactive.
            if (change==1) t.fake->regs[0x775]^=UINT64_C(1)<<60;
            if (change==2) t.fake->regs[0x776]^=1;
            if (change==3) t.fake->peci=false;
            if (change==4) t.fake->control=false;
            if (change==5) t.fake->flexible=false;
            if (change==6) t.fake->failRead=int(t.fake->reads+6);
            if (change==7) t.fake->failRead=int(t.fake->reads+7);
        };
        if (afterWrite) t.fake->afterRequest=[&](const Request &r) { if (r.write) mutate(); }; else mutate();
        const auto r=apply(t,old,IntelField::HwpEpp,192);
        assert(r.error && !r.verified && r.writeAttempted==afterWrite && t.fake->writes==unsigned(afterWrite));
    }
}

void intelEnergyCountersAndCapabilities() {
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned exponent=0;exponent<32;++exponent)
        for (std::uint32_t ticks : {0u,1u,0x80000000u,UINT32_MAX}) {
            TestService t; t.fake->model=model; t.fake->expectedCpu=130;
            t.fake->regs[0x606]=UINT64_C(0xfedcba9876500087) | (std::uint64_t(exponent)<<8);
            for (unsigned msr : {0x611,0x619,0x639}) t.fake->regs[msr]=UINT64_C(0x8765432100000000)|ticks;
            const auto out=snapshot(t);
            const auto &unit=named(out,"RAPL energy counter unit"), &range=named(out,"RAPL energy counter wrap range");
            assert(unit.decoded && unit.value==std::ldexp(1.0,-int(exponent)) && unit.unit=="J/tick" && !unit.editable);
            assert(range.value==std::ldexp(1.0,32-int(exponent)) && range.unit=="J" && !range.editable);
            unsigned counters=0;
            for (const auto &r:out.readings) if (r.msr==0x611 || r.msr==0x619 || r.msr==0x639) {
                ++counters; assert(r.decoded && !r.error && !r.editable && r.unit=="J");
                assert(r.value==double(ticks)*unit.value && r.raw==t.fake->regs[r.msr] && r.value<range.value);
                assert(r.msr==0x611 || r.msr==unsigned(model==0xb7?0x639:0x619));
            }
            assert(counters==2 && !t.fake->writes && !t.fake->wrongCpu);
            for (const auto &r:t.fake->requests) assert(r.address!=unsigned(model==0xb7?0x619:0x639));
        }
    for (unsigned byte=0;byte<256;++byte) {
        TestService t; t.fake->regs[0x771]=UINT64_C(0xfedcba9800000000)|(std::uint64_t(byte)<<8)|
            (std::uint64_t(255-byte)<<16)|0xff;
        const auto out=snapshot(t);
        const auto &guaranteed=named(out,"HWP guaranteed capability"), &efficient=named(out,"HWP most efficient capability");
        assert(guaranteed.value==byte && efficient.value==255-byte && guaranteed.decoded && efficient.decoded);
        assert(!guaranteed.editable && !efficient.editable && guaranteed.msr==0x771 && efficient.msr==0x771);
        assert(guaranteed.unit=="performance level" && efficient.unit=="performance level" && !t.fake->writes);
    }
}
void intelTelemetryErrorsAndGates() {
    for (int position : {0,3,6,7}) {
        TestService t; t.fake->failRead=position; const auto out=snapshot(t);
        for (const auto &r:out.readings) {
            const bool energy=r.msr==0x611 || r.msr==0x619 || r.msr==0x639 || r.msr==0x606;
            const bool failed=(position==0 && energy) || (position==3 && r.msr==0x771) ||
                (position==6 && r.msr==0x611) || (position==7 && r.msr==0x639);
            if (failed) assert(r.error==-EACCES && !r.decoded && !r.editable);
        }
        assert(!t.fake->writes);
    }
    for (unsigned mode=0;mode<3;++mode) {
        TestService t;
        if (mode==0) t.fake->msr=false;
        if (mode==1) t.fake->virtualized=true;
        if (mode==2) t.fake->failCpuid=3;
        IntelSnapshot out;
        const auto error=t.service.transaction([&](HardwareSession &s) { out=readIntelControls(s,130); return out.error; });
        assert(error==(mode==2?-EACCES:0));
        for (const auto &r:out.readings) if (r.msr==0x611 || r.msr==0x639 || r.msr==0x606)
            assert(r.error && !r.decoded && !r.editable);
        for (const auto &r:t.fake->requests) assert(r.address!=0x611 && r.address!=0x619 && r.address!=0x639);
    }
    for (unsigned model : {0x55,0xa5,0xad,0xbf,0}) {
        TestService t; t.fake->model=model; t.fake->hwp=false; const auto out=snapshot(t);
        for (const auto &r:out.readings) assert(r.msr!=0x611 && r.msr!=0x619 && r.msr!=0x639 && r.msr!=0x771);
    }
}
void intelHwpHistory() {
    // Every single register bit, both defined bits together and all reserved
    // bits. The fixture deliberately does not advertise HWP notifications.
    std::vector<std::uint64_t> patterns{0,5,UINT64_MAX};
    for (unsigned bit=0;bit<64;++bit) patterns.push_back(UINT64_C(1)<<bit);
    for (unsigned model : {0xb7,0xff}) for (auto raw : patterns) {
        TestService t; t.fake->model=model; t.fake->expectedCpu=130; t.fake->regs[0x777]=raw;
        const auto out=snapshot(t);
        const auto &guaranteed=named(out,"HWP guaranteed performance change history");
        const auto &minimum=named(out,"HWP excursion to minimum history");
        assert(guaranteed.value==double(raw&1) && minimum.value==double((raw>>2)&1));
        for (const auto *r : {&guaranteed,&minimum}) {
            assert(r->decoded && !r->error && !r->editable && r->raw==raw && r->msr==0x777 && r->unit=="0 / 1");
        }
        unsigned reads=0;
        for (const auto &r:t.fake->requests) if (r.address==0x777) ++reads;
        assert(reads==1 && !t.fake->writes && !t.fake->wrongCpu);
    }
}
void intelHwpHistoryGatesAndFailures() {
    for (unsigned mode=0;mode<3;++mode) {
        TestService t;
        if (mode==0) t.fake->hwp=false;
        if (mode==1) t.fake->amd=true;
        if (mode==2) t.fake->failCpuid=2;
        IntelSnapshot out;
        t.service.transaction([&](HardwareSession &s) { out=readIntelControls(s,130); return out.error; });
        for (const auto &r:out.readings) assert(r.msr!=0x777);
        for (const auto &r:t.fake->requests) assert(r.address!=0x777);
        assert(!t.fake->writes);
    }
    TestService t; t.fake->failRead=8; const auto old=snapshot(t);
    for (const auto &r:old.readings) if (r.msr==0x777) assert(r.error==-EACCES && !r.decoded && !r.editable);
    // A failed history read is not a zero event, and must not turn unrelated
    // ordinary configuration fields into invalid values.
    assert(named(old,"Package PL1").decoded && named(old,"HWP minimum").editable);
    const auto result=apply(t,old,IntelField::HwpEpp,192);
    assert(!result.error && result.verified && t.fake->writes==1);
    for (const auto &r:t.fake->requests) if (r.write) assert(r.address==0x774);
}
struct SmuFake : Fake {
    std::uint32_t index=0, response=1, completion=1, id=0x153a1022, message=0;
    bool failArgs=false;
    SmuFake() { amd=true; }
    Reply execute(const Request &r) override {
        requests.push_back(r); Reply out;
        if (!r.write && r.address==0) { out.value=id; return out; }
        if (r.address==0xf8 && r.write) { index=std::uint32_t(r.value); return out; }
        assert(r.address==0xfc);
        if (r.write) {
            ++writes;
            if (failArgs && index==0x3b109cc) { out.error=-EIO; return out; }
            if (index==0x3b1097c) response=std::uint32_t(r.value);
            if (index==0x3b10930) { message=std::uint32_t(r.value); response=completion; }
            return out;
        }
        ++reads; out.value=index==0x3b1097c?response:index-0x3b109c4+100; return out;
    }
};
void smuProtocol() {
    auto *fake=new SmuFake; HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target; target.profile=SmuProfile::Shimada; SmuCommand command; command.message=2;
    SmuReply reply;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(!reply.error && reply.response==1 && reply.args[0]==100 && reply.args[5]==120 && reply.completedCommands==1 && fake->message==2);
    fake->completion=0xfe; fake->message=0;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(reply.error==-EIO && reply.response==0xfe && reply.args[0]==0);
    fake->response=1; fake->failArgs=true; fake->message=0;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(reply.error==-EIO && !reply.messageAttempted && fake->message==0);
    fake->id=0xffffffff; fake->requests.clear();
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(reply.error==-ENODEV && fake->requests.size()==1 && !fake->requests[0].write);
    fake->requests.clear(); command.message=0x21;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(reply.error==-EINVAL && fake->requests.empty());
    std::uint32_t arg=0; assert(!encodeShimadaCoreFrequency(3,7,6000,arg) && arg==0x30701770);
    assert(encodeShimadaCoreFrequency(16,0,6000,arg)==-ERANGE);
}
void smuTimeouts() {
    auto *fake=new SmuFake; HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target; target.profile=SmuProfile::Shimada; SmuCommand command; command.message=2; SmuReply reply;
    fake->response=0;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; },10);
    assert(reply.error==-ETIMEDOUT && !reply.messageAttempted && fake->writes==0);
    fake->response=1; fake->completion=0;
    service.transaction([&](HardwareSession &s) { reply=sendSmu(s,target,command); return reply.error; });
    assert(reply.error==-ETIMEDOUT && reply.messageAttempted && reply.completedCommands==0 && reply.args[0]==0);
}
void spdAndPstateBits() {
    const std::uint8_t crcVector[]={'1','2','3','4','5','6','7','8','9'};
    assert(spdCrc16(crcVector,9)==0x31c3); // CRC-16/XMODEM published check value.
    std::vector<std::uint8_t> bytes(236); bytes[1]=0x10; bytes[192]=0x10; bytes[2]=0x12; bytes[3]=2; bytes[4]=4; bytes[6]=0x20; bytes[234]=8; bytes[235]=2;
    auto s=decodeSpd(bytes); assert(!s.error && s.memoryType==0x12 && !s.crcChecked);
    assert(s.fields[3].value=="2" && s.fields[4].value=="8" && s.fields[5].value=="32");
    bytes.resize(235); assert(decodeSpd(bytes).error==-EMSGSIZE);
    bytes.resize(512); bytes[2]=0x0c; assert(decodeSpd(bytes).error==-EILSEQ);
    bytes[2]=0xff; assert(decodeSpd(bytes).error==-ENOTSUP);
    // Published Advantech AQD-SD5V16GE48-SB base bytes 0..47 and geometry.
    // Other bytes deliberately zeroed: this is a derived fixture, not a dump.
    // Common revision is 1.0; CRC 0x2baf was calculated with Python crc_hqx.
    const std::uint8_t base[] = {
        0x30,0x10,0x12,0x03,0x04,0x00,0x20,0x62,0,0,0,0,0x70,0,0,0,
        0,0,0,0,0xa0,1,0xf2,3,0x7a,0x0d,0,0,0,0,0x80,0x3e,
        0x80,0x3e,0x80,0x3e,0,0x7d,0x80,0xbb,0x30,0x75,0x27,1,0xa0,0,0x82,0};
    bytes.assign(512,0); std::copy(std::begin(base),std::end(base),bytes.begin());
    bytes[192]=0x10; bytes[235]=0x2a; bytes[510]=0xaf; bytes[511]=0x2b;
    s=decodeSpd(bytes); assert(!s.error && s.crcChecked && s.crcValid);
    auto field=[&](const char *name) { for (const auto &v:s.fields) if (v.name==name) return v.value; return std::string(); };
    assert(field("SDRAM device width (bits)")=="8" && field("Subchannels per module")=="2");
    assert(field("Module capacity (MiB)")=="16384" && field("Bus extension per subchannel (bits)")=="4");
    assert(field("SPD tCK minimum (ps)")=="416" && field("SPD tAA minimum (ps)")=="16000");
    assert(field("SPD tRFC1 minimum (ns)")=="295");
    bytes[200]^=1; s=decodeSpd(bytes);
    assert(s.error==-EILSEQ && s.crcChecked && !s.crcValid && s.fields.size()==1);
    bytes.resize(511); s=decodeSpd(bytes); assert(!s.error && !s.crcChecked);
    bytes[234]|=0x40; s=decodeSpd(bytes); assert(field("Module capacity (MiB)").empty());
    // Derived from the AQD-SD4U16GN32-SE1 published base bytes, with
    // unrelated fields zeroed and a separately calculated CRC (0x4e2e).
    const std::uint8_t ddr4[] = {
        0x23,0x11,0x0c,3,0x85,0x21,0,8,0,0x60,0,3,9,3,0,0,
        0,0,5,0x0d,0xf8,0xff,3,0,0x6e,0x6e,0x6e,0x11,0,0x6e,0xf0,0x0a,
        0x20,8,0,5,0,0xa8,0x14,0x28,0x28,0,0x78,0,0x14,0x3c,0,0};
    bytes.assign(128,0); std::copy(std::begin(ddr4),std::end(ddr4),bytes.begin());
    bytes[118]=0x9c; bytes[124]=0xe7; bytes[126]=0x2e; bytes[127]=0x4e;
    s=decodeSpd(bytes); assert(!s.error && s.crcValid);
    assert(field("Module capacity (MiB)")=="16384" && field("SPD tCK minimum (ps)")=="625");
    assert(field("SPD tCK maximum (ps)")=="1600" && field("SPD tRRD_L minimum (ps)")=="4900");
    assert(field("SPD tRFC1 minimum (ps)")=="350000" && field("SPD tRC minimum (ps)")=="45750");
    const auto p=decodeFamily1aPstate(UINT64_C(0x80000001ffffffff));
    assert(p.vidBits==511 && p.iddValueBits==255 && p.iddDivBits==3 && p.frequencyMHz==20475);
}
}
int main() {
    maskedUpdates(); partialFailure(); transactionBoundaries(); intelRAPL(); intelHwpAndGating(); smuProtocol(); smuTimeouts(); spdAndPstateBits();
    intelVerifiedFields(); intelVerificationFailures(); intelVerificationFaults(); intelVerificationGuards();
    intelActivityWindow(); intelNonTurboRatio(); intelNonTurboRatioFailures(); intelEnergyCountersAndCapabilities(); intelTelemetryErrorsAndGates();
    intelHwpHistory(); intelHwpHistoryGatesAndFailures();
    intelHwpContextFields(); intelHwpContextCapabilityGates(); intelHwpContextReadFailures(); intelHwpPeciAndPolarityGuards(); intelHwpContextChangesRejectVerification();
    std::cout << "24 platform scenario groups passed\n";
}
