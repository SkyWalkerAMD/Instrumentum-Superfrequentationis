// SPDX-License-Identifier: GPL-2.0-only
#include "intel_tvb.h"
#include "intel_tvb_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelTvbFixture *device;
    HardwareService service;
    Test() : device(new IntelTvbFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelTvbSnapshot read() {
        IntelTvbSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelTvb(s, 130); return out.error; });
        assert(error == out.error); return out;
    }
    IntelTvbUpdate apply(const IntelTvbSnapshot &old, IntelTvbField field = IntelTvbField::RatioClipping, unsigned enabled = 0) {
        IntelTvbUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelTvb(s, old, field, enabled); return out.error; });
        assert(error == out.error); return out;
    }
};
void encoding() {
    for (std::uint32_t raw : {0u, UINT32_MAX, 0xa5c32110u, 0x87654321u})
        for (auto field : {IntelTvbField::RatioClipping, IntelTvbField::VoltageOptimization, IntelTvbField::RingDownBin})
            for (unsigned enabled : {0u, 1u}) {
                std::uint32_t word = 0; assert(!encodeIntelTvb(field, enabled, raw, word));
                const unsigned bit = field == IntelTvbField::RatioClipping ? 2 : field == IntelTvbField::VoltageOptimization ? 3 : 1;
                assert((word & ~(1u << bit)) == (raw & ~(1u << bit)));
                assert(bool((word >> bit) & 1) == !enabled);
                assert(intelTvbRatioClipping(word) == !(word & 4));
                assert(intelTvbVoltageOptimization(word) == !(word & 8));
                assert(intelRingDownBin(word) == !(word & 2));
            }
    std::uint32_t word = 123;
    assert(encodeIntelTvb(IntelTvbField(99), 1, 0, word) == -EINVAL && word == 123);
    assert(encodeIntelTvb(IntelTvbField::RatioClipping, 2, 0, word) == -ERANGE && word == 123);
}
void readAndGates() {
    Test t; const auto out = t.read();
    assert(!out.error && out.valid && intelTvbEditable(out) && out.cpu == 130);
    assert(out.response.data == t.device->data && out.flexRatio == t.device->flexExtra);
    assert((t.device->commands == std::vector<unsigned>{0x18}) && !t.device->settingsAttempts() && !t.device->wrongCpu);
    for (unsigned model : {0x97, 0xba, 0x8f, 0xcf, 0xad, 0}) {
        Test x; x.device->model = model;
        assert(x.read().error == -ENOTSUP && x.device->requests.empty());
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Test x;
        if (mode == 0) x.device->amd = true;
        if (mode == 1) x.device->msr = false;
        if (mode == 2) x.device->hypervisor = true;
        assert(x.read().error == -ENOTSUP && x.device->requests.empty());
    }
}
void invalidAndLocked() {
    Test t; const auto old = t.read(); const auto calls = t.device->calls;
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto bad = old;
        if (mode == 0) bad.valid = false;
        if (mode == 1) bad.error = -EIO;
        if (mode == 2) bad.response.completed = false;
        if (mode == 3) bad.response.error = -EIO;
        if (mode == 4) bad.response.firmwareStatus = 1;
        if (mode == 5) bad.response.commandAttempted = false;
        if (mode == 6) bad.identity.model = 0x8f;
        assert(t.apply(bad).error == -EINVAL && t.device->calls == calls);
    }
    assert(t.apply(old, IntelTvbField(99)).error == -EINVAL && t.device->calls == calls);
    assert(t.apply(old, IntelTvbField::RatioClipping, UINT32_MAX).error == -ERANGE && t.device->calls == calls);
    t.device->locked = true; const auto locked = t.read(); const auto lockedCalls = t.device->calls;
    assert(!locked.error && locked.valid && !intelTvbEditable(locked));
    assert(t.apply(locked).error == -EPERM && t.device->calls == lockedCalls && !t.device->settingsAttempts());
}
void applyAndUnchanged() {
    for (std::uint32_t raw : {0u, UINT32_MAX, 0xa5c32110u, 0x87654321u})
        for (auto field : {IntelTvbField::RatioClipping, IntelTvbField::VoltageOptimization, IntelTvbField::RingDownBin})
            for (unsigned enabled : {0u, 1u}) {
                Test t; t.device->data = raw; const auto old = t.read();
                const auto out = t.apply(old, field, enabled);
                const unsigned mask = field == IntelTvbField::RatioClipping ? 4 : field == IntelTvbField::VoltageOptimization ? 8 : 2;
                const auto expected = (raw & ~mask) | (enabled ? 0u : mask);
                assert(!out.error && out.verified && out.stage == IntelOcStage::Complete);
                assert(out.expected == expected && out.readback.response.data == expected && t.device->data == expected);
                assert(out.writeAttempted == (expected != raw) && out.unchanged == (expected == raw));
                assert(t.device->settingsAttempts() == (expected != raw ? 1u : 0u) && !t.device->wrongCpu);
                assert((t.device->commands == (expected == raw ? std::vector<unsigned>{0x18, 0x18}
                    : std::vector<unsigned>{0x18, 0x18, 0x19, 0x18})));
            }
}
void staleSnapshots(IntelTvbField field = IntelTvbField::RatioClipping) {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read(); t.device->data ^= 1u << bit;
        assert(t.apply(old, field).error == -EAGAIN && !t.device->settingsAttempts());
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t; const auto old = t.read();
        if (mode == 0) t.device->flexExtra ^= 1;
        if (mode == 1) t.device->locked = true;
        if (mode == 2) t.device->stepping = 2;
        if (mode == 3) t.device->hypervisor = true;
        const auto out = t.apply(old, field);
        assert(out.error == (mode == 0 ? -EAGAIN : mode == 1 ? -EPERM : mode == 2 ? -ENODEV : -ENOTSUP));
        assert(!out.verified && !out.writeAttempted && !t.device->settingsAttempts());
    }
}
void everyTransportFailure(IntelTvbField field = IntelTvbField::RatioClipping) {
    Test reference; const auto old = reference.read(); const unsigned reads = reference.device->calls;
    reference.device->clearTrace(); assert(reference.apply(old, field).verified); const unsigned writes = reference.device->calls;
    for (unsigned i = 0; i < reads; ++i) {
        Test t; t.device->failAt = int(i); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !intelTvbEditable(out) && !t.device->settingsAttempts());
    }
    for (unsigned i = 0; i < writes; ++i) {
        Test t; const auto before = t.read(); t.device->clearTrace(); t.device->failAt = int(i);
        const auto out = t.apply(before, field);
        assert(out.error == -EACCES && !out.verified && t.device->settingsAttempts() <= 1);
        assert(out.writeAttempted == bool(t.device->settingsAttempts()));
    }
}
void firmwareFailures(IntelTvbField field = IntelTvbField::RatioClipping) {
    for (unsigned status = 1; status <= 255; ++status) for (unsigned command : {0x18u, 0x19u}) {
        Test t; const auto old = t.read(); t.device->failCommand = command; t.device->status = status;
        const auto out = t.apply(old, field);
        assert(out.error == -EIO && !out.verified && out.response.firmwareStatus == status);
        assert(out.writeAttempted == (command == 0x19) && !t.device->mutations);
    }
}
void boundedBusy(IntelTvbField field = IntelTvbField::RatioClipping) {
    for (unsigned command : {0u, 0x18u, 0x19u}) {
        Test t; const auto old = t.read(); t.device->clearTrace();
        if (!command) t.device->busyBefore = true; else t.device->busyCommand = command;
        const auto out = t.apply(old, field);
        assert(out.error == -ETIMEDOUT && !out.verified && out.writeAttempted == (command == 0x19));
        assert(t.device->calls <= 120 && t.device->settingsAttempts() == (command == 0x19 ? 1u : 0u));
    }
}
void fullReadbackVerification(IntelTvbField field = IntelTvbField::RatioClipping) {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (r.write && ((r.value >> 32) & 255) == 0x19) t.device->data ^= 1u << bit;
        };
        const auto out = t.apply(old, field);
        assert(out.error == -EIO && !out.verified && out.writeAttempted && t.device->settingsAttempts() == 1);
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (!r.write || ((r.value >> 32) & 255) != 0x19) return;
            if (mode == 0) t.device->flexExtra ^= 1;
            if (mode == 1) t.device->locked = true;
            if (mode == 2) t.device->stepping = 2;
            if (mode == 3) t.device->hypervisor = true;
            if (mode == 4) { t.device->failCommand = 0x18; t.device->status = 5; }
        };
        const auto out = t.apply(old, field);
        assert(out.error && !out.verified && out.writeAttempted && t.device->settingsAttempts() == 1);
    }
    Test t; const auto old = t.read(); t.device->discardChange = true;
    assert(t.apply(old, field).error == -EIO && t.device->settingsAttempts() == 1);
}
void cancellationAndDeadline(IntelTvbField field = IntelTvbField::RatioClipping) {
    for (bool timeout : {false, true}) for (bool afterWrite : {false, true}) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (afterWrite ? !r.write || ((r.value >> 32) & 255) != 0x19 : r.address != 0x194) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25));
            else cancelled.store(true);
        };
        IntelTvbUpdate out;
        const auto error = t.service.transaction([&](HardwareSession &s) {
            out = applyIntelTvb(s, old, field, 0); return out.error;
        }, timeout ? 10 : 1000, &cancelled);
        assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && !out.verified && out.writeAttempted == afterWrite);
        assert(t.device->settingsAttempts() == (afterWrite ? 1u : 0u));
    }
}
}
int main() {
    encoding(); readAndGates(); invalidAndLocked(); applyAndUnchanged(); staleSnapshots();
    everyTransportFailure(); firmwareFailures(); boundedBusy(); fullReadbackVerification(); cancellationAndDeadline();
    // Exercise the new field through every transport/status/stale/readback boundary.
    const auto ring = IntelTvbField::RingDownBin;
    staleSnapshots(ring); everyTransportFailure(ring); firmwareFailures(ring); boundedBusy(ring);
    fullReadbackVerification(ring); cancellationAndDeadline(ring);
    Test locked; locked.device->locked = true; const auto old = locked.read();
    assert(locked.apply(old, ring).error == -EPERM && !locked.device->settingsAttempts());
    std::cout << "11 Intel TVB / Ring DownBin scenario groups passed\n";
}
