// SPDX-License-Identifier: GPL-2.0-only
#include "intel_clock_fixture.h"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace octool::core;
using intel_clock_fixture::Device;
namespace {
struct Borrowed : HardwareBackend {
    Device &d;
    explicit Borrowed(Device &device) : d(device) {}
    Reply execute(const Request &r) override { return d.execute(r); }
    CpuIdReply cpuid(unsigned c, std::uint32_t l, std::uint32_t s) override { return d.cpuid(c,l,s); }
    Backend backend(Space s) const override { return d.backend(s); }
};
IntelClockSnapshot read(Device &d, const std::atomic<bool> *cancelled = nullptr) {
    HardwareService service(std::unique_ptr<HardwareBackend>(new Borrowed(d))); IntelClockSnapshot out;
    const int error = service.transaction([&](HardwareSession &s) { out = readIntelClock(s,d.expectedCpu); return out.error; },10000,cancelled);
    if (error) out.error = error;
    return out;
}
void golden() {
    const auto s = decodeIntelClock(intel_clock_fixture::registers());
    assert(s.valid && !s.error && !s.bclkError && !s.mrcError && s.offline && s.registers.size() == 3);
    assert(s.bclkKhz == 101250 && s.mrcGear == 2 && s.mrcReference == 1 && s.mrcRatio == 32);
    assert(s.mrcVddqMillivolts == 1150 && s.mrcIccMaxEncoded == 9);
    auto v = intel_clock_fixture::registers(); v[0].raw = 0;
    assert(decodeIntelClock(v).mrcRatio == 32); // Decode DATA, not REQ.
    std::reverse(v.begin(),v.end()); assert(decodeIntelClock(v).bclkKhz == 101250);
}
void requestEncodings() {
    for (unsigned gear = 0; gear < 4; ++gear) for (unsigned ref = 0; ref < 16; ++ref) for (unsigned ratio : {0u,1u,2u,3u,32u,255u}) {
        auto v = intel_clock_fixture::registers(); v[1].raw = (gear<<12)|(ref<<8)|ratio;
        const auto s = decodeIntelClock(v); assert(s.valid && !s.error && s.bclkKhz == 101250);
        const bool valid = gear < 3 && ref < 2 && ratio != 1 && ratio != 2;
        assert(s.mrcError == (valid ? 0 : -ENOTSUP));
        if (valid) assert(s.mrcGear == (1u<<gear) && s.mrcReference == ref && s.mrcRatio == ratio);
        else assert(intelClockJson(s).find("\"gear_ratio\":null") != std::string::npos);
    }
    for (unsigned bit : {14u,15u,16u,31u}) {
        auto v = intel_clock_fixture::registers(); v[1].raw |= UINT64_C(1)<<bit;
        const auto s = decodeIntelClock(v); assert(s.valid && s.mrcError == -ENOTSUP && !s.bclkError);
    }
    auto v = intel_clock_fixture::registers(); v[1].raw = 0x7ffe21ff;
    const auto s = decodeIntelClock(v); assert(s.valid && !s.mrcError && s.mrcVddqMillivolts == 5115 && s.mrcIccMaxEncoded == 15 && s.mrcGear == 4);
}
void bclkBoundaries() {
    for (auto value : {UINT64_C(0),UINT64_C(1),UINT64_C(100000),UINT64_C(4294967294),UINT64_C(4294967295),UINT64_C(0x8000000000018b82)}) {
        auto v = intel_clock_fixture::registers(); v[2].raw = value;
        const auto s = decodeIntelClock(v); assert(s.valid && !s.mrcError);
        const int error = value>>32 ? -ENOTSUP : !value || value == UINT32_MAX ? -ENODATA : 0;
        assert(s.bclkError == error && s.bclkKhz == (error ? 0 : value));
        const auto parsed = parseIntelClockCapture(intelClockJson(s));
        assert(parsed.valid && parsed.bclkError == error && parsed.registers[2].raw == value);
    }
}
void malformedRegisters() {
    const auto initial = intel_clock_fixture::registers();
    for (unsigned i = 0; i < 3; ++i) {
        auto v = initial; v.erase(v.begin()+i); assert(decodeIntelClock(v).error == -EINVAL);
        v = initial; v.push_back(v[i]); assert(decodeIntelClock(v).error == -EINVAL);
        v = initial; v[i].offset = 0x15f60; assert(decodeIntelClock(v).error == -EINVAL);
        v = initial; v[i].raw = intelClockRegisters()[i].width == 4 ? UINT32_MAX : UINT64_MAX;
        assert(decodeIntelClock(v).error == -ENODATA);
        if (i < 2) { v = initial; v[i].raw |= UINT64_C(1)<<32; assert(decodeIntelClock(v).error == -EINVAL); }
    }
    auto v = initial; v[1].offset = v[0].offset; assert(decodeIntelClock(v).error == -EINVAL);
    v = initial; v[0].raw |= UINT64_C(1)<<31; const auto s = decodeIntelClock(v);
    assert(s.error == -EAGAIN && !s.valid && s.registers.empty());
}
void jsonRoundtrip() {
    auto s = decodeIntelClock(intel_clock_fixture::registers()); s.offline = false;
    const auto json = intelClockJson(s); const auto parsed = parseIntelClockCapture(json);
    assert(parsed.valid && parsed.offline && parsed.bclkKhz == 101250 && parsed.mrcRatio == 32);
    assert(json.find("\"current_memory_frequency_available\":false") != std::string::npos);
    for (const auto *name : {"intel-clock-read","intel-clock-decode"}) {
        const auto wrapped = "{\"schema_version\":1,\"ok\":true,\"error\":0,\"command\":\""+std::string(name)+"\",\"data\":{\"clock\":"+json+"}}";
        assert(parseIntelClockCapture(wrapped).bclkKhz == 101250);
    }
    auto forged = json; forged.replace(forged.find("\"reported_khz\":101250"),21,"\"reported_khz\":999999");
    assert(parseIntelClockCapture(forged).bclkKhz == 101250);
    s.bclkKhz = 999; s.mrcRatio = 9;
    assert(intelClockJson(s) == json); // Serialize recomputed values.
}
void malformedJson() {
    const auto json = intelClockJson(decodeIntelClock(intel_clock_fixture::registers()));
    for (const auto &text : {std::string(),std::string("{}"),json+"x",json.substr(0,json.size()-1),std::string(65537,' '),
            std::string("{\"format\":1,\"format\":2}"),std::string("{\"bad\":\"\xed\xa0\x80\"}"),std::string("{\"schema_version\":1,\"ok\":false}")}) {
        const auto s = parseIntelClockCapture(text); assert(s.error && !s.valid && s.registers.empty());
    }
    for (const auto *bad : {"0x0000000049cc112", "0x0000000049cc11200", "0x0000000049cc112g", "0000000049cc1120"}) {
        auto text = json; text.replace(text.find("0x0000000049cc1120"),18,bad); assert(parseIntelClockCapture(text).error);
    }
    auto text = json; text.replace(text.find("clock-v1"),8,"clock-v2"); assert(parseIntelClockCapture(text).error);
    text = json; text.replace(text.find("raptor-lake-s-743846-001"),22,"wrong-layout"); assert(parseIntelClockCapture(text).error);
}
void liveSuccess() {
    Device d; d.controller = d.channel = 1; // Shared PCU offsets ignore these.
    const auto s = read(d); assert(s.valid && !s.offline && !s.error && s.mchbar == d.bar && s.identity.model == 0xb7);
    assert(d.reads == 21 && !d.writes && d.cpuCalls == 3 && !d.wrongCpu && s.cpu == 130);
    assert(s.bclkKhz == 101250 && s.mrcVddqMillivolts == 1150);
    unsigned memory = 0;
    for (const auto &r : d.requests) {
        assert(!r.write && r.space != Space::Msr);
        if (r.space == Space::Memory) { ++memory; assert(r.address == UINT64_C(0x12fedc5e00) || r.address == UINT64_C(0x12fedc5e04) || r.address == UINT64_C(0x12fedc5f60)); }
    }
    assert(memory == 6);
}
void identityGates() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Device d;
        if (!mode) d.amd = true; else if (mode == 1) d.model = 0xba;
        else if (mode == 2) d.hypervisor = true; else if (mode == 3) d.pciId = 0xa7068086; else d.pciClass = 0x03000001;
        const auto s = read(d); assert(s.error && !s.valid && s.registers.empty() && !d.writes);
        for (const auto &r : d.requests) assert(r.space == Space::Pci);
    }
}
void barGates() {
    for (const auto bar : {UINT64_C(0),UINT64_C(1),UINT64_C(0xfedc0000),UINT64_C(0xfedc0003),UINT64_C(0x400fedc0001),UINT64_MAX}) {
        Device d; d.bar = bar; const auto s = read(d); assert(s.error && !s.valid);
        for (const auto &r : d.requests) assert(r.space == Space::Pci);
    }
    Device d; d.afterRequest = [&](const Request &r) { if (r.space == Space::Pci && r.address == 0x48) d.bar += UINT64_C(1)<<32; };
    assert(read(d).error == -EAGAIN && d.reads == 5);
}
void failurePositions() {
    for (int call = 0; call < 3; ++call) { Device d; d.failCpuid = call; const auto s = read(d); assert(s.error == -EACCES && d.requests.empty()); }
    for (int call = 0; call < 21; ++call) {
        Device d; d.failRead = call; const auto s = read(d);
        assert(s.error == -EACCES && !s.valid && s.registers.empty() && int(d.reads) == call+1 && !d.writes);
    }
}
void racesAndBusy() {
    for (unsigned i = 0; i < 3; ++i) {
        Device d; d.afterRequest = [&](const Request &) { if (d.reads == 8) d.clock[intelClockRegisters()[i].offset] ^= 1; };
        assert(read(d).error == -EAGAIN && !d.writes);
    }
    Device d; d.afterRequest = [&](const Request &) { if (d.reads == 8) d.bar ^= 0x20000; };
    assert(read(d).error == -EAGAIN);
    Device busy; busy.clock[0x5e00] |= UINT64_C(1)<<31; assert(read(busy).error == -EAGAIN && busy.reads == 6);
    Device later; later.afterRequest = [&](const Request &) { if (later.reads == 8) later.clock[0x5e00] |= UINT64_C(1)<<31; };
    assert(read(later).error == -EAGAIN && later.reads == 14);
    Device absent; absent.clock[0x5e00] = UINT32_MAX; assert(read(absent).error == -ENODATA && absent.reads == 6);
}
void cancellation() {
    Device d; std::atomic<bool> stop{true}; assert(read(d,&stop).error == -ECANCELED && !d.cpuCalls);
    stop = false; d.afterRequest = [&](const Request &) { if (d.reads == 6) stop = true; };
    assert(read(d,&stop).error == -ECANCELED && d.reads == 6 && !d.writes);
}
}
int main() {
    golden(); requestEncodings(); bclkBoundaries(); malformedRegisters(); jsonRoundtrip(); malformedJson();
    liveSuccess(); identityGates(); barGates(); failurePositions(); racesAndBusy(); cancellation();
    std::cout << "12 Intel clock scenario groups passed\n";
}
