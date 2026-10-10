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
        for (unsigned pos = 0; pos < 6; ++pos) {
            TestService t; const auto old = snapshot(t); t.fake->failCpuid = int(t.fake->cpuCalls + pos);
            const auto r = apply(t, old, hwp ? IntelField::HwpEpp : IntelField::Pl1, hwp ? 192 : 100);
            assert(r.error && !r.verified && t.fake->writes <= 1);
        }
        // Target and unit/capability read failures must never become zero data.
        const std::vector<unsigned> positions = hwp ? std::vector<unsigned>{2,3,4,5,8,9,10} : std::vector<unsigned>{0,1,5,6,7};
        for (unsigned pos : positions) {
            TestService t; const auto old = snapshot(t); t.fake->failRead = int(t.fake->reads + pos);
            const auto r = apply(t, old, hwp ? IntelField::HwpEpp : IntelField::Pl1, hwp ? 192 : 100);
            assert(r.error && !r.verified && t.fake->writes <= 1);
        }
    }
}
void intelVerificationGuards() {
    TestService t; const auto old = snapshot(t);
    for (auto field : {IntelField(-1), IntelField(13)}) {
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
    std::vector<std::uint8_t> bytes(236); bytes[2]=0x12; bytes[3]=2; bytes[4]=4; bytes[6]=0x20; bytes[234]=8; bytes[235]=2;
    auto s=decodeSpd(bytes); assert(!s.error && s.memoryType==0x12 && !s.crcChecked);
    assert(s.fields[3].value=="2" && s.fields[4].value=="8" && s.fields[5].value=="32");
    bytes.resize(235); assert(decodeSpd(bytes).error==-EMSGSIZE);
    bytes.resize(512); bytes[2]=0x0c; assert(decodeSpd(bytes).error==-EILSEQ);
    bytes[2]=0xff; assert(decodeSpd(bytes).error==-ENOTSUP);
    // Published Advantech AQD-SD5V16GE48-SB base bytes 0..47 and geometry.
    // Other bytes deliberately zeroed: this is a derived fixture, not a dump.
    // Expected CRC 0x2145 was calculated separately with Python crc_hqx.
    const std::uint8_t base[] = {
        0x30,0x10,0x12,0x03,0x04,0x00,0x20,0x62,0,0,0,0,0x70,0,0,0,
        0,0,0,0,0xa0,1,0xf2,3,0x7a,0x0d,0,0,0,0,0x80,0x3e,
        0x80,0x3e,0x80,0x3e,0,0x7d,0x80,0xbb,0x30,0x75,0x27,1,0xa0,0,0x82,0};
    bytes.assign(512,0); std::copy(std::begin(base),std::end(base),bytes.begin());
    bytes[235]=0x2a; bytes[510]=0x45; bytes[511]=0x21;
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
    intelActivityWindow();
    std::cout << "13 platform scenario groups passed\n";
}
