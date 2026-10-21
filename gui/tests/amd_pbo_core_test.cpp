// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; }
    AmdPboScalar read(int timeout = 10000, const std::atomic<bool> *cancel = nullptr) {
        AmdPboScalar out;
        const int error = service.transaction([&](HardwareSession &s) { out = readShimadaPboScalar(s,target); return out.error; }, timeout, cancel);
        if (error) { out.error = error; out.valid = false; }
        return out;
    }
    AmdPboScalarUpdate set(unsigned value = 5, std::uint32_t expected = 0x3f800000,
                           const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdPboScalarUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = setShimadaPboScalar(s,target,expected,value); return out.error; }, timeout, cancel);
        if (error) { out.error = error; out.verified = false; }
        return out;
    }
};
void decode() {
    for (std::uint32_t raw : {0u,1u,0x3f000000u,0x3f800000u,0x41200000u}) {
        double value = -1; assert(decodeAmdPboScalar(raw,value) && value >= 0 && value <= 10);
    }
    for (std::uint32_t raw : {0x80000000u,0xbf800000u,0x41200001u,0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u,UINT32_MAX}) {
        double value = -1; assert(!decodeAmdPboScalar(raw,value) && value == 0);
        Test t; t.fake->scalarRaw = raw; const auto r = t.read();
        assert(r.error == -ERANGE && !r.valid && r.rawValid && r.raw == raw && t.fake->settings == 0);
        Test invalid; assert(invalid.set(2,raw).error == -ERANGE && !invalid.fake->calls);
    }
}
void eligibility() {
    for (unsigned mode = 0; mode < 10; ++mode) {
        Test t;
        if (mode < 3) t.target.profile = mode == 0 ? SmuProfile::None : mode == 1 ? SmuProfile::Phoenix : SmuProfile::Gpt;
        if (mode == 3) t.target.bus = 1;
        if (mode == 4) t.target.device = 1;
        if (mode == 5) t.target.function = 1;
        if (mode == 6) t.fake->amd = false;
        if (mode == 7) t.fake->family = 0x19;
        if (mode == 8) t.fake->model = 0x18;
        if (mode == 9) t.fake->pciId = 0x14d81022;
        const auto r = t.set(); assert(r.error && !r.writeAttempted && !t.fake->commands);
        if (mode < 6) assert(!t.fake->calls);
        for (const auto &q : t.fake->requests) assert(!q.write);
    }
    for (unsigned value : {0u,11u,UINT32_MAX}) { Test t; assert(t.set(value).error == -ERANGE && !t.fake->calls); }
}
void valuesAndArguments() {
    for (unsigned value = 1; value <= 10; ++value) {
        Test t; t.fake->scalarRaw = 0; const auto r = t.set(value,0);
        assert(!r.error && r.verified && r.accepted && !r.unchanged && r.writeAttempted && r.phase == AmdPboPhase::Complete);
        assert(r.argument == value*100 && r.before.valid && r.before.multiplier == 0 && r.after.valid && r.after.multiplier == value);
        assert(t.fake->settings == 1 && !t.fake->wrongCpu && t.fake->commands == 3 && t.fake->argumentReads == 2);
        assert((t.fake->messages == std::vector<std::uint32_t>{0x6d,0x5b,0x6d}));
        assert(t.fake->commandArgs[1][0] == value*100 && t.fake->commandArgs[0][0] == 0 && t.fake->commandArgs[2][0] == 0);
    }
}
void noopAndConflict() {
    Test same; const auto a = same.set(1);
    assert(!a.error && a.unchanged && a.verified && !a.writeAttempted && !a.accepted && !same.fake->settings && same.fake->commands == 1);
    Test changed; changed.fake->scalarRaw = 0x40000000;
    const auto b = changed.set(); assert(b.error == -EAGAIN && b.before.raw == 0x40000000 && !b.writeAttempted && !changed.fake->settings);
    Test identity; identity.fake->afterRequest = [&] { if (identity.fake->argumentReads) identity.fake->model = 0x18; };
    const auto c = identity.set(); assert(c.error == -ENOTSUP && c.before.valid && !c.writeAttempted && !identity.fake->settings);
}
void rejectionAndReadback() {
    for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion = status; const auto r = t.set();
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified && !r.after.valid);
        assert(r.phase == AmdPboPhase::Write && r.response == status && t.fake->settings == 1 && t.fake->commands == 2);
    }
    Test ignored; ignored.fake->discardChange = true; const auto a = ignored.set();
    assert(a.error == -EIO && a.accepted && !a.verified && a.after.raw == 0x3f800000 && a.phase == AmdPboPhase::Verify);
    Test corrupt; corrupt.fake->afterRequest = [&] { if (corrupt.fake->settings) corrupt.fake->scalarRaw = 0x7fc00000; };
    const auto b = corrupt.set(); assert(b.error == -ERANGE && b.accepted && b.after.rawValid && !b.after.valid && !b.verified);
    Test unavailable; unavailable.fake->afterRequest = [&] { if (unavailable.fake->settings) unavailable.fake->completion = 0xfe; };
    const auto c = unavailable.set(); assert(c.error == -EIO && c.accepted && !c.after.rawValid && c.phase == AmdPboPhase::Verify);
}
void queryRejectionAndBusy() {
    for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu}) {
        Test t; t.fake->completion = status; const auto r = t.read();
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && !r.valid && !r.rawValid && !t.fake->argumentReads);
    }
    Test busy; busy.fake->busyBefore = true; const auto r = busy.read();
    assert(r.error == -ETIMEDOUT && busy.fake->polls == 100 && !busy.fake->commands);
    for (const auto &q : busy.fake->requests) assert(!q.write || q.address == 0xb8);
    Test delay; delay.fake->busyPolls = 3; delay.fake->response = 0xfe;
    assert(delay.read().valid && delay.fake->polls == 4);
}
void queryFaults() {
    Test base; assert(base.read().valid);
    for (unsigned at = 0; at < base.fake->calls; ++at) {
        Test t; t.fake->failAt = int(at); const auto r = t.read();
        assert(r.error == -EACCES && !r.valid && !r.rawValid && t.fake->calls == at+1 && !t.fake->settings);
    }
}
void settingFaults() {
    Test base; assert(base.set().verified);
    for (unsigned at = 0; at < base.fake->calls; ++at) {
        Test t; t.fake->failAt = int(at); const auto r = t.set();
        assert(r.error == -EACCES && !r.verified && t.fake->calls == at+1 && t.fake->settings <= 1);
        assert(!r.accepted || r.writeAttempted);
    }
}
void cancellation() {
    Test base; assert(base.set().verified);
    for (unsigned after = 0; after <= base.fake->calls; ++after) {
        Test t; std::atomic<bool> cancel{after == 0};
        t.fake->afterRequest = [&] { if (t.fake->calls == after) cancel.store(true); };
        const auto r = t.set(5,0x3f800000,&cancel);
        assert(r.error == -ECANCELED && !r.verified && t.fake->calls == after && t.fake->settings <= 1);
    }
}
void deadlineAndLateRead() {
    Test t; t.fake->busyBefore = true; assert(t.read(1).error == -ETIMEDOUT && !t.fake->commands);
    Test late; late.fake->afterRequest = [&] {
        if (late.fake->argumentReads == 2) std::this_thread::sleep_for(std::chrono::milliseconds(5100));
    };
    const auto r = late.set(5,0x3f800000,nullptr,5000);
    assert(r.error == -ETIMEDOUT && r.accepted && !r.verified && !r.after.valid && !r.after.rawValid);
}
}
int main() {
    decode(); eligibility(); valuesAndArguments(); noopAndConflict(); rejectionAndReadback();
    queryRejectionAndBusy(); queryFaults(); settingFaults(); cancellation(); deadlineAndLateRead();
    std::cout << "10 AMD PBO scalar scenario groups passed\n";
}
