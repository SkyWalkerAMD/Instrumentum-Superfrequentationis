// SPDX-License-Identifier: GPL-2.0-only
#include "intel_oc.h"
#include "intel_oc_fixture.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <chrono>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelOcFixture *device;
    HardwareService service;
    Test() : device(new IntelOcFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelOcSnapshot read(IntelOcDomain domain = IntelOcDomain::Core) {
        IntelOcSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelOc(s, 130, domain); return out.error; });
        assert(error == out.error); return out;
    }
    IntelOcUpdate apply(const IntelOcSnapshot &old, double value = -50) {
        IntelOcUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelOcOffset(s, old, value); return out.error; });
        assert(error == out.error); return out;
    }
    IntelOcUpdate ratio(const IntelOcSnapshot &old, unsigned value = 59) {
        IntelOcUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelOcRatio(s, old, value); return out.error; });
        assert(error == out.error); return out;
    }
    IntelOcUpdate voltage(const IntelOcSnapshot &old, unsigned value = 1234,
                          IntelOcVoltageMode mode = IntelOcVoltageMode::Adaptive) {
        IntelOcUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelOcVoltage(s, old, value, mode); return out.error; });
        assert(error == out.error); return out;
    }
    IntelVfSnapshot vf(unsigned point = 0, IntelOcDomain domain = IntelOcDomain::Core) {
        IntelVfSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelVf(s, 130, domain, point); return out.error; });
        assert(error == out.error); return out;
    }
};
void encoding() {
    // Exhaust the signed 11-bit field while unrelated target/mode/ratio bits
    // are nonzero. Exact representable values must round-trip unchanged.
    for (unsigned code = 0; code < 2048; ++code) {
        const std::uint32_t raw = (code << 21) | 0x1a7fcb;
        std::uint32_t value = 0;
        const double expected = double(code < 1024 ? int(code) : int(code) - 2048) * 1000.0 / 1024;
        assert(intelOcOffsetMillivolts(raw) == expected);
        assert(!encodeIntelOcOffset(expected, raw, value) && value == raw);
    }
    std::uint32_t value = 7;
    assert(!encodeIntelOcOffset(-50, 0x123456, value) && value == 0xf9b23456);
    assert(intelOcOffsetMillivolts(value) == -49.8046875);
    for (double invalid : {-1000.01, 1000.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        value = 7; assert(encodeIntelOcOffset(invalid, 0, value) == -ERANGE && value == 7);
    }
}
void readDomains() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) {
        Test t; t.device->locked = true; const auto out = t.read(domain);
        assert(!out.error && out.valid && out.locked && out.cpu == 130 && out.domain == domain);
        assert(out.response.data == t.device->settings[unsigned(domain)] && out.response.completed);
        assert(t.device->mutationCount == 0 && t.device->commands == std::vector<unsigned>{0x10});
        assert(t.device->requests.size() == 4 && !t.device->wrongCpu);
        assert(t.device->requests[0].address == 0x194);
        assert(t.device->requests[2].value == ((UINT64_C(0x80000010) | (std::uint64_t(unsigned(domain)) << 8)) << 32));
    }
}
void identityRejection() {
    for (unsigned model : {0x8f, 0xad, 0xcf, 0x97, 0xba, 0}) {
        Test t; t.device->model = model; assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    Test amd; amd.device->amd = true; assert(amd.read().error == -ENOTSUP && amd.device->requests.empty());
    Test msr; msr.device->msr = false; assert(msr.read().error == -ENOTSUP && msr.device->requests.empty());
    Test invalid; assert(invalid.read(IntelOcDomain(1)).error == -EINVAL && invalid.device->calls == 0);
}
void readFailures() {
    Test baseline; assert(baseline.read().valid); const unsigned count = baseline.device->calls;
    for (unsigned index = 0; index < count; ++index) {
        Test t; t.device->failAt = int(index); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !out.response.data);
        assert(t.device->calls == index + 1 && t.device->mutationCount == 0);
    }
}
void applyPreservesAndNoop() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) {
        Test t; const auto before = t.read(domain); const auto other = t.device->settings[2 - unsigned(domain)];
        t.device->clearTrace(); const auto out = t.apply(before);
        assert(!out.error && out.verified && out.writeAttempted && out.stage == IntelOcStage::Complete);
        assert((out.submitted & 0x1fffff) == (before.response.data & 0x1fffff));
        assert(intelOcOffsetMillivolts(out.readback.response.data) == -49.8046875);
        assert(t.device->settings[2 - unsigned(domain)] == other && t.device->mutationCount == 1 && !t.device->wrongCpu);
        assert(t.device->commands == std::vector<unsigned>({0x10, 0x11, 0x10}));
        t.device->clearTrace(); const auto unchanged = t.apply(out.readback);
        assert(!unchanged.error && unchanged.verified && !unchanged.writeAttempted);
        assert(t.device->commands == std::vector<unsigned>{0x10} && t.device->mutationCount == 1);
    }
}
void staleAndLock() {
    Test stale; const auto old = stale.read(); stale.device->settings[0] ^= 1u << 20;
    const auto changed = stale.apply(old); assert(changed.error == -EAGAIN && !changed.writeAttempted && !stale.device->mutationCount);
    Test locked; const auto beforeLock = locked.read(); locked.device->locked = true;
    assert(locked.apply(beforeLock).error == -EPERM && !locked.device->mutationCount);
    const auto nowLocked = locked.read(); locked.device->clearTrace();
    assert(locked.apply(nowLocked).error == -EPERM && locked.device->calls == 0);
    Test t; const auto valid = t.read(); t.device->clearTrace();
    assert(t.apply(valid, 1000).error == -ERANGE && t.device->calls == 0);
    auto corrupt = valid; corrupt.valid = false; assert(t.apply(corrupt).error == -EINVAL && t.device->calls == 0);
    corrupt = valid; corrupt.domain = IntelOcDomain(3); assert(t.apply(corrupt).error == -EINVAL && t.device->calls == 0);
}
void applyFailures() {
    Test base; const auto old = base.read(); base.device->clearTrace(); assert(base.apply(old).verified);
    const unsigned count = base.device->calls;
    for (unsigned index = 0; index < count; ++index) {
        Test t; const auto previous = t.read(); t.device->clearTrace(); t.device->failAt = int(index);
        const auto out = t.apply(previous);
        assert(out.error == -EACCES && !out.verified && t.device->calls == index + 1);
        assert(t.device->mutationCount <= 1);
        unsigned attempts = 0;
        for (const auto &r : t.device->requests) if (r.write && ((r.value >> 32) & 255) == 0x11) ++attempts;
        assert(attempts <= 1 && out.writeAttempted == (attempts != 0));
    }
}
void firmwareAndReadback() {
    Test query; query.device->failCommand = 0x10; query.device->status = 0xfe;
    const auto read = query.read(); assert(read.error == -EIO && !read.valid && !read.response.data && read.response.firmwareStatus == 0xfe);
    Test reject; const auto old = reject.read(); reject.device->failCommand = 0x11; reject.device->status = 3;
    const auto out = reject.apply(old);
    assert(out.error == -EIO && out.writeAttempted && !out.verified && out.response.firmwareStatus == 3 && !reject.device->mutationCount);
    Test ignored; const auto previous = ignored.read(); ignored.device->discardChange = true;
    const auto mismatch = ignored.apply(previous);
    assert(mismatch.error == -EIO && mismatch.stage == IntelOcStage::Verify && mismatch.readback.valid && !mismatch.verified);
    assert(ignored.device->mutationCount == 1 && mismatch.readback.response.data == previous.response.data);
}
void waitBounds() {
    Test initial; initial.device->busyBefore = true;
    assert(initial.read().error == -ETIMEDOUT && initial.device->commands.empty());
    assert(initial.device->requests.size() == 101); // lock read + exactly 100 busy observations
    Test completion; completion.device->busyCommand = 0x10;
    const auto out = completion.read(); assert(out.error == -ETIMEDOUT && out.response.commandAttempted && !out.valid);
    assert(completion.device->commands == std::vector<unsigned>{0x10});
    assert(completion.device->requests.size() == 103);
    Test mutation; const auto old = mutation.read(); mutation.device->busyCommand = 0x11;
    const auto update = mutation.apply(old);
    assert(update.error == -ETIMEDOUT && update.writeAttempted && !update.verified && mutation.device->mutationCount == 1);
}
void cancellation() {
    Test t; const auto old = t.read(); t.device->clearTrace(); std::atomic<bool> cancelled{false};
    // Cancel after the fresh query completed: no subsequent voltage write.
    t.device->afterRequest = [&] { if (t.device->requests.size() == 4) cancelled.store(true); };
    IntelOcUpdate out;
    const int error = t.service.transaction([&](HardwareSession &s) { out = applyIntelOcOffset(s, old, -50); return out.error; }, 1000, &cancelled);
    assert(error == -ECANCELED && !out.writeAttempted && !out.verified && !t.device->mutationCount);
}
void ratioEncoding() {
    for (unsigned ratio = 1; ratio <= 85; ++ratio) {
        for (const auto previous : {UINT32_C(0), UINT32_MAX, UINT32_C(0xf3512345), UINT32_C(0x012abcde)}) {
            std::uint32_t encoded = 0;
            assert(!encodeIntelOcRatio(ratio, previous, encoded));
            assert((encoded & 255) == ratio && (encoded & 0xffffff00u) == (previous & 0xffffff00u));
            assert(intelOcOffsetMillivolts(encoded) == intelOcOffsetMillivolts(previous));
        }
    }
    for (const unsigned invalid : {0u, 86u, 255u, 256u, UINT32_MAX}) {
        std::uint32_t encoded = 7;
        assert(encodeIntelOcRatio(invalid, UINT32_MAX, encoded) == -ERANGE && encoded == 7);
        Test t; const auto old = t.read(); t.device->clearTrace();
        assert(t.ratio(old, invalid).error == -ERANGE && t.device->calls == 0);
    }
}
void ratioPreservesVoltageAndNoop() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) {
        Test t; const auto old = t.read(domain); const auto other = t.device->settings[2 - unsigned(domain)];
        t.device->clearTrace(); const auto out = t.ratio(old);
        assert(!out.error && out.verified && out.writeAttempted);
        assert(out.submitted == ((old.response.data & 0xffffff00u) | 59u));
        assert(t.device->settings[2 - unsigned(domain)] == other && !t.device->wrongCpu);
        assert(t.device->commands == std::vector<unsigned>({0x10, 0x11, 0x10}));
        t.device->clearTrace(); const auto same = t.ratio(out.readback);
        assert(!same.error && same.verified && !same.writeAttempted && t.device->mutationCount == 1);
        assert(t.device->commands == std::vector<unsigned>{0x10});
        // Changing offset afterwards must keep the newly set ratio.
        assert(t.apply(same.readback).verified && (t.device->settings[unsigned(domain)] & 255) == 59);
    }
}
void ratioFailurePositionsAndReadback() {
    Test baseline; const auto old = baseline.read(); baseline.device->clearTrace(); assert(baseline.ratio(old).verified);
    for (unsigned index = 0; index < baseline.device->calls; ++index) {
        Test t; const auto before = t.read(); t.device->clearTrace(); t.device->failAt = int(index);
        const auto out = t.ratio(before);
        assert(out.error == -EACCES && !out.verified && t.device->calls == index + 1);
        unsigned attempts = 0;
        for (const auto &r : t.device->requests) if (r.write && ((r.value >> 32) & 255) == 0x11) ++attempts;
        assert(attempts <= 1 && out.writeAttempted == (attempts != 0));
    }
    Test reject; const auto before = reject.read(); reject.device->failCommand = 0x11; reject.device->status = 3;
    const auto denied = reject.ratio(before);
    assert(denied.error == -EIO && !denied.verified && denied.response.firmwareStatus == 3 && !reject.device->mutationCount);
    Test ignored; const auto previous = ignored.read(); ignored.device->discardChange = true;
    const auto out = ignored.ratio(previous);
    assert(out.error == -EIO && !out.verified && out.stage == IntelOcStage::Verify && ignored.device->mutationCount == 1);
    Test corrupted; const auto initial = corrupted.read();
    // A correct low byte is insufficient: firmware must preserve voltage too.
    bool changed = false;
    corrupted.device->afterRequest = [&] {
        if (!changed && corrupted.device->mutationCount == 1 && corrupted.device->commands.back() == 0x11) {
            corrupted.device->settings[0] ^= 1u << 20;
            changed = true;
        }
    };
    assert(!corrupted.ratio(initial).verified);
}
void ratioGuardsAndCancellation() {
    Test stale; const auto old = stale.read(); stale.device->settings[0] ^= 1u << 31;
    assert(stale.ratio(old).error == -EAGAIN && !stale.device->mutationCount);
    Test locked; const auto unlocked = locked.read(); locked.device->locked = true;
    assert(locked.ratio(unlocked).error == -EPERM && !locked.device->mutationCount);
    Test invalid; auto snapshot = invalid.read(); snapshot.valid = false; invalid.device->clearTrace();
    assert(invalid.ratio(snapshot).error == -EINVAL && !invalid.device->calls);
    Test t; const auto before = t.read(); t.device->clearTrace(); std::atomic<bool> cancelled{false};
    t.device->afterRequest = [&] { if (t.device->requests.size() == 4) cancelled.store(true); };
    IntelOcUpdate out;
    assert(t.service.transaction([&](HardwareSession &s) { out = applyIntelOcRatio(s, before, 59); return out.error; }, 1000, &cancelled) == -ECANCELED);
    assert(!out.verified && !out.writeAttempted && !t.device->mutationCount);
    Test busy; const auto original = busy.read(); busy.device->busyCommand = 0x11;
    const auto pending = busy.ratio(original);
    assert(pending.error == -ETIMEDOUT && pending.writeAttempted && !pending.verified && busy.device->mutationCount == 1);
}
void voltageEncoding() {
    for (const auto mode : {IntelOcVoltageMode::Adaptive, IntelOcVoltageMode::Override}) {
        for (unsigned mv = 1; mv <= 2000; ++mv) {
            for (const auto old : {UINT32_C(0), UINT32_MAX, UINT32_C(0xf3512345), UINT32_C(0x012abcde)}) {
                std::uint32_t encoded = 0;
                assert(!encodeIntelOcVoltage(mv, mode, old, encoded));
                assert(intelOcVoltageMode(encoded) == mode);
                assert((encoded & 0xffe000ffu) == (old & 0xffe000ffu));
                assert(std::abs(intelOcTargetMillivolts(encoded) - mv) <= 500.0 / 1024.0);
            }
        }
    }
    std::uint32_t value = 0;
    assert(!encodeIntelOcVoltage(1234, IntelOcVoltageMode::Override, 0xf3512345, value) && value == 0xf354f045);
    assert(intelOcTargetMillivolts(value) == 1234.375);
    assert(!encodeIntelOcVoltage(2000, IntelOcVoltageMode::Adaptive, 0, value) && value == 0x80000);
    for (unsigned invalid : {0u, 2001u, 4096u, UINT32_MAX}) {
        value = 7;
        assert(encodeIntelOcVoltage(invalid, IntelOcVoltageMode::Adaptive, 0, value) == -ERANGE && value == 7);
        Test t; const auto old = t.read(); t.device->clearTrace();
        assert(t.voltage(old, invalid).error == -ERANGE && !t.device->calls);
    }
    for (const auto mode : {IntelOcVoltageMode(-1), IntelOcVoltageMode(2)}) {
        value = 7;
        assert(encodeIntelOcVoltage(1000, mode, 0, value) == -EINVAL && value == 7);
        Test t; const auto old = t.read(); t.device->clearTrace();
        assert(t.voltage(old, 1000, mode).error == -EINVAL && !t.device->calls);
    }
}
void voltagePreservesAndNoop() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) {
        for (const auto mode : {IntelOcVoltageMode::Adaptive, IntelOcVoltageMode::Override}) {
            Test t; const auto old = t.read(domain); const auto other = t.device->settings[2 - unsigned(domain)];
            t.device->clearTrace(); const auto out = t.voltage(old, 1234, mode);
            assert(!out.error && out.verified && out.writeAttempted && out.stage == IntelOcStage::Complete);
            assert((out.submitted & 0xffe000ffu) == (old.response.data & 0xffe000ffu));
            assert(intelOcVoltageMode(out.readback.response.data) == mode && intelOcTargetMillivolts(out.readback.response.data) == 1234.375);
            assert(t.device->settings[2 - unsigned(domain)] == other && !t.device->wrongCpu && t.device->mutationCount == 1);
            assert(t.device->commands == std::vector<unsigned>({0x10, 0x11, 0x10}));
            t.device->clearTrace(); const auto same = t.voltage(out.readback, 1234, mode);
            assert(!same.error && same.verified && !same.writeAttempted && t.device->mutationCount == 1);
            assert(t.device->commands == std::vector<unsigned>{0x10});
            const auto offset = t.apply(same.readback); const auto ratio = t.ratio(offset.readback);
            assert(offset.verified && ratio.verified && (ratio.submitted & 0x1fff00u) == (out.submitted & 0x1fff00u));
        }
    }
}
void voltageFailuresAndReadback() {
    Test baseline; const auto old = baseline.read(); baseline.device->clearTrace(); assert(baseline.voltage(old).verified);
    for (unsigned index = 0; index < baseline.device->calls; ++index) {
        Test t; const auto before = t.read(); t.device->clearTrace(); t.device->failAt = int(index);
        const auto out = t.voltage(before);
        assert(out.error == -EACCES && !out.verified && t.device->calls == index + 1);
        unsigned attempts = 0;
        for (const auto &r : t.device->requests) if (r.write && ((r.value >> 32) & 255) == 0x11) ++attempts;
        assert(attempts <= 1 && out.writeAttempted == (attempts != 0));
    }
    Test reject; const auto before = reject.read(); reject.device->failCommand = 0x11; reject.device->status = 3;
    const auto denied = reject.voltage(before);
    assert(denied.error == -EIO && !denied.verified && denied.response.firmwareStatus == 3 && !reject.device->mutationCount);
    Test ignored; const auto previous = ignored.read(); ignored.device->discardChange = true;
    const auto out = ignored.voltage(previous);
    assert(out.error == -EIO && !out.verified && out.stage == IntelOcStage::Verify && ignored.device->mutationCount == 1);
    // Verify every bit: correct target/mode alone cannot certify preservation.
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto initial = t.read(); bool changed = false;
        t.device->afterRequest = [&] {
            if (!changed && t.device->mutationCount == 1) { t.device->settings[0] ^= 1u << bit; changed = true; }
        };
        const auto mismatch = t.voltage(initial);
        assert(mismatch.error == -EIO && !mismatch.verified && mismatch.stage == IntelOcStage::Verify && t.device->mutationCount == 1);
    }
}
void voltageGuards() {
    Test stale; const auto old = stale.read(); stale.device->settings[0] ^= 1u << 31;
    assert(stale.voltage(old).error == -EAGAIN && !stale.device->mutationCount);
    Test locked; const auto unlocked = locked.read(); locked.device->locked = true;
    assert(locked.voltage(unlocked).error == -EPERM && !locked.device->mutationCount);
    const auto nowLocked = locked.read(); locked.device->clearTrace();
    assert(locked.voltage(nowLocked).error == -EPERM && !locked.device->calls);
    Test invalid; auto snapshot = invalid.read(); snapshot.valid = false; invalid.device->clearTrace();
    assert(invalid.voltage(snapshot).error == -EINVAL && !invalid.device->calls);
    Test changed; const auto client = changed.read(); changed.device->model = 0x8f;
    assert(changed.voltage(client).error == -ENOTSUP && !changed.device->mutationCount);
    Test busy; const auto original = busy.read(); busy.device->busyCommand = 0x11;
    const auto pending = busy.voltage(original);
    assert(pending.error == -ETIMEDOUT && pending.writeAttempted && !pending.verified && busy.device->mutationCount == 1);
}
void voltageCancellationAndDeadline() {
    for (bool timeout : {false, true}) {
        for (unsigned boundary : {4u, 6u, 11u}) {
            Test t; const auto before = t.read(); t.device->clearTrace(); std::atomic<bool> cancelled{false};
            t.device->afterRequest = [&] {
                if (t.device->requests.size() == boundary) {
                    if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    else cancelled.store(true);
                }
            };
            IntelOcUpdate out;
            const int error = t.service.transaction([&](HardwareSession &s) {
                out = applyIntelOcVoltage(s, before, 1234, IntelOcVoltageMode::Override); return out.error;
            }, timeout ? 10 : 1000, &cancelled);
            assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && out.error == error && !out.verified);
            assert(out.writeAttempted == (boundary > 4) && t.device->mutationCount == unsigned(boundary > 4));
        }
    }
}
void vfDomainsAndSelection() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) {
        Test t; t.device->locked = true; const auto all = t.vf(0, domain);
        assert(!all.error && all.scanCompleted && all.points.size() == 15 && all.selectedPoint == 0);
        assert(all.cpu == 130 && all.domain == domain && !t.device->wrongCpu && !t.device->mutationCount);
        for (unsigned point = 1; point <= 15; ++point) {
            const auto &r = all.points[point - 1];
            assert(r.point == point && r.response.completed && !r.response.error);
            assert(r.response.data == t.device->vfSettings[unsigned(domain)][point]);
            const std::uint64_t expected = UINT64_C(0x8000001000000000) | (std::uint64_t(point) << 48) | (std::uint64_t(unsigned(domain)) << 40);
            assert(t.device->requests[(point - 1) * 3 + 1].value == expected);
            const auto one = t.vf(point, domain);
            assert(!one.error && one.scanCompleted && one.points.size() == 1 && one.points[0].point == point);
            assert(one.points[0].response.data == r.response.data);
        }
        for (const auto &r : t.device->requests) {
            assert(r.address == 0x150);
            if (r.write) assert(((r.value >> 32) & 255) == 0x10 && std::uint32_t(r.value) == 0);
        }
    }
}
void vfRejectionsBeforeIo() {
    for (unsigned point : {16u, 255u, 256u, UINT32_MAX}) {
        Test t; assert(t.vf(point).error == -EINVAL && !t.device->calls);
    }
    Test invalid; assert(invalid.vf(1, IntelOcDomain(1)).error == -EINVAL && !invalid.device->calls);
    for (unsigned model : {0x8fu, 0xadu, 0x97u, 0xcfu}) {
        Test t; t.device->model = model; const auto r = t.vf();
        assert(r.error == -ENOTSUP && r.points.empty() && !r.scanCompleted && t.device->requests.empty());
    }
    Test amd; amd.device->amd = true; assert(amd.vf().error == -ENOTSUP && amd.device->requests.empty());
    Test msr; msr.device->msr = false; assert(msr.vf().error == -ENOTSUP && msr.device->requests.empty());
}
void vfFirmwareHoles() {
    for (unsigned point : {1u, 8u, 15u}) {
        Test t; t.device->failVfPoint = point;
        const auto r = t.vf(); assert(r.error == -EIO && r.scanCompleted && r.points.size() == 15);
        for (const auto &row : r.points) {
            assert(row.response.completed && row.response.commandAttempted);
            if (row.point == point) assert(row.response.error == -EIO && row.response.firmwareStatus == 0xfe && !row.response.data);
            else assert(!row.response.error && row.response.data == t.device->vfSettings[0][row.point]);
        }
        assert(!t.device->mutationCount && t.device->commands.size() == 15);
    }
}
void vfTransportFailures() {
    Test base; assert(!base.vf().error); const unsigned calls = base.device->calls;
    for (unsigned failure = 0; failure < calls; ++failure) {
        Test t; t.device->failAt = int(failure); const auto out = t.vf();
        assert(out.error == -EACCES && !out.scanCompleted && t.device->calls == failure + 1);
        assert(!t.device->mutationCount);
        if (!out.points.empty()) {
            const auto &r = out.points.back().response;
            assert(r.error == -EACCES && !r.completed && !r.data);
            for (std::size_t i = 0; i + 1 < out.points.size(); ++i) assert(!out.points[i].response.error);
        }
    }
}
void vfBusyBounds() {
    Test initial; initial.device->busyBefore = true;
    const auto r = initial.vf(); assert(r.error == -ETIMEDOUT && !r.scanCompleted && r.points.size() == 1);
    assert(!r.points[0].response.commandAttempted && initial.device->commands.empty() && initial.device->requests.size() == 100);
    Test completion; completion.device->busyCommand = 0x10;
    const auto c = completion.vf(); assert(c.error == -ETIMEDOUT && !c.scanCompleted && c.points.size() == 1);
    assert(c.points[0].response.commandAttempted && !c.points[0].response.data && completion.device->requests.size() == 102);
}
void vfCancellationAndDeadline() {
    for (bool timeout : {false, true}) {
        Test t; std::atomic<bool> cancelled{false};
        // Even cancellation/timeout on the last completion read must not
        // publish that response or start the next point's query.
        t.device->afterRequest = [&] {
            if (t.device->requests.size() == 3) {
                if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                else cancelled.store(true);
            }
        };
        IntelVfSnapshot out;
        const int error = t.service.transaction([&](HardwareSession &s) { out = readIntelVf(s, 130, IntelOcDomain::Core); return out.error; }, timeout ? 10 : 1000, &cancelled);
        assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && out.error == error && !out.scanCompleted);
        assert(out.points.size() == 1 && !out.points[0].response.completed && !out.points[0].response.data);
        assert(t.device->commands.size() == 1 && !t.device->mutationCount);
    }
}
}
int main() {
    encoding(); readDomains(); identityRejection(); readFailures(); applyPreservesAndNoop();
    staleAndLock(); applyFailures(); firmwareAndReadback(); waitBounds(); cancellation();
    ratioEncoding(); ratioPreservesVoltageAndNoop(); ratioFailurePositionsAndReadback(); ratioGuardsAndCancellation();
    voltageEncoding(); voltagePreservesAndNoop(); voltageFailuresAndReadback(); voltageGuards(); voltageCancellationAndDeadline();
    vfDomainsAndSelection(); vfRejectionsBeforeIo(); vfFirmwareHoles(); vfTransportFailures(); vfBusyBounds(); vfCancellationAndDeadline();
    std::cout << "25 Intel OC / VF scenario groups passed\n";
}
