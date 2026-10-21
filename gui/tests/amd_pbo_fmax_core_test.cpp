// SPDX-License-Identifier: GPL-2.0-only
#include "amd_pbo.h"
#include "amd_pm.h"
#include "amd_pbo_fixture.h"
#include <cassert>
#include <iostream>
using namespace octool::core;
namespace {
struct Test {
    AmdPboFixture *fake = new AmdPboFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(fake)};
    SmuTarget target;
    Test() { target.cpu = 130; target.profile = SmuProfile::Shimada; }
    AmdPboCapabilities read() {
        AmdPboCapabilities out;
        const auto e = service.transaction([&](HardwareSession &s) { out = readShimadaPboCapabilities(s,target); return out.error; }, 10000);
        if (e) { out.error = e; out.valid = false; } return out;
    }
    AmdPboFmaxUpdate set(unsigned mhz = 5000, std::uint32_t caps = 4, const std::atomic<bool> *cancel = nullptr, int timeout = 10000) {
        AmdPboFmaxUpdate out;
        const auto e = service.transaction([&](HardwareSession &s) { out = setShimadaPboFmax(s,target,caps,mhz); return out.error; }, timeout, cancel);
        if (e) out.error = e;
        return out;
    }
};
void eligibility() {
    for (unsigned mode = 0; mode < 8; ++mode) {
        Test t;
        if (mode == 0) t.target.profile = SmuProfile::Phoenix;
        if (mode == 1) t.target.bus = 1;
        if (mode == 2) t.target.device = 1;
        if (mode == 3) t.target.function = 1;
        if (mode == 4) t.fake->amd = false;
        if (mode == 5) t.fake->model = 0x18;
        if (mode == 6) t.fake->family = 0x19;
        if (mode == 7) t.fake->pciId ^= 0x10000;
        assert(t.set().error && !t.fake->commands && !t.fake->settings);
    }
    for (unsigned value : {0u,0x100000u,UINT32_MAX}) { Test t; assert(t.set(value).error == -ERANGE && !t.fake->calls); }
    for (unsigned value : {0u,1u,2u,3u,5u,UINT32_MAX}) { Test t; assert(t.set(5000,value).error == -ENOTSUP && !t.fake->calls); }
}
void acknowledgementOnly() {
    for (unsigned mhz : {1u,4999u,5000u,0xfffffu}) {
        Test t; t.fake->discardChange = true;
        const auto r = t.set(mhz);
        assert(!r.error && r.accepted && r.writeAttempted && !r.verified && r.phase == AmdPboPhase::Complete);
        assert(r.requestedMHz == mhz && r.before.valid && r.before.raw == 4 && r.response == 1);
        assert(!t.fake->wrongCpu && !t.fake->fmaxMHz && t.fake->settings == 1 && t.fake->argumentReads == 1);
        assert((t.fake->messages == std::vector<std::uint32_t>{0x6f,0x70}));
        assert(t.fake->commandArgs[0][0] == 0 && t.fake->commandArgs[1][0] == mhz);
        for (const auto &args : t.fake->commandArgs) for (unsigned n = 1; n < 6; ++n) assert(!args[n]);
    }
    Test unknown; unknown.fake->capsRaw = 0x80000004;
    const auto caps = unknown.read(); assert(caps.valid && caps.raw == 0x80000004 && amdPboFmaxAvailable(caps.raw));
}
void changedCapabilityAndIdentity() {
    for (std::uint32_t raw : {0u,5u,6u,0x80000004u}) {
        Test t; t.fake->capsRaw = raw; const auto r = t.set();
        assert(r.error == -EAGAIN && !r.writeAttempted && !t.fake->settings && r.before.raw == raw);
    }
    Test t; t.fake->afterRequest = [&] { if (t.fake->argumentReads) t.fake->model = 0x18; };
    assert(t.set().error == -ENOTSUP && !t.fake->settings);
}
void rejections() {
    for (std::uint32_t status : {0u,2u,0xfcu,0xfdu,0xfeu,UINT32_MAX}) {
        Test t; t.fake->writeCompletion = status; const auto r = t.set();
        assert(r.error == (status ? -EIO : -ETIMEDOUT) && r.writeAttempted && !r.accepted && !r.verified);
        assert(t.fake->settings == 1 && t.fake->commands == 2);
        Test q; q.fake->completion = status; assert(q.set().error && !q.fake->settings && q.fake->commands == 1);
    }
    Test busy; busy.fake->busyBefore = true;
    assert(busy.set().error == -ETIMEDOUT && !busy.fake->commands);
}
void faultsAndCancellation() {
    Test base; assert(base.set().accepted);
    for (unsigned n = 0; n < base.fake->calls; ++n) {
        Test t; t.fake->failAt = int(n); const auto r = t.set();
        assert(r.error == -EACCES && !r.verified && t.fake->calls == n+1 && t.fake->settings <= 1);
    }
    for (unsigned n = 0; n <= base.fake->calls; ++n) {
        Test t; std::atomic<bool> cancel{n == 0};
        t.fake->afterRequest = [&] { if (t.fake->calls == n) cancel.store(true); };
        const auto r = t.set(5000,4,&cancel);
        assert(r.error == -ECANCELED && !r.verified && t.fake->calls == n && t.fake->settings <= 1);
    }
    Test deadline; deadline.fake->busyBefore = true;
    assert(deadline.set(5000,4,nullptr,1).error == -ETIMEDOUT && !deadline.fake->settings);
}
void railsEncoding() {
    const auto none = encodeLegacyAmdPboRails(0,0); assert(!none.error && none.empty && !none.argument);
    for (unsigned mv = 250; mv <= 2800; mv += 5) {
        const auto r = encodeLegacyAmdPboRails(mv,0), b = encodeLegacyAmdPboRails(0,mv);
        assert(!r.error && r.mirrored && !r.empty && r.rail0Millivolts == mv && r.rail1Millivolts == mv);
        assert(r.argument == b.argument && (r.argument & 511) * 5 + 245 == mv && (r.argument >> 17) * 5 + 245 == mv);
    }
    for (unsigned bad : {1u,244u,245u,249u,251u,2801u,UINT32_MAX}) {
        assert(encodeLegacyAmdPboRails(bad,1000).error == -ERANGE);
        assert(encodeLegacyAmdPboRails(1000,bad).error == -ERANGE);
    }
    assert(encodeLegacyAmdPboRails(700,1250).argument == (91u | (201u << 17)));
}
void layoutAudit() {
    const auto small = auditLegacyAmdPmLayout(0x730204,2844,2);
    assert(!small.error && small.coreSlots == 16 && small.ccdSlots == 2 && small.outsideWords == 22);
    assert(small.ranges.size() == 26 && small.ranges.front().first == 0x634 && small.ranges[15].first == 0x1fb4);
    const auto large = auditLegacyAmdPmLayout(0x731004,11636,16);
    assert(!large.error && !large.outsideWords && large.overlappingWords == 176 && large.publicFieldConflicts == 2);
    const auto six = auditLegacyAmdPmLayout(0x730604,5356,6);
    assert(six.constructorFallback && six.coreSlots == 16 && six.ccdSlots == 2);
    assert(auditLegacyAmdPmLayout(0x730204,0,2).outsideWords == 262);
    assert(auditLegacyAmdPmLayout(0x730204,2843,2).error == -EMSGSIZE);
    assert(auditLegacyAmdPmLayout(0x730205,2844,2).error == -ENOTSUP);
    for (unsigned n : {0u,17u,UINT32_MAX}) assert(auditLegacyAmdPmLayout(0x730204,2844,n).error == -EINVAL);
    for (const auto &layout : amdPmLayouts()) for (unsigned n = 1; n <= 16; ++n) {
        const auto r = auditLegacyAmdPmLayout(layout.version,layout.bytes,n); assert(!r.error && r.ranges.size() == 26);
        assert(amdPmLayoutAuditJson(r).find("\"decoder_enabled\":false") != std::string::npos);
    }
}
}
int main() {
    eligibility(); acknowledgementOnly(); changedCapabilityAndIdentity(); rejections(); faultsAndCancellation(); railsEncoding(); layoutAudit();
    std::cout << "7 AMD Fmax, rails and PM layout scenario groups passed\n";
}
