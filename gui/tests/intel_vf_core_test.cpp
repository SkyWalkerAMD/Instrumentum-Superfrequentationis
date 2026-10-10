// SPDX-License-Identifier: GPL-2.0-only
#include "intel_oc.h"
#include "intel_oc_fixture.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelVfEditFixture *device;
    HardwareService service;
    Test() : device(new IntelVfEditFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelVfEditSnapshot read(unsigned point = 8, IntelOcDomain domain = IntelOcDomain::Core) {
        IntelVfEditSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelVfEdit(s, 130, domain, point); return out.error; });
        assert(error == out.error); return out;
    }
    IntelVfUpdate apply(const IntelVfEditSnapshot &old, double value = -50) {
        IntelVfUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelVfOffset(s, old, value); return out.error; });
        assert(error == out.error); return out;
    }
};
unsigned settingsAttempts(const IntelOcFixture &d) {
    return unsigned(std::count_if(d.requests.begin(), d.requests.end(), [](const Request &r) {
        return r.write && ((r.value >> 32) & 255) == 0x11;
    }));
}
void readContext() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) for (unsigned p = 1; p <= 15; ++p) {
        Test t; const auto out = t.read(p, domain);
        assert(!out.error && out.valid && intelVfEditable(out) && intelVfDomainDefault(out));
        assert(out.cpu == 130 && out.point == p && out.domain == domain);
        assert(out.value.data == t.device->vfSettings[unsigned(domain)][p]);
        assert(out.control.data == t.device->control && out.legacy.data == t.device->settings[unsigned(domain)]);
        assert(out.flexRatio == t.device->flexExtra && !t.device->wrongCpu);
        assert((t.device->commands == std::vector<unsigned>{0x14, 0x10, 0x10}) && !settingsAttempts(*t.device));
    }
}
void inputAndIdentityGates() {
    for (unsigned point : {0u, 16u, UINT32_MAX}) {
        Test t; assert(t.read(point).error == -EINVAL && !t.device->calls);
    }
    Test invalid; assert(invalid.read(1, IntelOcDomain(1)).error == -EINVAL && !invalid.device->calls);
    for (unsigned model : {0x97, 0xba, 0x8f, 0xcf, 0xad, 0}) {
        Test t; t.device->model = model; assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        Test t; if (mode) t.device->msr = false; else t.device->amd = true;
        assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    Test t; const auto old = t.read(); const auto calls = t.device->calls;
    for (double value : {-1000.01, 1000.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        assert(t.apply(old, value).error == -ERANGE && t.device->calls == calls);
    for (unsigned mode = 0; mode < 8; ++mode) {
        auto bad = old;
        if (mode == 0) bad.valid = false;
        if (mode == 1) bad.error = -EIO;
        if (mode == 2) bad.point = 0;
        if (mode == 3) bad.control.completed = false;
        if (mode == 4) bad.legacy.firmwareStatus = 1;
        if (mode == 5) bad.value.error = -EACCES;
        if (mode == 6) bad.identity.model = 0x8f;
        if (mode == 7) bad.domain = IntelOcDomain(1);
        assert(t.apply(bad).error == -EINVAL && t.device->calls == calls);
    }
}
void permissionPolicy() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Test t;
        if (mode == 0) t.device->locked = true;
        if (mode == 1) t.device->control |= 8;
        if (mode == 2) t.device->settings[0] |= 1u << 20; // Override voltage mode.
        if (mode == 3) t.device->settings[0] |= 1u << 21; // Global offset.
        if (mode == 4) t.device->settings[0] |= 1u << 8;  // Target voltage.
        const auto old = t.read(); const auto calls = t.device->calls;
        assert(!old.error && old.valid && !intelVfEditable(old));
        assert(t.apply(old).error == -EPERM && t.device->calls == calls && !settingsAttempts(*t.device));
    }
    Test cache; cache.device->control |= 8;
    const auto old = cache.read(8, IntelOcDomain::Cache);
    assert(intelVfEditable(old) && cache.apply(old).verified);
    assert(cache.device->control == old.control.data); // Core's scope is not a cache setting.
}
void encodingAndEveryPoint() {
    for (const auto domain : {IntelOcDomain::Core, IntelOcDomain::Cache}) for (unsigned p = 1; p <= 15; ++p) {
        Test t; const auto old = t.read(p, domain);
        const auto out = t.apply(old);
        assert(!out.error && out.verified && out.writeAttempted && !out.unchanged && out.stage == IntelOcStage::Complete);
        assert(!t.device->wrongCpu && settingsAttempts(*t.device) == 1 && t.device->mutationCount == 1);
        assert(out.submitted == UINT32_C(0xf9a00000)); // -51 / 1024 V, nearest to -50 mV.
        assert(out.expected == (old.value.data & 0x1fffff) + out.submitted && out.readback.value.data == out.expected);
        assert(out.readback.legacy.data == old.legacy.data && out.readback.control.data == old.control.data);
        assert(std::find(t.device->commands.begin(), t.device->commands.end(), 0x15u) == t.device->commands.end());
        for (const auto &r : t.device->requests) if (r.write && ((r.value >> 32) & 255) == 0x11) {
            assert(((r.value >> 40) & 255) == unsigned(domain) && ((r.value >> 48) & 255) == p);
            assert((r.value & 0x1fffff) == 0);
        }
        IntelVfEditFixture untouched;
        for (unsigned d : {0u, 2u}) for (unsigned i = 1; i <= 15; ++i)
            if (d != unsigned(domain) || i != p) assert(t.device->vfSettings[d][i] == untouched.vfSettings[d][i]);
    }
    for (unsigned code = 0; code < 2048; ++code) {
        Test t; const auto old = t.read();
        const int signedCode = code < 1024 ? int(code) : int(code) - 2048;
        const auto out = t.apply(old, signedCode * (1000.0 / 1024.0));
        assert(out.verified && out.submitted == (code << 21));
        assert((out.readback.value.data & 0x1fffff) == (old.value.data & 0x1fffff));
    }
}
void unchangedAndStale() {
    Test t; const auto old = t.read();
    const auto noOp = t.apply(old, intelOcOffsetMillivolts(old.value.data));
    assert(!noOp.error && noOp.verified && noOp.unchanged && !noOp.writeAttempted && !settingsAttempts(*t.device));
    for (unsigned mode = 0; mode < 7; ++mode) {
        Test x; const auto before = x.read();
        if (mode == 0) x.device->vfSettings[0][8] ^= 1;
        if (mode == 1) x.device->control ^= 1;
        if (mode == 2) x.device->settings[0] ^= 1;
        if (mode == 3) x.device->flexExtra ^= UINT64_C(1) << 40;
        if (mode == 4) x.device->locked = true;
        if (mode == 5) x.device->control |= 8;
        if (mode == 6) x.device->settings[0] |= 1u << 21;
        assert(x.apply(before).error == (mode < 4 ? -EAGAIN : -EPERM));
        assert(!settingsAttempts(*x.device));
    }
}
void transportFailures() {
    Test reference; const auto old = reference.read();
    const unsigned initialCalls = reference.device->calls;
    assert(reference.apply(old).verified);
    const unsigned applyCalls = reference.device->calls - initialCalls;
    assert(initialCalls == 13 && applyCalls == 29);
    for (unsigned i = 0; i < initialCalls; ++i) {
        Test t; t.device->failAt = int(i); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !intelVfEditable(out) && !settingsAttempts(*t.device));
    }
    for (unsigned i = 0; i < applyCalls; ++i) {
        Test t; const auto snapshot = t.read(); t.device->failAt = int(t.device->calls + i);
        const auto out = t.apply(snapshot);
        assert(out.error == -EACCES && !out.verified);
        assert(out.writeAttempted == (i >= 14) && settingsAttempts(*t.device) == (i >= 14 ? 1u : 0u));
    }
}
void firmwareAndBusy() {
    for (unsigned command : {0x14u, 0x10u, 0x11u}) {
        Test t; const auto old = t.read(); t.device->failCommand = command; t.device->status = 0xfe;
        const auto out = t.apply(old);
        assert(out.error == -EIO && !out.verified && out.writeAttempted == (command == 0x11));
        assert(!t.device->mutationCount);
        if (command == 0x11) assert(out.response.completed && out.response.firmwareStatus == 0xfe);
    }
    Test unsupported; const auto old = unsupported.read(); unsupported.device->failVfPoint = 8;
    assert(unsupported.apply(old).error == -EIO && !settingsAttempts(*unsupported.device));
    for (unsigned command : {0u, 0x14u, 0x10u, 0x11u}) {
        Test t; const auto snapshot = t.read(); t.device->clearTrace();
        if (command) t.device->busyCommand = command; else t.device->busyBefore = true;
        const auto out = t.apply(snapshot);
        assert(out.error == -ETIMEDOUT && !out.verified && out.writeAttempted == (command == 0x11));
        assert(t.device->calls <= 130);
    }
}
void verifyPointAndContext() {
    for (unsigned bit = 0; bit < 32; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (r.write && ((r.value >> 32) & 255) == 0x11) t.device->vfSettings[0][8] ^= 1u << bit;
        };
        const auto out = t.apply(old);
        assert(out.error == -EIO && !out.verified && out.writeAttempted && settingsAttempts(*t.device) == 1);
    }
    for (unsigned mode = 0; mode < 6; ++mode) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (!r.write || ((r.value >> 32) & 255) != 0x11) return;
            if (mode == 0) t.device->control ^= 1;
            if (mode == 1) t.device->settings[0] ^= 1;
            if (mode == 2) t.device->flexExtra ^= UINT64_C(1) << 32;
            if (mode == 3) t.device->locked = true;
            if (mode == 4) t.device->model = 0x8f;
            if (mode == 5) { t.device->failCommand = 0x14; t.device->status = 3; }
        };
        const auto out = t.apply(old);
        assert(out.error && !out.verified && out.writeAttempted && settingsAttempts(*t.device) == 1);
    }
    Test ignored; const auto old = ignored.read(); ignored.device->discardChange = true;
    assert(ignored.apply(old).error == -EIO && settingsAttempts(*ignored.device) == 1);
}
void cancellationAndDeadline() {
    for (bool timeout : {false, true}) for (bool afterWrite : {false, true}) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false);
        t.device->afterRequest = [&] {
            const auto &r = t.device->requests.back();
            if (afterWrite ? !r.write || ((r.value >> 32) & 255) != 0x11 : r.address != 0x194) return;
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(25));
            else cancelled.store(true);
        };
        IntelVfUpdate out;
        const auto error = t.service.transaction([&](HardwareSession &s) {
            out = applyIntelVfOffset(s, old, -50); return out.error;
        }, timeout ? 10 : 1000, &cancelled);
        assert(error == (timeout ? -ETIMEDOUT : -ECANCELED) && !out.verified && out.writeAttempted == afterWrite);
        assert(settingsAttempts(*t.device) == (afterWrite ? 1u : 0u));
    }
}
}
int main() {
    readContext(); inputAndIdentityGates(); permissionPolicy(); encodingAndEveryPoint(); unchangedAndStale();
    transportFailures(); firmwareAndBusy(); verifyPointAndContext(); cancellationAndDeadline();
    std::cout << "9 Intel VF editing scenario groups passed\n";
}
