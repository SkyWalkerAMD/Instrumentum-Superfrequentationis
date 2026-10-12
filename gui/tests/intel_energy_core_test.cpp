// SPDX-License-Identifier: GPL-2.0-only
#include "intel_energy.h"
#include "intel_energy_fixture.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace octool::core;
namespace {
struct Test {
    IntelEnergyFixture *device = new IntelEnergyFixture;
    HardwareService service{std::unique_ptr<HardwareBackend>(device)};
    IntelEnergySnapshot read() {
        IntelEnergySnapshot out;
        const int error = service.transaction([&](HardwareSession &s) { out = readIntelEnergy(s, 130); return out.error; });
        assert(error == out.error); return out;
    }
    IntelEnergyUpdate apply(const IntelEnergySnapshot &old, IntelEnergyField field, unsigned enabled = 0) {
        IntelEnergyUpdate out;
        const int error = service.transaction([&](HardwareSession &s) { out = applyIntelEnergy(s, old, field, enabled); return out.error; });
        assert(error == out.error); return out;
    }
};
void encoding() {
    for (unsigned selected = 0; selected < 2; ++selected) {
        const auto field = IntelEnergyField(selected); const auto mask = UINT64_C(1) << (19+selected);
        for (unsigned bit = 0; bit < 64; ++bit) for (auto raw : {UINT64_C(1) << bit, ~(UINT64_C(1) << bit)}) {
            for (unsigned enabled : {0, 1}) {
                std::uint64_t word = 123;
                assert(!encodeIntelEnergy(field, enabled, raw, word));
                assert((word & ~mask) == (raw & ~mask) && bool(word & mask) == !enabled);
                assert(intelEfficientTurbo(word) == !(word & (UINT64_C(1) << 19)));
                assert(intelRaceToHalt(word) == !(word & (UINT64_C(1) << 20)));
            }
        }
        for (unsigned invalid : {2u, 255u, UINT32_MAX}) {
            std::uint64_t word = 123;
            assert(encodeIntelEnergy(field, invalid, 0, word) == -ERANGE && word == 123);
        }
    }
    std::uint64_t word = 123;
    assert(encodeIntelEnergy(IntelEnergyField(99), 1, 0, word) == -EINVAL && word == 123);
}
void readsAndGates() {
    for (unsigned bits = 0; bits < 4; ++bits) {
        Test t; t.device->regs[0x1fc] |= std::uint64_t(bits) << 19; const auto old = t.read();
        assert(old.valid && !old.error && old.cpu == 130 && intelEnergyEditable(old));
        assert(intelEfficientTurbo(old.raw) == !(bits & 1) && intelRaceToHalt(old.raw) == !(bits & 2));
        assert(t.device->requests.size() == 1 && !t.device->wrongCpu && !t.device->writes);
        const auto &r = t.device->requests.front();
        assert(r.address == 0x1fc && !r.write && r.width == 8 && r.space == Space::Msr);
    }
    for (unsigned mode = 0; mode < 8; ++mode) {
        Test t;
        if (mode == 0) t.device->amd = true;
        if (mode == 1) t.device->hasMsr = false;
        if (mode == 2) t.device->hypervisor = true;
        if (mode == 3) t.device->maximumLeaf = 0;
        if (mode == 4) t.device->inconsistentSignature = true;
        if (mode >= 5) t.device->model = mode == 5 ? 0x8f : mode == 6 ? 0xad : 0xba;
        const auto out = t.read();
        assert(out.error && !out.valid && !out.raw && t.device->requests.empty());
    }
}
void updates(IntelEnergyField field) {
    const auto mask = UINT64_C(1) << (field == IntelEnergyField::EfficientTurbo ? 19 : 20);
    for (auto raw : {UINT64_C(0), UINT64_MAX, UINT64_C(0xa5c3211098600243)}) for (unsigned enabled : {0, 1}) {
        Test t; t.device->regs[0x1fc] = raw; const auto old = t.read();
        const auto out = t.apply(old, field, enabled);
        const bool changed = bool(raw & mask) == bool(enabled);
        assert(!out.error && out.verified && out.expectedValid && out.writeAttempted == changed);
        assert(out.completed == unsigned(changed) && out.unchanged == !changed);
        assert(out.after.raw == out.expected && out.after.valid && !t.device->wrongCpu);
        assert((out.after.raw & ~mask) == (old.raw & ~mask));
        assert(bool(out.after.raw & mask) == !enabled);
        for (const auto &r : t.device->requests) assert(r.address == 0x1fc);
        const auto unchanged = t.apply(out.after, field, enabled);
        assert(!unchanged.error && unchanged.verified && unchanged.unchanged && !unchanged.writeAttempted && !unchanged.completed);
        assert(t.device->writes == unsigned(changed));
    }
}
void invalidAndStale(IntelEnergyField field) {
    Test t; const auto old = t.read(); const auto calls = t.device->cpuCalls;
    assert(t.apply(old, field, 2).error == -ERANGE && t.device->cpuCalls == calls);
    assert(t.apply(old, IntelEnergyField(99)).error == -EINVAL && t.device->cpuCalls == calls);
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto bad = old;
        if (mode == 0) bad.valid = false;
        if (mode == 1) bad.error = -EIO;
        if (mode == 2) bad.identity.error = -EACCES;
        if (mode == 3) bad.identity.family = 0x1a;
        if (mode == 4) bad.identity.model = 0x8f;
        const auto out = t.apply(bad, field);
        assert(out.error == -EINVAL && !out.expectedValid && t.device->cpuCalls == calls);
    }
    for (unsigned bit = 0; bit < 64; ++bit) {
        t.device->regs[0x1fc] = old.raw ^ (UINT64_C(1) << bit);
        assert(t.apply(old, field).error == -EAGAIN && !t.device->writes);
    }
    t.device->regs[0x1fc] = old.raw; t.device->stepping = 1;
    assert(t.apply(old, field).error == -ENODEV && !t.device->writes);
    Test race; const auto before = race.read();
    race.device->afterRequest = [&](const Request &r) { if (!r.write && race.device->reads == 2) race.device->regs[0x1fc] ^= UINT64_C(1) << 40; };
    assert(race.apply(before, field).error == -EAGAIN && !race.device->writes);
}
void failures(IntelEnergyField field) {
    for (int position = 0; position < 3; ++position) {
        Test t; t.device->failCpuid = position;
        assert(t.read().error == -EACCES && t.device->requests.empty());
    }
    Test initial; initial.device->failRead = 0;
    const auto failed = initial.read(); assert(failed.error == -EACCES && !failed.raw && !failed.valid);
    for (int position = 0; position < 3; ++position) {
        Test t; const auto old = t.read(); t.device->failRead = int(t.device->reads) + position;
        const auto out = t.apply(old, field);
        assert(out.error == -EACCES && !out.verified && out.writeAttempted == (position == 2));
        assert(t.device->writes == (position == 2 ? 1u : 0u));
    }
    for (int position = 0; position < 6; ++position) {
        Test t; const auto old = t.read(); t.device->failCpuid = int(t.device->cpuCalls) + position;
        const auto out = t.apply(old, field);
        assert(out.error == -EACCES && !out.verified && out.writeAttempted == (position >= 3));
    }
    Test write; const auto old = write.read(); write.device->failWrite = 0;
    const auto out = write.apply(old, field);
    assert(out.error == -EIO && out.writeAttempted && !out.completed && !out.verified && write.device->writes == 1);
}
void readback(IntelEnergyField field) {
    for (unsigned bit = 0; bit < 64; ++bit) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) t.device->regs[0x1fc] ^= UINT64_C(1) << bit; };
        const auto out = t.apply(old, field);
        assert(out.error == -EIO && !out.verified && out.writeAttempted && out.completed == 1 && t.device->writes == 1);
    }
    for (unsigned mode = 0; mode < 6; ++mode) {
        Test t; const auto old = t.read();
        t.device->afterRequest = [&](const Request &r) { if (r.write) {
            if (mode == 0) t.device->stepping = 1;
            if (mode == 1) t.device->hypervisor = true;
            if (mode == 2) t.device->hasMsr = false;
            if (mode == 3) t.device->amd = true;
            if (mode == 4) t.device->model = 0x8f;
            if (mode == 5) t.device->inconsistentSignature = true;
        } };
        const auto out = t.apply(old, field);
        assert(out.error && !out.verified && t.device->writes == 1);
    }
    Test ignored; const auto old = ignored.read(); ignored.device->discardChange = true;
    assert(ignored.apply(old, field).error == -EIO && ignored.device->writes == 1);
}
void cancellation(IntelEnergyField field) {
    // Cancel after preflight, final comparison, write, and readback.
    for (unsigned stop = 1; stop <= 4; ++stop) {
        Test t; const auto old = t.read(); std::atomic<bool> cancelled(false); unsigned calls = 0;
        t.device->afterRequest = [&](const Request &) { if (++calls == stop) cancelled.store(true); };
        IntelEnergyUpdate out;
        const int error = t.service.transaction([&](HardwareSession &s) { out = applyIntelEnergy(s, old, field, 0); return out.error; }, 5000, &cancelled);
        assert(error == -ECANCELED && out.error == error && !out.verified && t.device->writes == (stop >= 3 ? 1u : 0u));
    }
    for (unsigned stop : {1, 3, 4}) {
        Test t; const auto old = t.read(); unsigned calls = 0;
        t.device->afterRequest = [&](const Request &) { if (++calls == stop) std::this_thread::sleep_for(std::chrono::milliseconds(15)); };
        IntelEnergyUpdate out;
        const int error = t.service.transaction([&](HardwareSession &s) { out = applyIntelEnergy(s, old, field, 0); return out.error; }, 10);
        assert(error == -ETIMEDOUT && !out.verified && t.device->writes <= (stop >= 3 ? 1u : 0u));
    }
}
}
int main() {
    encoding(); readsAndGates();
    for (auto field : {IntelEnergyField::EfficientTurbo, IntelEnergyField::RaceToHalt}) {
        updates(field); invalidAndStale(field); failures(field); readback(field); cancellation(field);
    }
    std::cout << "12 Intel energy optimization scenario groups passed\n";
}
