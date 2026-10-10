// SPDX-License-Identifier: GPL-2.0-only
#include "intel_uncore.h"
#include "intel_uncore_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelUncoreFixture *device;
    HardwareService service;
    Test() : device(new IntelUncoreFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelUncoreSnapshot read() {
        IntelUncoreSnapshot out;
        const auto error = service.transaction([&](HardwareSession &s) { out = readIntelUncore(s, 130); return out.error; });
        assert(error == out.error); return out;
    }
    IntelUncoreUpdate apply(const IntelUncoreSnapshot &old, unsigned lo = 12, unsigned hi = 42) {
        IntelUncoreUpdate out;
        const auto error = service.transaction([&](HardwareSession &s) { out = applyIntelUncoreRange(s, old, lo, hi); return out.error; });
        assert(error == out.error); return out;
    }
};
void encoding() {
    for (const auto raw : {UINT64_C(0), UINT64_MAX, UINT64_C(0x9876543210fe882d)}) {
        for (unsigned lo = 1; lo <= 127; ++lo) for (unsigned hi = lo; hi <= 127; ++hi) {
            std::uint64_t word = 0;
            assert(!encodeIntelUncoreRange(raw, lo, hi, word));
            assert(intelUncoreMinimum(word) == lo && intelUncoreMaximum(word) == hi);
            assert((word & ~UINT64_C(0x7f7f)) == (raw & ~UINT64_C(0x7f7f)));
        }
    }
    for (const auto pair : {std::make_pair(0u, 40u), {1u, 0u}, {40u, 39u}, {1u, 128u}, {UINT32_MAX, UINT32_MAX}}) {
        std::uint64_t word = 123;
        assert(encodeIntelUncoreRange(0, pair.first, pair.second, word) == -ERANGE && word == 123);
    }
}
void readsAndGates() {
    for (unsigned model : {0xb7, 0x8f}) {
        Test t; t.device->model = model; const auto old = t.read();
        assert(old.valid && !old.error && old.cpu == 130 && intelUncoreEditable(old));
        assert(intelUncoreMinimum(old.raw) == 8 && intelUncoreMaximum(old.raw) == 45);
        assert(t.device->requests.size() == 1 && !t.device->wrongCpu && !t.device->writes);
        const auto &r = t.device->requests.front();
        assert(r.address == 0x620 && !r.write && r.width == 8 && r.space == Space::Msr);
    }
    for (unsigned mode = 0; mode < 7; ++mode) {
        Test t;
        if (mode == 0) t.device->amd = true;
        if (mode == 1) t.device->hasMsr = false;
        if (mode == 2) t.device->hypervisor = true;
        if (mode == 3) t.device->maximumLeaf = 0;
        if (mode >= 4) t.device->model = mode == 4 ? 0xad : mode == 5 ? 0xcf : 0xba;
        const auto out = t.read();
        assert(out.error && !out.valid && !out.raw && t.device->requests.empty());
    }
}
void updates() {
    for (unsigned model : {0xb7, 0x8f}) for (const auto pair : {std::make_pair(1u, 1u), {1u, 127u}, {127u, 127u}, {12u, 42u}}) {
        Test t; t.device->model = model; const auto old = t.read();
        const auto out = t.apply(old, pair.first, pair.second);
        assert(!out.error && out.verified && out.writeAttempted && out.completed == 1 && !out.unchanged);
        assert(out.after.raw == out.expected && out.after.valid && !t.device->wrongCpu);
        assert((out.after.raw & ~UINT64_C(0x7f7f)) == (old.raw & ~UINT64_C(0x7f7f)));
        assert(intelUncoreMinimum(out.after.raw) == pair.first && intelUncoreMaximum(out.after.raw) == pair.second);
        for (const auto &r : t.device->requests) assert(r.address == 0x620);
        const auto unchanged = t.apply(out.after, pair.first, pair.second);
        assert(!unchanged.error && unchanged.verified && unchanged.unchanged && !unchanged.writeAttempted && !unchanged.completed);
        assert(t.device->writes == 1);
    }
}
void invalidAndStale() {
    for (const auto raw : {UINT64_C(0), UINT64_C(0x00002d), UINT64_C(0x0800), UINT64_C(0x302d)}) {
        Test t; t.device->regs[0x620] = raw; const auto old = t.read();
        assert(old.valid && !old.error && !intelUncoreEditable(old));
        const auto count = t.device->requests.size();
        assert(t.apply(old).error == -EINVAL && t.device->requests.size() == count);
    }
    Test t; const auto old = t.read(); const auto calls = t.device->cpuCalls;
    assert(t.apply(old, 0, 42).error == -ERANGE && t.device->cpuCalls == calls);
    auto bad = old; bad.valid = false; assert(t.apply(bad).error == -EINVAL);
    bad = old; bad.error = -EIO; assert(t.apply(bad).error == -EINVAL);
    for (unsigned bit = 0; bit < 64; ++bit) {
        t.device->regs[0x620] = old.raw ^ (UINT64_C(1) << bit);
        assert(t.apply(old).error == -EAGAIN && !t.device->writes);
    }
    t.device->regs[0x620] = old.raw; t.device->model = 0x8f;
    assert(t.apply(old).error == -ENODEV && !t.device->writes);
    Test race; const auto before = race.read();
    race.device->afterRequest = [&](const Request &r) { if (!r.write && race.device->reads == 2) race.device->regs[0x620] ^= UINT64_C(1) << 40; };
    assert(race.apply(before).error == -EAGAIN && !race.device->writes);
}
void failures() {
    for (int position = 0; position < 3; ++position) {
        Test t; t.device->failCpuid = position;
        assert(t.read().error == -EACCES && t.device->requests.empty());
    }
    Test initial; initial.device->failRead = 0;
    assert(initial.read().error == -EACCES && !initial.read().error); // one injected failure
    for (int position = 0; position < 3; ++position) {
        Test t; const auto old = t.read(); t.device->failRead = int(t.device->reads) + position;
        const auto out = t.apply(old);
        assert(out.error == -EACCES && !out.verified && out.writeAttempted == (position == 2));
        assert(t.device->writes == (position == 2 ? 1u : 0u));
    }
    for (int position = 0; position < 6; ++position) {
        Test t; const auto old = t.read(); t.device->failCpuid = int(t.device->cpuCalls) + position;
        const auto out = t.apply(old);
        assert(out.error == -EACCES && !out.verified && out.writeAttempted == (position >= 3));
    }
    Test write; const auto old = write.read(); write.device->failWrite = 0;
    const auto out = write.apply(old);
    assert(out.error == -EIO && out.writeAttempted && !out.completed && !out.verified && write.device->writes == 1);
}
void readback() {
    for (unsigned bit = 0; bit < 64; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[0x620] ^= UINT64_C(1) << bit; };
        const auto out = t.apply(old);
        assert(out.error == -EIO && !out.verified && out.writeAttempted && out.completed == 1 && t.device->writes == 1);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) {
            if (mode == 0) t.device->model = 0x8f;
            if (mode == 1) t.device->hypervisor = true;
            if (mode == 2) t.device->hasMsr = false;
            if (mode == 3) t.device->amd = true;
        } };
        const auto out = t.apply(old);
        assert(out.error && !out.verified && t.device->writes == 1);
    }
    Test ignored; const auto old = ignored.read(); ignored.device->discardChange = true;
    assert(ignored.apply(old).error == -EIO && ignored.device->writes == 1);
}
void cancellation() {
    // Cancel after every access: preflight, compare, write and final readback.
    for (unsigned stop = 1; stop <= 4; ++stop) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false); unsigned calls = 0;
        t.device->afterRequest = [&](const Request &) { if (++calls == stop) cancelled.store(true); };
        IntelUncoreUpdate out;
        const auto error = t.service.transaction([&](HardwareSession &s) { out = applyIntelUncoreRange(s, old, 12, 42); return out.error; }, 5000, &cancelled);
        assert(error == -ECANCELED && out.error == error && !out.verified && t.device->writes == (stop >= 3 ? 1u : 0u));
    }
    Test t; const auto old = t.read();
    t.device->afterRequest = [](const Request &) { std::this_thread::sleep_for(std::chrono::milliseconds(3)); };
    IntelUncoreUpdate out;
    const auto error = t.service.transaction([&](HardwareSession &s) { out = applyIntelUncoreRange(s, old, 12, 42); return out.error; }, 1);
    assert(error == -ETIMEDOUT && !out.verified && !t.device->writes);
}
}
int main() {
    encoding(); readsAndGates(); updates(); invalidAndStale(); failures(); readback(); cancellation();
    std::cout << "7 Intel uncore scenario groups passed\n";
}
