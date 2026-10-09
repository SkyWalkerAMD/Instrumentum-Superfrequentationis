// SPDX-License-Identifier: GPL-2.0-only
#include "intel_controls.h"
#include "amd_smu.h"
#include "amd_pstates.h"
#include "spd.h"
#include <algorithm>
#include <iterator>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <map>
#include <thread>
#include <future>
#include <iostream>

using namespace octool::core;
namespace {
struct Fake : HardwareBackend {
    bool amd = false, hwp = true, epp = true;
    unsigned model = 0xb7;
    int failRead = -1, failWrite = -1;
    unsigned reads = 0, writes = 0;
    std::vector<Request> requests;
    std::map<std::uint64_t,std::uint64_t> regs;
    Fake() {
        regs[0x606] = UINT64_C(0xa0e03); // 1/8 W, 1/1024 s.
        regs[0x610] = UINT64_C(0x800000000000000) | (UINT64_C(2000) << 32) | (UINT64_C(1) << 47) | 1000 | (1 << 15);
        regs[0x770] = 1; regs[0x771] = 0x08203040; regs[0x774] = UINT64_C(0x8000100080004008);
    }
    Reply execute(const Request &r) override {
        requests.push_back(r); Reply out;
        if (r.write) {
            if (int(writes++) == failWrite) { out.error = -EIO; return out; }
            regs[r.address] = r.value; return out;
        }
        if (int(reads++) == failRead) { out.error = -EACCES; out.value = UINT64_MAX; return out; }
        out.value = regs[r.address]; return out;
    }
    CpuIdReply cpuid(unsigned, std::uint32_t leaf, std::uint32_t) override {
        CpuIdReply r;
        if (!leaf) { r.words[0]=6; r.words[1]=amd?0x68747541:0x756e6547; r.words[3]=amd?0x69746e65:0x49656e69; r.words[2]=amd?0x444d4163:0x6c65746e; }
        if (leaf==1) r.words[0]=amd?0x00b00f20:0x600|((model&15)<<4)|((model&240)<<12);
        if (leaf==6) r.words[0]=(hwp?1u<<7:0)|(epp?1u<<10:0);
        return r;
    }
    Backend backend(Space) const override { return Backend::Module; }
};
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
    std::cout << "8 platform scenario groups passed\n";
}
