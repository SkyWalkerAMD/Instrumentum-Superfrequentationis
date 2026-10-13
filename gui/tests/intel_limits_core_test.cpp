// SPDX-License-Identifier: GPL-2.0-only
#include "intel_limits.h"
#include "intel_limits_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelLimitsFixture *d; HardwareService service;
    Test():d(new IntelLimitsFixture),service(std::unique_ptr<HardwareBackend>(d)) {}
    IntelLimitsSnapshot read() { IntelLimitsSnapshot out; const int e=service.transaction([&](HardwareSession &s) { out=readIntelLimits(s,130); return out.error; }); assert(e==out.error); return out; }
    IntelLimitsClear clear(const IntelLimitsSnapshot &old,const std::string &reason="all") { IntelLimitsClear out; const int e=service.transaction([&](HardwareSession &s) { out=clearIntelLimitLogs(s,old,reason); return out.error; }); assert(e==out.error); return out; }
};
void decodingAndW0c() {
    const unsigned bits[]={0,1,4,5,6,7,8,10,11,12,13}; unsigned i=0;
    assert(intelLimitLogMask("all")==UINT64_C(0x3df30000) && !intelLimitLogMask("invalid"));
    for (const auto &r:intelLimitReasons()) { assert(r.bit==bits[i++] && intelLimitLogMask(r.key)==(UINT64_C(1)<<(r.bit+16))); }
    for (std::uint64_t raw : {UINT64_C(0),UINT64_MAX,UINT64_C(0x8765432100030000)}) {
        for (const auto &r:intelLimitReasons()) {
            Test t; t.d->regs[0x64f]=raw; const auto old=t.read(); assert(intelLimitsValid(old) && old.raw==raw);
            const auto out=t.clear(old,r.key); const auto mask=UINT64_C(1)<<(r.bit+16);
            assert(!out.error && out.verified && out.after.raw==(raw & ~mask));
            assert(out.unchanged==!(raw & mask) && t.d->writes==unsigned(bool(raw & mask)) && !t.d->wrongCpu);
            if (out.writeAttempted) assert(out.submitted==((raw | IntelLimitsFixture::logs)&~mask));
        }
        Test t; t.d->regs[0x64f]=raw; const auto out=t.clear(t.read());
        assert(!out.error && out.verified && out.after.raw==(raw & ~IntelLimitsFixture::logs));
    }
}
void volatileStatusAndNewEvents() {
    for (const auto &r:intelLimitReasons()) {
        Test t; t.d->regs[0x64f]=UINT64_MAX; const auto old=t.read();
        t.d->regs[0x64f]^=UINT64_C(1)<<r.bit;
        t.d->afterRequest=[&](const Request &q) { if (q.write) t.d->regs[0x64f]^=UINT64_C(1)<<r.bit; };
        const auto out=t.clear(old); assert(!out.error && out.verified && out.after.raw==(UINT64_MAX & ~IntelLimitsFixture::logs));
    }
    // An unselected history bit set between the last read and the write must
    // survive even if it was zero in both snapshots (W0C requires writing 1).
    Test t; t.d->regs[0x64f]=UINT64_C(1)<<16; const auto old=t.read(); t.d->newEvents=UINT64_C(1)<<17;
    const auto out=t.clear(old,"prochot"); assert(out.verified && out.after.raw==(UINT64_C(1)<<17));
    t.d->reassert=UINT64_C(1)<<17; const auto again=t.clear(out.after,"thermal");
    assert(again.error==-EAGAIN && again.writeAttempted && !again.verified && again.stillSetMask==(UINT64_C(1)<<17));
}
void staleSnapshotsAndCorruption() {
    const auto live=IntelLimitsFixture::logs>>16;
    for (unsigned bit=0;bit<64;++bit) {
        if (live & (UINT64_C(1)<<bit)) continue;
        Test t; const auto old=t.read(); t.d->regs[0x64f]^=UINT64_C(1)<<bit;
        assert(t.clear(old).error==-EAGAIN && !t.d->writes);
    }
    for (unsigned mode=0;mode<4;++mode) {
        Test t; const auto old=t.read();
        if (mode==0) t.d->discardChange=true;
        if (mode==1) t.d->afterRequest=[&](const Request &r) { if (r.write) t.d->regs[0x64f]^=UINT64_C(1)<<63; };
        if (mode==2) t.d->afterRequest=[&](const Request &r) { if (r.write) t.d->regs[0x64f]&=~(UINT64_C(1)<<17); };
        if (mode==3) t.d->afterRequest=[&](const Request &r) { if (r.write) t.d->stepping=1; };
        const auto out=t.clear(old,"prochot"); assert(out.error && !out.verified && out.writeAttempted && t.d->writes==1);
    }
}
void gatesAndInputs() {
    for (unsigned model : {0x8f,0xcf,0xad,0xa5,0xbf,0}) { Test t; t.d->model=model; assert(t.read().error==-ENOTSUP && t.d->requests.empty()); }
    for (unsigned mode=0;mode<4;++mode) {
        Test t;
        if (mode==0) t.d->amd=true;
        if (mode==1) t.d->hypervisor=true;
        if (mode==2) t.d->hasMsr=false;
        if (mode==3) t.d->inconsistentSignature=true;
        assert(t.read().error && t.d->requests.empty());
    }
    Test t; const auto old=t.read(); const auto calls=t.d->cpuCalls;
    assert(t.clear(old,"invalid").error==-EINVAL);
    for (unsigned mode=0;mode<3;++mode) {
        auto bad=old; if (mode==0) bad.valid=false; if (mode==1) bad.error=-EIO; if (mode==2) bad.identity.model=0x8f;
        assert(t.clear(bad).error==-EINVAL);
    }
    assert(t.d->cpuCalls==calls && !t.d->writes);
}
void failuresAndInterruption() {
    for (unsigned i=0;i<6;++i) { Test t; const auto old=t.read(); t.d->failCpuid=int(t.d->cpuCalls+i); const auto out=t.clear(old); assert(out.error==-EACCES && !out.verified); }
    for (unsigned i=0;i<2;++i) { Test t; const auto old=t.read(); t.d->failRead=int(t.d->reads+i); const auto out=t.clear(old); assert(out.error==-EACCES && !out.verified); }
    Test t; const auto old=t.read(); t.d->failWrite=0; const auto out=t.clear(old); assert(out.error==-EIO && out.writeAttempted && !out.verified);
    for (bool deadline : {false,true}) for (bool writing : {false,true}) {
        Test r; const auto before=r.read(); std::atomic<bool> cancelled(false);
        r.d->afterRequest=[&](const Request &q) {
            if (q.write!=writing) return;
            if (deadline) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled=true;
        };
        IntelLimitsClear result;
        const int error=r.service.transaction([&](HardwareSession &s) { result=clearIntelLimitLogs(s,before,"all"); return result.error; },deadline?10:1000,&cancelled);
        assert(error==(deadline?-ETIMEDOUT:-ECANCELED) && !result.verified && result.writeAttempted==writing);
    }
}
}
int main() { decodingAndW0c(); volatileStatusAndNewEvents(); staleSnapshotsAndCorruption(); gatesAndInputs(); failuresAndInterruption(); std::cout<<"5 Intel limit reasons scenario groups passed\n"; }
