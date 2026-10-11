// SPDX-License-Identifier: GPL-2.0-only
#include "intel_turbo.h"
#include "intel_turbo_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelTurboFixture *device;
    HardwareService service;
    Test() : device(new IntelTurboFixture), service(std::unique_ptr<HardwareBackend>(device)) {}
    IntelTurboSnapshot read(IntelTurboKind kind = IntelTurboKind::Primary) {
        IntelTurboSnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelTurbo(s, 130, kind); return out.error; });
        assert(error == out.error); return out;
    }
    IntelTurboUpdate apply(const IntelTurboSnapshot &old, unsigned group = 3, unsigned ratio = 56) {
        IntelTurboUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelTurboRatio(s, old, group, ratio); return out.error; });
        assert(error == out.error); return out;
    }
    IntelTurboUpdate threshold(const IntelTurboSnapshot &old, unsigned group = 3, unsigned count = 9) {
        IntelTurboUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelTurboThreshold(s, old, group, count); return out.error; });
        assert(error == out.error); return out;
    }
};
void thresholdEncoding() {
    IntelTurboSnapshot old; old.ratios = UINT64_C(0x3232323232323232);
    for (unsigned group = 0; group < 7; ++group) {
        old.coreCounts = 0;
        for (unsigned i = 0; i < 8; ++i) old.coreCounts |= std::uint64_t(i <= group ? i+1 : 248+i) << (8*i);
        for (unsigned value = 1; value <= 255; ++value) {
            std::uint64_t encoded = 123;
            const auto error = encodeIntelTurboThreshold(old, group, value, encoded);
            const bool allowed = value > group && value < 249+group;
            assert(bool(!error) == allowed && intelTurboThresholdEditable(old,group));
            if (!allowed) { assert(encoded == 123); continue; }
            const auto mask = UINT64_C(255) << (group*8);
            assert((encoded & ~mask) == (old.coreCounts & ~mask));
            assert(intelTurboByte(encoded,group) == value && validIntelTurboLayout(old.ratios,encoded));
        }
    }
    std::uint64_t encoded = 123;
    assert(encodeIntelTurboThreshold(old,7,255,encoded) == -EPERM && encoded == 123);
    for (unsigned bad : {0u,256u,UINT32_MAX}) assert(encodeIntelTurboThreshold(old,0,bad,encoded) == -ERANGE);
    for (unsigned bad : {8u,UINT32_MAX}) assert(encodeIntelTurboThreshold(old,bad,1,encoded) == -ERANGE && !intelTurboThresholdEditable(old,bad));
    old.coreCounts = 0x08000402; old.ratios = 0x10101010;
    assert(intelTurboThresholdEditable(old,1) && !intelTurboThresholdEditable(old,2) && !intelTurboThresholdEditable(old,3));
    assert(!encodeIntelTurboThreshold(old,1,7,encoded) && encoded == 0x08000702);
    assert(encodeIntelTurboThreshold(old,2,7,encoded) == -EINVAL);
    old.coreCounts = 0x0008; assert(!intelTurboThresholdEditable(old,0));
    old.coreCounts = 0x0204; assert(encodeIntelTurboThreshold(old,0,1,encoded) == -EINVAL);
}
void thresholdUpdates() {
    for (auto kind : {IntelTurboKind::Primary,IntelTurboKind::Secondary}) for (unsigned group = 0; group < 7; ++group) {
        Test t; t.device->regs[intelTurboCountMsr(kind)] = UINT64_C(0x100e0c0a08060402);
        const auto old = t.read(kind), original = old;
        const auto before = t.device->regs; const auto count = intelTurboByte(old.coreCounts,group)+1;
        const auto out = t.threshold(old,group,count);
        assert(out.verified && !out.error && out.writeAttempted && out.completed == 1 && !out.unchanged);
        assert(out.after.coreCounts == out.expected && out.after.ratios == original.ratios && !t.device->wrongCpu);
        for (const auto &entry : before) if (entry.first != intelTurboCountMsr(kind)) assert(t.device->regs[entry.first] == entry.second);
        assert(intelTurboByte(out.after.coreCounts,7) == 16);
        const auto again = t.threshold(out.after,group,count);
        assert(again.verified && again.unchanged && !again.writeAttempted && !again.completed && t.device->writes == 1);
    }
}
void thresholdGuards() {
    for (auto addr : {0xce,0x194,0x650,0x651}) for (unsigned bit = 0; bit < 64; ++bit) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary); t.device->regs[addr] ^= UINT64_C(1)<<bit;
        assert(t.threshold(old).error && !t.device->writes);
    }
    for (unsigned mode = 0; mode < 8; ++mode) {
        Test t; auto old = t.read(IntelTurboKind::Secondary);
        if (mode == 0) old.valid = false;
        if (mode == 1) old.error = -EIO;
        if (mode == 2) old.identity.model = 0x8f;
        if (mode == 3) old.kind = IntelTurboKind(9);
        if (mode == 4) old.platformInfo &= ~(UINT64_C(1)<<28);
        if (mode == 5) old.flexRatio |= UINT64_C(1)<<20;
        if (mode == 6) old.coreCounts = 0;
        if (mode == 7) old.ratios = 0;
        old.programmable = true; old.locked = false; old.layoutValid = true;
        const auto reads = t.device->reads, calls = t.device->cpuCalls;
        assert(t.threshold(old).error && !t.device->writes && reads == t.device->reads && calls == t.device->cpuCalls);
    }
    Test t; const auto old = t.read(IntelTurboKind::Secondary); const auto reads = t.device->reads;
    assert(t.threshold(old,7,15).error == -EPERM && t.threshold(old,3,10).error == -ERANGE && !t.device->writes && reads == t.device->reads);
    t.device->afterRequest = [&](const Request &r) { if (r.address == 0x651) t.device->regs[0x651] ^= UINT64_C(1)<<56; };
    assert(t.threshold(old).error == -EAGAIN && !t.device->writes);
}
void thresholdFaults() {
    for (unsigned at = 0; at < 9; ++at) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary); t.device->failRead = int(t.device->reads+at);
        const auto out = t.threshold(old); assert(out.error == -EACCES && !out.verified && t.device->writes == (at >= 5 ? 1u : 0u));
    }
    for (unsigned at = 0; at < 8; ++at) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary); t.device->failCpuid = int(t.device->cpuCalls+at);
        const auto out = t.threshold(old); assert(out.error == -EACCES && !out.verified && t.device->writes == (at >= 4 ? 1u : 0u));
    }
    Test t; const auto old = t.read(IntelTurboKind::Secondary); t.device->failWrite = 0;
    const auto out = t.threshold(old); assert(out.error == -EIO && out.writeAttempted && !out.completed && !out.verified && t.device->writes == 1);
}
void thresholdReadback() {
    for (auto kind : {IntelTurboKind::Primary,IntelTurboKind::Secondary}) for (unsigned bit = 0; bit < 64; ++bit) {
        Test t; t.device->regs[intelTurboCountMsr(kind)] = UINT64_C(0x100e0c0a08060402);
        const auto old = t.read(kind);
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[intelTurboCountMsr(kind)] ^= UINT64_C(1)<<bit; };
        const auto out = t.threshold(old); assert(out.error == -EIO && !out.verified && out.writeAttempted && t.device->writes == 1);
    }
    for (auto addr : {0xce,0x194,0x650}) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary);
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[addr] ^= UINT64_C(1)<<48; };
        assert(t.threshold(old).error == -EAGAIN && t.device->writes == 1);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary);
        t.device->discardChange = mode == 0;
        t.device->afterRequest = [&](const Request &r) { if (!r.write) return; if (mode == 1) t.device->model = 0x8f; if (mode == 2) t.device->hybrid = false; };
        assert(!t.threshold(old).verified && t.device->writes == 1);
    }
}
void thresholdCancellation() {
    for (bool afterWrite : {false,true}) {
        Test t; const auto old = t.read(IntelTurboKind::Secondary); std::atomic<bool> cancelled(false); IntelTurboUpdate out;
        t.device->afterRequest = [&](const Request &r) { if (afterWrite ? r.write : r.address == 0x651) cancelled.store(true); };
        const auto error = t.service.transaction([&](HardwareSession &s) { out = applyIntelTurboThreshold(s,old,3,9); return out.error; },2000,&cancelled);
        assert(error == -ECANCELED && !out.verified && out.writeAttempted == afterWrite && t.device->writes == (afterWrite ? 1u : 0u));
    }
    Test t; const auto old = t.read(IntelTurboKind::Secondary); IntelTurboUpdate out;
    t.device->afterRequest = [&](const Request &r) { if (r.write) std::this_thread::sleep_for(std::chrono::milliseconds(25)); };
    const auto error = t.service.transaction([&](HardwareSession &s) { out = applyIntelTurboThreshold(s,old,3,9); return out.error; },10);
    assert(error == -ETIMEDOUT && !out.verified && out.writeAttempted && t.device->writes == 1);
}
void encoding() {
    IntelTurboSnapshot old; old.coreCounts = UINT64_C(0x0807060504030201);
    // Every group and all accepted inputs; unrelated bytes survive exactly.
    for (unsigned group = 0; group < 8; ++group) for (unsigned ratio = 1; ratio <= 85; ++ratio) {
        old.ratios = 0;
        for (unsigned i = 0; i < 8; ++i) old.ratios |= std::uint64_t(i < group ? 85 : i > group ? 1 : 42) << (i * 8);
        std::uint64_t encoded = 0;
        assert(!encodeIntelTurboRatio(old, group, ratio, encoded));
        const auto mask = UINT64_C(255) << (group * 8);
        assert((encoded & ~mask) == (old.ratios & ~mask));
        assert(intelTurboByte(encoded, group) == ratio);
    }
    std::uint64_t encoded = 123;
    assert(encodeIntelTurboRatio(old, 8, 10, encoded) == -ERANGE && encoded == 123);
    assert(encodeIntelTurboRatio(old, 0, 0, encoded) == -ERANGE);
    assert(encodeIntelTurboRatio(old, 0, 86, encoded) == -ERANGE);
    assert(intelTurboByte(UINT64_MAX, 8) == 0 && intelTurboByte(UINT64_MAX, UINT32_MAX) == 0);
    assert(intelTurboRatioMsr(IntelTurboKind(9)) == 0 && intelTurboCountMsr(IntelTurboKind(9)) == 0);
}
void layout() {
    assert(validIntelTurboLayout(0x10200030, 0x04030001)); // Ignore zero-count hole.
    for (const auto counts : {UINT64_C(0), UINT64_C(0x0102), UINT64_C(0x0101)})
        assert(!validIntelTurboLayout(UINT64_C(0x2020), counts));
    assert(!validIntelTurboLayout(0x2120, 0x0201));
    assert(!validIntelTurboLayout(0x0020, 0x0201));
    Test t; auto old = t.read();
    assert(t.apply(old, 3, 60).error == -ERANGE && t.apply(old, 3, 50).error == -ERANGE);
    old.coreCounts &= ~(UINT64_C(255) << 24);
    assert(t.apply(old).error == -EINVAL && !t.device->writes);
}
void reads() {
    for (const auto kind : {IntelTurboKind::Primary, IntelTurboKind::Secondary}) {
        Test t; const auto out = t.read(kind);
        assert(!out.error && out.valid && out.layoutValid && out.programmable && !out.locked && out.hybrid);
        assert(out.cpu == 130 && out.kind == kind && !t.device->wrongCpu && t.device->requests.size() == 4);
        assert(out.ratios == t.device->regs[intelTurboRatioMsr(kind)] && out.coreCounts == t.device->regs[intelTurboCountMsr(kind)]);
        for (const auto &request : t.device->requests) assert(!request.write && request.space == Space::Msr && request.width == 8);
        assert(intelTurboByte(out.coreCounts, 7) == (kind == IntelTurboKind::Primary ? 8u : 16u));
    }
    Test t; t.device->regs[0x1ae] = 0; const auto out = t.read();
    assert(!out.error && out.valid && !out.layoutValid); // Raw read remains inspectable.
}
void gates() {
    for (unsigned model : {0x97, 0xba, 0xad, 0x8f, 0xcf, 0}) {
        Test t; t.device->model = model; assert(t.read().error == -ENOTSUP && t.device->requests.empty());
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Test t;
        if (mode == 0) t.device->amd = true;
        if (mode == 1) t.device->maximumLeaf = 6;
        if (mode == 2) t.device->hasMsr = false;
        if (mode == 3) t.device->hybrid = false;
        assert(t.read(IntelTurboKind::Secondary).error == -ENOTSUP && t.device->requests.empty());
    }
    Test t; t.device->hybrid = false; assert(t.read().valid);
    const auto calls = t.device->cpuCalls;
    assert(t.read(IntelTurboKind(9)).error == -EINVAL && t.device->cpuCalls == calls);
    for (unsigned mode = 0; mode < 2; ++mode) {
        Test x;
        if (mode) x.device->regs[0x194] |= UINT64_C(1) << 20;
        else x.device->regs[0xce] &= ~(UINT64_C(1) << 28);
        auto old = x.read(); const auto reads = x.device->reads;
        old.programmable = true; old.locked = false;
        assert(x.apply(old).error == -EPERM && x.device->reads == reads && !x.device->writes);
    }
}
void updateEveryGroup() {
    for (const auto kind : {IntelTurboKind::Primary, IntelTurboKind::Secondary}) for (unsigned group = 0; group < 8; ++group) {
        Test t; const auto original = t.device->regs;
        const auto old = t.read(kind); const unsigned ratio = intelTurboByte(old.ratios, group) - 1;
        const auto out = t.apply(old, group, ratio);
        assert(!out.error && out.verified && !out.unchanged && out.writeAttempted && out.completed == 1);
        assert(out.after.valid && out.after.ratios == out.expected && !t.device->wrongCpu);
        assert((old.ratios ^ out.expected) >> (group * 8) < 256);
        for (const auto &entry : original) if (entry.first != intelTurboRatioMsr(kind))
            assert(t.device->regs[entry.first] == entry.second);
        const auto writes = t.device->writes;
        const auto unchanged = t.apply(out.after, group, ratio);
        assert(!unchanged.error && unchanged.verified && unchanged.unchanged && !unchanged.writeAttempted);
        assert(unchanged.completed == 0 && t.device->writes == writes);
    }
}
void staleAndLocks() {
    for (const auto address : {0xce, 0x194, 0x1ad, 0x1ae}) {
        Test t; const auto old = t.read(); t.device->regs[address] ^= UINT64_C(1) << 48;
        assert(t.apply(old).error == -EAGAIN && !t.device->writes);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Test t; const auto old = t.read();
        if (mode == 0) t.device->regs[0x194] |= UINT64_C(1) << 20;
        if (mode == 1) t.device->regs[0xce] &= ~(UINT64_C(1) << 28);
        if (mode == 2) t.device->model = 0x8f;
        assert(t.apply(old).error && !t.device->writes);
    }
    Test t; const auto old = t.read();
    t.device->afterRequest = [&](const Request &r) { if (r.address == 0x1ae) t.device->regs[0x1ad] ^= UINT64_C(1) << 56; };
    assert(t.apply(old).error == -EAGAIN && !t.device->writes);
    auto invalid = old; invalid.valid = false;
    const auto requests = t.device->requests.size();
    assert(t.apply(invalid).error == -EINVAL && t.device->requests.size() == requests);
}
void faultPositions() {
    Test reference; const auto old = reference.read();
    const unsigned beforeReads = reference.device->reads, beforeCpu = reference.device->cpuCalls;
    assert(reference.apply(old).verified);
    const unsigned reads = reference.device->reads - beforeReads, calls = reference.device->cpuCalls - beforeCpu;
    assert(reads == 9 && calls == 8);
    for (unsigned at = 0; at < reads; ++at) {
        Test t; const auto snapshot = t.read(); t.device->failRead = int(t.device->reads + at);
        const auto out = t.apply(snapshot);
        assert(out.error == -EACCES && !out.verified && t.device->writes == (at >= 5 ? 1u : 0u));
    }
    for (unsigned at = 0; at < calls; ++at) {
        Test t; const auto snapshot = t.read(); t.device->failCpuid = int(t.device->cpuCalls + at);
        const auto out = t.apply(snapshot);
        assert(out.error == -EACCES && !out.verified && t.device->writes == (at >= 4 ? 1u : 0u));
    }
    for (unsigned at = 0; at < 4; ++at) {
        Test t; t.device->failRead = int(at); const auto out = t.read();
        assert(out.error == -EACCES && !out.valid && !t.device->writes);
        Test cp; cp.device->failCpuid = int(at); assert(!cp.read().valid && cp.device->requests.empty());
    }
    Test t; const auto snapshot = t.read(); t.device->failWrite = 0;
    const auto out = t.apply(snapshot);
    assert(out.error == -EIO && out.writeAttempted && !out.verified && !out.completed && t.device->writes == 1);
}
void readback() {
    for (unsigned bit = 0; bit < 64; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[0x1ad] ^= UINT64_C(1) << bit; };
        const auto out = t.apply(old); assert(out.error == -EIO && !out.verified && out.writeAttempted && t.device->writes == 1);
    }
    for (const auto address : {0xce, 0x194, 0x1ae}) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[address] ^= UINT64_C(1) << 48; };
        assert(t.apply(old).error == -EAGAIN && t.device->writes == 1);
    }
    Test t; const auto old = t.read(); t.device->discardChange = true;
    assert(t.apply(old).error == -EIO && t.device->writes == 1);
    Test model; const auto snapshot = model.read();
    model.device->afterRequest = [&](const Request &r) { if (r.write) model.device->model = 0x8f; };
    assert(!model.apply(snapshot).verified && model.device->writes == 1);
    Test hybrid; const auto secondary = hybrid.read(IntelTurboKind::Secondary);
    hybrid.device->afterRequest = [&](const Request &r) { if (r.write) hybrid.device->hybrid = false; };
    assert(!hybrid.apply(secondary, 3, 40).verified && hybrid.device->writes == 1);
}
void cancellation() {
    for (bool afterWrite : {false, true}) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false); IntelTurboUpdate out;
        t.device->afterRequest = [&](const Request &r) { if (afterWrite ? r.write : r.address == 0x1ae) cancelled.store(true); };
        const auto error = t.service.transaction([&](HardwareSession &s) {
            out = applyIntelTurboRatio(s, old, 3, 56); return out.error;
        }, 2000, &cancelled);
        assert(error == -ECANCELED && !out.verified && out.writeAttempted == afterWrite);
        assert(t.device->writes == (afterWrite ? 1u : 0u));
    }
    Test t; const auto old = t.read(); IntelTurboUpdate out;
    t.device->afterRequest = [&](const Request &r) { if (r.write) std::this_thread::sleep_for(std::chrono::milliseconds(25)); };
    const auto error = t.service.transaction([&](HardwareSession &s) {
        out = applyIntelTurboRatio(s, old, 3, 56); return out.error;
    }, 10);
    assert(error == -ETIMEDOUT && !out.verified && out.writeAttempted && t.device->writes == 1);
}
}
int main() {
    encoding(); layout(); reads(); gates(); updateEveryGroup(); staleAndLocks(); faultPositions(); readback(); cancellation();
    thresholdEncoding(); thresholdUpdates(); thresholdGuards(); thresholdFaults(); thresholdReadback(); thresholdCancellation();
    std::cout << "15 Intel turbo scenario groups passed\n";
}
