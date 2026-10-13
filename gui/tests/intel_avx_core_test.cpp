// SPDX-License-Identifier: GPL-2.0-only
#include "intel_avx.h"
#include "intel_avx_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    IntelAvxFixture *device;
    HardwareService service;
    Test() : device(new IntelAvxFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelAvxSnapshot read() {
        IntelAvxSnapshot out;
        const auto error = service.transaction([&](HardwareSession &s) { out = readIntelAvx(s,130); return out.error; });
        assert(error == out.error); return out;
    }
    IntelAvxUpdate apply(const IntelAvxSnapshot &old, unsigned bins = 7) {
        IntelAvxUpdate out;
        const auto error = service.transaction([&](HardwareSession &s) { out = applyIntelAvx(s,old,bins); return out.error; });
        assert(error == out.error); return out;
    }
};
void allValuesAndPreservation() {
    for (std::uint32_t raw : {0u, UINT32_MAX, 0x87654321u, 0xa5c32110u}) for (unsigned bins=0; bins<=31; ++bins) {
        Test t; t.device->data = raw; const auto old = t.read();
        assert(old.valid && intelAvxEditable(old));
        const auto out = t.apply(old,bins); const auto expected = (raw & ~0x3e0u) | (bins << 5);
        assert(!out.error && out.verified && out.expectedValid && out.expected == expected);
        assert(out.after.response.data == expected && intelAvx2Offset(expected) == bins);
        assert(out.writeAttempted == (expected != raw) && out.unchanged == (expected == raw));
        assert(t.device->settingsAttempts() == unsigned(expected != raw) && !t.device->wrongCpu);
        assert((t.device->commands == (out.unchanged ? std::vector<unsigned>{0x1a,0x1a} : std::vector<unsigned>{0x1a,0x1a,0x1b,0x1a})));
    }
    for (unsigned bad : {32u, UINT32_MAX}) {
        std::uint32_t encoded = 123;
        assert(encodeIntelAvx2Offset(bad,0,encoded) == -ERANGE && encoded == 123);
        Test t; const auto old=t.read(); const auto calls=t.device->calls;
        assert(t.apply(old,bad).error == -ERANGE && t.device->calls == calls);
    }
}
void gatesAndLocks() {
    for (unsigned model : {0xa5,0x97,0xba,0x8f,0xcf,0xad,0}) {
        Test t; t.device->model=model;
        assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    for (unsigned mode=0; mode<6; ++mode) {
        Test t;
        if (mode==0) t.device->amd=true;
        if (mode==1) t.device->msr=false;
        if (mode==2) t.device->hypervisor=true;
        if (mode==3) t.device->avx=false;
        if (mode==4) t.device->avx2=false;
        if (mode==5) t.device->maximum=6;
        assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    Test t; t.device->locked=true; const auto old=t.read(); const auto calls=t.device->calls;
    assert(old.valid && !old.error && !intelAvxEditable(old));
    assert(t.apply(old).error == -EPERM && t.device->calls == calls);
}
void invalidSnapshots() {
    Test t; const auto old=t.read(); const auto calls=t.device->calls;
    for (unsigned mode=0; mode<7; ++mode) {
        auto bad=old;
        if (mode==0) bad.error=-EIO;
        if (mode==1) bad.valid=false;
        if (mode==2) bad.identity.model=0x8f;
        if (mode==3) bad.response.error=-EIO;
        if (mode==4) bad.response.commandAttempted=false;
        if (mode==5) bad.response.completed=false;
        if (mode==6) bad.response.firmwareStatus=255;
        assert(t.apply(bad).error == -EINVAL && t.device->calls == calls);
    }
}
void staleAndReadback() {
    for (unsigned bit=0; bit<32; ++bit) {
        Test t; const auto old=t.read(); t.device->data ^= 1u<<bit;
        assert(t.apply(old).error == -EAGAIN && !t.device->settingsAttempts());
        Test r; const auto before=r.read();
        r.device->afterRequest=[&] { const auto &q=r.device->requests.back(); if (q.write && ((q.value>>32)&255)==0x1b) r.device->data ^= 1u<<bit; };
        const auto out=r.apply(before); assert(out.error == -EIO && out.writeAttempted && !out.verified);
    }
    for (bool afterWrite : {false,true}) for (unsigned mode=0; mode<5; ++mode) {
        Test t; const auto old=t.read();
        auto change=[&] {
            if (mode==0) t.device->locked=true;
            if (mode==1) t.device->flexExtra ^= 1;
            if (mode==2) t.device->stepping=2;
            if (mode==3) t.device->hypervisor=true;
            if (mode==4) t.device->avx2=false;
        };
        if (!afterWrite) change();
        else t.device->afterRequest=[&] { const auto &q=t.device->requests.back(); if (q.write && ((q.value>>32)&255)==0x1b) change(); };
        const auto out=t.apply(old); assert(out.error && !out.verified && out.writeAttempted == afterWrite);
    }
    Test t; const auto old=t.read(); t.device->discardChange=true;
    assert(t.apply(old).error == -EIO && t.device->settingsAttempts()==1);
}
void transportFailures() {
    Test reference; const auto old=reference.read(); const unsigned reads=reference.device->calls;
    reference.device->clearTrace(); assert(reference.apply(old).verified); const unsigned writes=reference.device->calls;
    for (unsigned i=0; i<reads; ++i) {
        Test t; t.device->failAt=int(i); const auto out=t.read();
        assert(out.error == -EACCES && !out.valid && !t.device->settingsAttempts());
    }
    for (unsigned i=0; i<writes; ++i) {
        Test t; const auto before=t.read(); t.device->clearTrace(); t.device->failAt=int(i);
        const auto out=t.apply(before);
        assert(out.error == -EACCES && !out.verified && t.device->settingsAttempts()<=1);
        assert(out.writeAttempted == bool(t.device->settingsAttempts()));
    }
}
void firmwareAndBusy() {
    for (unsigned status=1; status<=255; ++status) for (unsigned command : {0x1au,0x1bu}) {
        Test t; const auto old=t.read(); t.device->failCommand=command; t.device->status=status;
        const auto out=t.apply(old);
        assert(out.error == -EIO && !out.verified && out.response.firmwareStatus==status);
        assert(out.writeAttempted == (command==0x1b) && !t.device->mutations);
    }
    for (unsigned command : {0u,0x1au,0x1bu}) {
        Test t; const auto old=t.read(); t.device->clearTrace();
        if (command) t.device->busyCommand=command; else t.device->busyBefore=true;
        const auto out=t.apply(old);
        assert(out.error == -ETIMEDOUT && out.writeAttempted == (command==0x1b));
        assert(t.device->calls<=120 && !out.verified);
    }
}
void cancellationAndDeadline() {
    for (bool timeout : {false,true}) for (bool afterWrite : {false,true}) {
        Test t; const auto old=t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest=[&] {
            const auto &q=t.device->requests.back();
            if (afterWrite ? !q.write || ((q.value>>32)&255)!=0x1b : q.address!=0x194) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25)); else cancelled.store(true);
        };
        IntelAvxUpdate out;
        const auto error=t.service.transaction([&](HardwareSession &s) { out=applyIntelAvx(s,old,7); return out.error; },timeout?10:1000,&cancelled);
        assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && !out.verified && out.writeAttempted==afterWrite);
    }
}
}
int main() {
    allValuesAndPreservation(); gatesAndLocks(); invalidSnapshots(); staleAndReadback();
    transportFailures(); firmwareAndBusy(); cancellationAndDeadline();
    std::cout << "7 Intel AVX2 offset scenario groups passed\n";
}
