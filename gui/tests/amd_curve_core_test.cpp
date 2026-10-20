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
    AmdCurveSnapshot expected(unsigned mask = 0x80) {
        AmdCurveSnapshot out; out.ccd = 3; out.coreMask = mask; out.valid = true; out.raw.fill(0xffffffe2); return out;
    }
    AmdCurveUpdate set(const AmdCurveSnapshot &expected, int margin = -25, const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdCurveUpdate out; fake->writable = true;
        const int error = service.transaction([&](HardwareSession &s) {
            out = setShimadaCurves(s, target, expected, margin); return out.error;
        }, timeout, cancel);
        if (error) out.error = error;
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
void selectionAndEncoding() {
    unsigned mask = 123;
    for (const std::string value : {"", "8", "00", "0,", ",0", "0,,2", "2,0", "0,0", "0, 2", "-1", "0,1,2,3,4,5,6,7,8"})
        assert(!parseAmdCurveCores(value, mask) && !mask);
    assert(parseAmdCurveCores("0,2,7", mask) && mask == 0x85);
    assert(parseAmdCurveCores("0,1,2,3,4,5,6,7", mask) && mask == 255);
    std::uint32_t argument = 123;
    for (unsigned ccd : {0u, 15u}) for (unsigned core : {0u, 7u}) for (int margin : {-50,-1,0,1,50}) {
        assert(!encodeShimadaCurve(ccd, core, margin, argument));
        assert(argument == ((ccd << 28) | (core << 20) | (std::uint32_t(margin) & 0xffffu)));
        assert(!(argument & 0x0f0f0000u));
    }
    for (int margin : {-51,51,INT32_MIN,INT32_MAX}) assert(encodeShimadaCurve(0,0,margin,argument) == -ERANGE && !argument);
    assert(encodeShimadaCurve(16,0,0,argument) == -ERANGE && !argument);
    assert(encodeShimadaCurve(0,8,0,argument) == -ERANGE && !argument);
}
void updateEligibility() {
    for (unsigned mode = 0; mode < 11; ++mode) {
        Test t; auto expected = t.expected();
        if (mode == 0) expected.valid = false;
        if (mode == 1) expected.error = -EIO;
        if (mode == 2) expected.ccd = 16;
        if (mode == 3) expected.coreMask = 0;
        if (mode == 4) expected.coreMask = 256;
        if (mode == 5) expected.raw[7] = 51;
        if (mode == 6) expected.raw[7] = 0xffce; // unknown 16-bit return encoding
        if (mode == 7) expected.raw[7] = 0xffffffcd; // -51
        if (mode == 8) t.target.profile = SmuProfile::Phoenix;
        if (mode == 9) t.target.bus = 1;
        const auto r = t.set(expected, mode == 10 ? 51 : 0);
        assert(r.error && !r.attemptedMask && !t.fake->calls);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t;
        if (mode == 0) t.fake->family = 0x19;
        if (mode == 1) t.fake->model = 0x18;
        if (mode == 2) t.fake->pciId ^= 1;
        if (mode == 3) t.fake->amd = false;
        const auto r = t.set(t.expected()); assert(r.error && !r.attemptedMask && !t.fake->commands);
    }
}
void selectionReads() {
    for (unsigned mask : {0u,256u,UINT32_MAX,0x85u}) {
        Test t; t.fake->writable = true;
        AmdCurveSnapshot r;
        t.service.transaction([&](HardwareSession &s) { r = readShimadaCurves(s,t.target,3,mask); return r.error; });
        if (mask != 0x85) assert(r.error == -ERANGE && !r.valid && !t.fake->calls);
        else assert(r.valid && r.raw[0] == 0xffffffe2 && r.raw[2] == 0xffffffe2 && r.raw[7] == 0xffffffe2 && !r.raw[1] && t.fake->commands == 3);
    }
    Test t; t.fake->afterRequest = [&] { if (t.fake->argumentReads) t.fake->completion = 0xfe; };
    AmdCurveSnapshot r;
    t.service.transaction([&](HardwareSession &s) { r = readShimadaCurves(s,t.target,3,5); return r.error; });
    assert(r.error == -EIO && !r.valid && r.raw == (std::array<std::uint32_t,8>{}));
}
void updateSuccess() {
    for (int margin : {-50,-30,-1,0,50}) {
        Test t; const auto r = t.set(t.expected(0x85), margin);
        assert(!r.error && r.phase == AmdCurvePhase::Complete && r.core == 8 && r.verifiedMask == 0x85);
        assert(r.beforeMask == 0x85 && r.afterMask == 0x85 && !t.fake->wrongCpu);
        assert(r.attemptedMask == (margin == -30 ? 0u : 0x85u) && r.acceptedMask == r.attemptedMask);
        assert(r.unchangedMask == (margin == -30 ? 0x85u : 0u));
        assert(t.fake->settings == (margin == -30 ? 0u : 3u));
        for (unsigned core = 0; core < 8; ++core) assert(t.fake->margins[24+core] == ((0x85 & (1u<<core)) ? std::uint32_t(margin) : 0xffffffe2));
        for (std::size_t i = 0; i < t.fake->messages.size(); ++i) {
            assert(t.fake->messages[i] == 6 || t.fake->messages[i] == 0xa3);
            if (t.fake->messages[i] == 6) assert((t.fake->arguments[i] & 0x0f0fffffu) == (std::uint32_t(margin) & 0xffffu));
        }
    }
    Test full; assert(!full.set(full.expected(255), 20).error && full.fake->settings == 8);
}
void updateConflictsAndPartial() {
    Test stale; stale.fake->margins[26] = 1;
    auto r = stale.set(stale.expected(5));
    assert(r.error == -EAGAIN && r.phase == AmdCurvePhase::Preflight && r.core == 2 && !r.attemptedMask && !stale.fake->settings);
    Test changed; changed.fake->afterRequest = [&] {
        if (changed.fake->argumentReads == 2) changed.fake->margins[24] = 0;
    };
    r = changed.set(changed.expected(5));
    assert(r.error == -EAGAIN && r.phase == AmdCurvePhase::Recheck && !r.attemptedMask && !changed.fake->settings);
    Test partial; partial.fake->afterRequest = [&] { if (partial.fake->settings) partial.fake->margins[26] = 0; };
    r = partial.set(partial.expected(5));
    assert(r.error == -EAGAIN && r.core == 2 && r.verifiedMask == 1 && r.attemptedMask == 1 && partial.fake->settings == 1);
    assert(partial.fake->margins[24] == std::uint32_t(-25) && partial.fake->margins[26] == 0);
    Test identity; identity.fake->afterRequest = [&] { if (identity.fake->argumentReads == 2) identity.fake->model = 0x18; };
    r = identity.set(identity.expected()); assert(r.error == -ENOTSUP && !identity.fake->settings && !r.attemptedMask);
}
void updateResponsesAndReadback() {
    for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion = status; const auto r = t.set(t.expected());
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && r.phase == AmdCurvePhase::Write && r.response == status);
        assert(r.attemptedMask == 128 && !r.acceptedMask && !r.verifiedMask && !r.afterMask && t.fake->settings == 1);
    }
    Test ignored; ignored.fake->discardChange = true; auto r = ignored.set(ignored.expected());
    assert(r.error == -EIO && r.phase == AmdCurvePhase::Verify && r.afterMask == 128 && r.after[7] == 0xffffffe2);
    assert(r.acceptedMask == 128 && !r.verifiedMask && ignored.fake->settings == 1);
    Test failed; failed.fake->afterRequest = [&] { if (failed.fake->settings) failed.fake->completion = 0xfe; };
    r = failed.set(failed.expected()); assert(r.error == -EIO && r.phase == AmdCurvePhase::Verify && !r.afterMask && r.acceptedMask == 128);
    Test partial; partial.fake->afterRequest = [&] { if (partial.fake->settings) partial.fake->writeCompletion = 0xfe; };
    r = partial.set(partial.expected(0x85));
    assert(r.error == -EIO && r.core == 2 && r.attemptedMask == 5 && r.acceptedMask == 1 && r.verifiedMask == 1 && partial.fake->settings == 2);
}
void updateFaults() {
    Test baseline; assert(!baseline.set(baseline.expected()).error);
    for (unsigned at = 0; at < baseline.fake->calls; ++at) {
        Test t; t.fake->failAt = int(at); const auto r = t.set(t.expected());
        assert(r.error == -EACCES && t.fake->calls == at+1 && !r.verifiedMask && t.fake->settings <= 1);
        assert((r.acceptedMask & ~r.attemptedMask) == 0 && (r.afterMask & ~r.acceptedMask) == 0);
    }
}
void updateCancellation() {
    Test baseline; assert(!baseline.set(baseline.expected()).error);
    for (unsigned after = 0; after <= baseline.fake->calls; ++after) {
        Test t; std::atomic<bool> cancel{after == 0};
        t.fake->afterRequest = [&] { if (t.fake->calls == after) cancel.store(true); };
        const auto r = t.set(t.expected(), -25, &cancel);
        assert(r.error == -ECANCELED && t.fake->calls == after && !r.verifiedMask && t.fake->settings <= 1);
    }
    Test timeout; timeout.fake->writeCompletion = 0;
    const auto r = timeout.set(timeout.expected(), -25, nullptr, 100);
    assert(r.error == -ETIMEDOUT && timeout.fake->settings <= 1 && !r.verifiedMask);
}
}
int main() {
    eligibility(); encodingsAndRawReturn(); faultEveryPosition(); firmwareResponses(); boundedWaits();
    cancellationAtEveryBoundary(); deadlineAndFreshIdentity();
    selectionAndEncoding(); updateEligibility(); selectionReads(); updateSuccess(); updateConflictsAndPartial();
    updateResponsesAndReadback(); updateFaults(); updateCancellation();
    std::cout << "15 AMD curve scenario groups passed\n";
}
