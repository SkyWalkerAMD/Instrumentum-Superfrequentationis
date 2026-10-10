// SPDX-License-Identifier: GPL-2.0-only
#include "amd_curve.h"
#include "amd_curve_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace octool::core;

namespace {
struct Test {
    AmdCurveFixture *fake = new AmdCurveFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; }
    AmdCurveReply read(unsigned ccd = 3, unsigned core = 7, int timeout = 10000, const std::atomic<bool> *cancel = nullptr) {
        AmdCurveReply out;
        const int error = service.transaction([&](HardwareSession &s) {
            out = readShimadaCurve(s, target, ccd, core); return out.error;
        }, timeout, cancel);
        if (error) { out.error = error; out.valid = false; out.raw = 0; }
        return out;
    }
};
void eligibility() {
    for (unsigned value : {16u, 256u, UINT32_MAX}) { Test t; assert(t.read(value, 0).error == -ERANGE && !t.fake->calls); }
    for (unsigned value : {8u, 15u, 256u, UINT32_MAX}) { Test t; assert(t.read(0, value).error == -ERANGE && !t.fake->calls); }
    for (SmuProfile profile : {SmuProfile::None, SmuProfile::Phoenix, SmuProfile::Gpt}) {
        Test t; t.target.profile = profile; assert(t.read().error == -ENOTSUP && !t.fake->calls);
    }
    for (unsigned field = 0; field < 3; ++field) {
        Test t; (field == 0 ? t.target.bus : field == 1 ? t.target.device : t.target.function) = 1;
        assert(t.read().error == -ENOTSUP && !t.fake->calls);
    }
    Test t; t.fake->amd = false; assert(t.read().error == -ENOTSUP && t.fake->requests.empty());
    t.fake->amd = true; t.fake->family = 0x19; assert(t.read().error == -ENOTSUP && t.fake->requests.empty());
    t.fake->family = 0x1a; t.fake->pciId = 0x153a8086;
    assert(t.read().error == -ENODEV && t.fake->requests.size() == 1 && !t.fake->requests[0].write);
}
void encodingsAndRawReturn() {
    for (unsigned ccd : {0u, 3u, 15u}) for (unsigned core : {0u, 7u}) {
        Test t; const auto out = t.read(ccd, core);
        assert(!out.error && out.valid && out.response == 1 && out.raw == 0xffffffe2 && out.messageAttempted);
        assert(t.fake->submitted == ((ccd << 28) | (core << 20)) && t.fake->commands == 1);
        assert(t.fake->argumentReads == 1 && !t.fake->wrongCpu);
        assert(t.fake->requests[1].address == 0xb8 && t.fake->requests[2].address == 0xbc && !t.fake->requests[2].write);
    }
    for (std::uint32_t value : {0u, 1u, 30u, 0x80000000u, UINT32_MAX}) {
        Test t; t.fake->returned = value; const auto out = t.read();
        assert(out.valid && out.raw == value);
    }
}
void faultEveryPosition() {
    Test baseline; assert(baseline.read().valid); const unsigned count = baseline.fake->calls;
    assert(count == 15);
    for (unsigned at = 0; at < count; ++at) {
        Test t; t.fake->failAt = int(at); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !out.raw && t.fake->calls == at + 1);
        assert(t.fake->commands <= 1);
    }
}
void firmwareResponses() {
    for (std::uint32_t status : {2u, 0xfcu, 0xfdu, 0xfeu, UINT32_MAX}) {
        Test t; t.fake->completion = status; const auto out = t.read();
        assert(out.error == -EIO && !out.valid && !out.raw && out.response == status && out.messageAttempted);
        assert(t.fake->commands == 1 && !t.fake->argumentReads);
    }
    Test t; t.fake->response = 0xfe; t.fake->busyPolls = 3;
    const auto out = t.read(); assert(out.valid && t.fake->polls == 4 && t.fake->commands == 1);
}
void boundedWaits() {
    Test before; before.fake->busyBefore = true;
    auto out = before.read(); assert(out.error == -ETIMEDOUT && !out.messageAttempted && !out.valid);
    assert(before.fake->polls == 100 && !before.fake->commands && !before.fake->argumentReads);
    for (const auto &r : before.fake->requests) assert(!r.write || r.address == 0xb8);
    Test after; after.fake->completion = 0;
    out = after.read(); assert(out.error == -ETIMEDOUT && out.messageAttempted && !out.valid);
    assert(after.fake->polls == 100 && after.fake->commands == 1 && !after.fake->argumentReads);
}
void cancellationAtEveryBoundary() {
    Test baseline; assert(baseline.read().valid);
    for (unsigned after = 1; after <= baseline.fake->calls; ++after) {
        Test t; std::atomic<bool> cancel{false};
        t.fake->afterRequest = [&] { if (t.fake->calls == after) cancel.store(true); };
        const auto out = t.read(3, 7, 10000, &cancel);
        assert(out.error == -ECANCELED && !out.valid && !out.raw && t.fake->calls == after);
    }
    Test t; std::atomic<bool> cancel{true}; assert(t.read(3, 7, 10000, &cancel).error == -ECANCELED && !t.fake->calls);
}
void deadlineAndFreshIdentity() {
    Test t; t.fake->busyBefore = true;
    const auto deadline = t.read(3, 7, 1);
    assert(deadline.error == -ETIMEDOUT && !deadline.valid && !t.fake->commands);
    t.fake->busyBefore = false; auto out = t.read(); assert(out.valid);
    t.fake->pciId = 0x14d81022; out = t.read();
    assert(out.error == -ENODEV && !out.valid && !out.raw && t.fake->commands == 1);
    // Expiry during the final read must not publish an otherwise valid value.
    Test late; late.fake->afterRequest = [&] {
        if (late.fake->argumentReads) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    };
    out = late.read(3, 7, 1000); assert(out.error == -ETIMEDOUT && !out.valid && !out.raw);
}
}
int main() {
    eligibility(); encodingsAndRawReturn(); faultEveryPosition(); firmwareResponses(); boundedWaits();
    cancellationAtEveryBoundary(); deadlineAndFreshIdentity();
    std::cout << "7 AMD curve query scenario groups passed\n";
}
