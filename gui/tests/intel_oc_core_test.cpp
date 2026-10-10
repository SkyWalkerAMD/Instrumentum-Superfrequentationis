// SPDX-License-Identifier: GPL-2.0-only
#include "intel_oc.h"
#include "intel_oc_fixture.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

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
}
int main() {
    encoding(); readDomains(); identityRejection(); readFailures(); applyPreservesAndNoop();
    staleAndLock(); applyFailures(); firmwareAndReadback(); waitBounds(); cancellation();
    ratioEncoding(); ratioPreservesVoltageAndNoop(); ratioFailurePositionsAndReadback(); ratioGuardsAndCancellation();
    std::cout << "14 Intel OC scenario groups passed\n";
}
