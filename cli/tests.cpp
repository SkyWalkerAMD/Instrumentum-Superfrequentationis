// SPDX-License-Identifier: GPL-2.0-only
#include "commands.h"
#include "json.h"
#include "platform/linux_inventory_native.h"
#include "tests/amd_curve_fixture.h"
#include "tests/intel_oc_fixture.h"
#include "tests/intel_controls_fixture.h"
#include <cassert>
#include <cerrno>
#include <iostream>
#include <map>
#include <sstream>

using namespace octool::core;
using namespace octool::cli;
namespace {
bool emit = false;
struct Borrowed : HardwareBackend {
    HardwareBackend &device;
    explicit Borrowed(HardwareBackend &d) : device(d) {}
    Reply execute(const Request &r) override { return device.execute(r); }
    CpuIdReply cpuid(unsigned c, std::uint32_t l, std::uint32_t s) override { return device.cpuid(c, l, s); }
    Backend backend(Space s) const override { return device.backend(s); }
};
std::string command(HardwareBackend &device, const std::vector<std::string> &args, int expected) {
    std::ostringstream out;
    const int code = run(args, out, [&] { return std::unique_ptr<HardwareBackend>(new Borrowed(device)); });
    if (code != expected) std::cerr << out.str();
    assert(code == expected);
    if (emit) std::cout << out.str();
    return out.str();
}
void invalidBeforeOpen() {
    const std::vector<std::vector<std::string>> invalid = {
        {"unknown"}, {"diagnose", "--cpu", "0"}, {"cpu"}, {"cpu", "--cpu"}, {"cpu", "--cpu", "-1"},
        {"cpu", "--cpu", "0x"}, {"cpu", "--cpu", "1junk"}, {"cpu", "--cpu", " 1"},
        {"cpu", "--cpu", "42949672960"}, {"cpu", "--cpu", "0", "--cpu", "1"},
        {"cpu", "--cpu", "0", "--timeout-ms", "0"}, {"cpu", "--cpu", "0", "--timeout-ms", "120001"},
        {"intel-set", "--cpu", "0", "--field", "pl1", "--value", "125"},
        {"intel-set", "--cpu", "0", "--field", "pl1", "--value", "nan", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-epp", "--value", "3.5", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "pl1-enable", "--value", "2", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "bad", "--value", "1", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "max-ratio", "--value", "86", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "cache", "--field", "offset-mv", "--value", "-1001", "--apply"},
        {"intel-oc-read", "--cpu", "0", "--domain", "fabric"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "1000", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "1000", "--mode", "auto", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "0", "--mode", "adaptive", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "2001", "--mode", "adaptive", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "-1", "--mode", "adaptive", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "1000.5", "--mode", "override", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "nan", "--mode", "override", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "4294967295", "--mode", "override", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "offset-mv", "--value", "0", "--mode", "adaptive", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "max-ratio", "--value", "50", "--mode", "override", "--apply"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "1000", "--mode", "override"},
        {"intel-oc-set", "--cpu", "0", "--domain", "core", "--field", "target-mv", "--value", "1000", "--mode", "override", "--mode", "adaptive", "--apply"},
        {"intel-vf-read", "--cpu", "0", "--domain", "fabric"},
        {"intel-vf-read", "--cpu", "0"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--point", "0"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--point", "16"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--point", "-1"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--apply"},
        {"amd-smu-read", "--cpu", "0", "--profile", "shimada", "--message", "0x24"},
        {"amd-smu-send", "--cpu", "0", "--profile", "phoenix", "--message", "0x24", "--arg0", "1", "--apply"},
        {"amd-smu-send", "--cpu", "0", "--profile", "shimada", "--message", "0x24", "--arg0", "1"},
        {"amd-smu-send", "--cpu", "0", "--profile", "shimada", "--message", "0x24", "--apply"},
        {"amd-smu-probe", "--cpu", "0", "--profile", "shimada", "--device", "32"},
        {"amd-curve-read", "--cpu", "0", "--ccd", "16", "--core", "0"},
        {"amd-curve-read", "--cpu", "0", "--ccd", "0", "--core", "8"},
        {"amd-umc-read", "--cpu", "0", "--bank", "23", "--refresh-slot", "0"},
        {"amd-umc-read", "--cpu", "0", "--bank", "0"},
        {"spd-decode", "--file", ""}, {"inventory", "--apply"},
        {"register-read", "--space", "bad", "--address", "0"},
        {"register-read", "--space", "msr", "--address", "0x606"},
        {"register-read", "--space", "msr", "--cpu", "0", "--address", "0x606", "--width", "4"},
        {"register-read", "--space", "msr", "--cpu", "0", "--address", "4294967296"},
        {"register-read", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "0"},
        {"register-read", "--space", "msr", "--cpu", "0", "--address", "1", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "0"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "18446744073709551616", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "0x10000000000000000", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "-1", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "0x", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "", "--apply"},
        {"register-write", "--space", "msr", "--cpu", "0", "--address", "1", "--value", "1.0", "--apply"},
        {"register-read", "--space", "mmio", "--address", "0x1001", "--width", "4"},
        {"register-read", "--space", "mmio", "--address", "0x10000000000000000", "--width", "1"},
        {"register-read", "--space", "mmio", "--address", "0x1000", "--width", "0"},
        {"register-read", "--space", "mmio", "--address", "0x1000", "--width", "3"},
        {"register-write", "--space", "mmio", "--address", "0x1000", "--width", "1", "--value", "256", "--apply"},
        {"register-read", "--space", "pci", "--bus", "0", "--device", "0", "--function", "0", "--address", "0", "--width", "8"},
        {"register-read", "--space", "pci", "--bus", "0", "--device", "0", "--function", "0", "--address", "253", "--width", "4"},
        {"register-read", "--space", "pci", "--bus", "256", "--device", "0", "--function", "0", "--address", "0", "--width", "4"},
        {"register-read", "--space", "pci", "--bus", "0", "--device", "32", "--function", "0", "--address", "0", "--width", "4"},
        {"register-read", "--space", "pci", "--bus", "0", "--device", "0", "--function", "8", "--address", "0", "--width", "4"},
        {"register-read", "--space", "pci", "--bus", "0", "--device", "0", "--address", "0", "--width", "4"},
        {"register-read", "--space", "mmio", "--cpu", "0", "--address", "0x1000", "--width", "4"},
        {"register-read", "--space", "msr", "--cpu", "0", "--bus", "0", "--address", "1"},
        {"intel-set", "--cpu", "0", "--field", "hwp-window-us", "--value", "-1", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-window-us", "--value", "1270000001", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-window-us", "--value", "0.5", "--apply"}
    };
    unsigned opens = 0;
    const auto factory = [&]() { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (const auto &args : invalid) {
        std::ostringstream out; assert(run(args, out, factory) == 2);
        assert(out.str().find("\"ok\":false") != std::string::npos);
        if (emit) std::cout << out.str();
    }
    for (const auto &args : std::vector<std::vector<std::string>>{{}, {"--help"}, {"--version"}, {"diagnose"}}) {
        std::ostringstream out; assert(!run(args, out, factory));
    }
    assert(!opens);
}
struct Registers : HardwareBackend {
    bool amd = false, denied = false, locked = false;
    unsigned writes = 0, reads = 0, cpuCalls = 0, expectedCpu = 130;
    std::map<std::uint64_t, std::uint64_t> values;
    Registers() {
        values[0x606] = 0xa0e03; values[0x610] = UINT64_C(0x0800800000008000) | 1000;
        values[0x770] = 1; values[0x771] = 0x08203040; values[0x774] = 0x80004008;
        values[0xc0010061] = 1u << 4; values[0xc0010064] = UINT64_C(0xfffffffffffffc00); values[0xc0010065] = 0;
    }
    Reply execute(const Request &r) override {
        assert(r.cpu == expectedCpu && r.space == Space::Msr); Reply out;
        if (r.write) { ++writes; values[r.address] = r.value; }
        else { ++reads; if (denied) out.error = -EACCES; else out.value = values.at(r.address); }
        if (!r.write && r.address == 0x610 && locked) out.value |= UINT64_C(1) << 63;
        return out;
    }
    CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        assert(cpu == expectedCpu); ++cpuCalls; CpuIdReply out;
        if (!leaf) { out.words[0] = 6; out.words[1] = amd ? 0x68747541 : 0x756e6547; out.words[2] = amd ? 0x444d4163 : 0x6c65746e; out.words[3] = amd ? 0x69746e65 : 0x49656e69; }
        else if (leaf == 1) out.words[0] = amd ? 0xb00f20 : 0xb0670;
        else if (leaf == 6) out.words[0] = (1u << 7) | (1u << 10);
        else if (leaf == 0x80000000) out.words[0] = 0x80000026;
        else if (leaf == 0x80000007) out.words[3] = 1u << 7;
        else if (leaf == 0x80000026) {
            const unsigned levels[4][4] = {{1,2,0x100,0x1af},{4,12,0x201,0x1af},{4,12,0x302,0x1af},{8,144,0x403,0x1af}};
            if (subleaf < 4) for (unsigned i = 0; i < 4; ++i) out.words[i] = levels[subleaf][i];
        } else out.error = -EINVAL;
        return out;
    }
    Backend backend(Space) const override { return Backend::Module; }
};
void registerCommands() {
    Registers r;
    command(r, {"cpu", "--cpu", "130"}, 0);
    command(r, {"intel-read", "--cpu", "130"}, 0); assert(!r.writes);
    const auto previous = r.values[0x610];
    command(r, {"intel-set", "--cpu", "130", "--field", "pl1", "--value", "100", "--apply"}, 0);
    assert(r.writes == 1 && r.values[0x610] == ((previous & ~UINT64_C(0x7fff)) | 800));
    command(r, {"intel-set", "--cpu", "130", "--field", "hwp-epp", "--value", "192", "--apply"}, 0);
    assert(r.values[0x774] == 0xc0004008 && r.writes == 2);
    r.locked = true;
    command(r, {"intel-set", "--cpu", "130", "--field", "pl1", "--value", "100", "--apply"}, 3); assert(r.writes == 2);
    r.denied = true; command(r, {"intel-read", "--cpu", "130"}, 3);
    r.denied = false; r.amd = true;
    command(r, {"amd-pstates", "--cpu", "0x82"}, 0);
    command(r, {"amd-topology", "--cpu", "130"}, 0);
    r.denied = true; command(r, {"amd-pstates", "--cpu", "130"}, 3);
    r.amd = false; command(r, {"amd-pstates", "--cpu", "130"}, 3);
    assert(r.writes == 2);
}
void ocAndCurveCommands() {
    IntelOcFixture intel;
    command(intel, {"intel-oc-read", "--cpu", "130", "--domain", "cache"}, 0);
    assert(!intel.mutationCount && !intel.wrongCpu);
    const auto previous = intel.settings[2];
    command(intel, {"intel-oc-set", "--cpu", "130", "--domain", "cache", "--field", "max-ratio", "--value", "50", "--apply"}, 0);
    assert(intel.settings[2] == ((previous & ~255u) | 50) && intel.mutationCount == 1);
    command(intel, {"intel-oc-set", "--cpu", "130", "--domain", "core", "--field", "offset-mv", "--value", "-50", "--apply"}, 0);
    assert(intel.mutationCount == 2);
    intel.locked = true;
    command(intel, {"intel-oc-set", "--cpu", "130", "--domain", "core", "--field", "max-ratio", "--value", "50", "--apply"}, 3);
    assert(intel.mutationCount == 2);
    AmdCurveFixture amd;
    command(amd, {"amd-curve-read", "--cpu", "130", "--ccd", "10", "--core", "7"}, 0);
    assert(amd.submitted == 0xa0700000 && !amd.wrongCpu);
    amd.completion = 0xfe;
    command(amd, {"amd-curve-read", "--cpu", "130", "--ccd", "0", "--core", "0"}, 3);
    assert(amd.commands == 2 && amd.argumentReads == 1);
}
void verifiedControls() {
    IntelControlsFixture device; device.expectedCpu = 130;
    const std::vector<std::string> power = {"intel-set", "--cpu", "130", "--field", "pl1", "--value", "100.12", "--apply"};
    auto result = command(device, power, 0);
    assert(result.find("\"verified\":true") != std::string::npos && result.find("\"readback_value\":100") != std::string::npos);
    result = command(device, power, 0);
    assert(result.find("\"unchanged\":true") != std::string::npos && device.writes == 1);
    result = command(device, {"intel-set", "--cpu", "130", "--field", "hwp-epp", "--value", "192", "--apply"}, 0);
    assert(result.find("\"verified\":true") != std::string::npos && device.writes == 2);
    device.discardChange = true;
    result = command(device, {"intel-set", "--cpu", "130", "--field", "hwp-epp", "--value", "64", "--apply"}, 3);
    assert(result.find("\"verified\":false") != std::string::npos && result.find("\"readback_value\":null") != std::string::npos);
    assert(device.writes == 3 && !device.wrongCpu);
}
void rawRegisters() {
    struct Raw : HardwareBackend {
        std::vector<Request> requests;
        int error = 0;
        Reply execute(const Request &r) override {
            requests.push_back(r); Reply out; out.error = error;
            out.value = r.width == 8 ? UINT64_C(0xfedcba9876543210) : (UINT64_C(1) << (8 * r.width)) - 1;
            return out;
        }
        CpuIdReply cpuid(unsigned, std::uint32_t, std::uint32_t) override { assert(false); return {}; }
        Backend backend(Space) const override { return Backend::Module; }
    } device;
    auto result = command(device, {"register-read", "--space", "msr", "--cpu", "130", "--address", "0xffffffff"}, 0);
    assert(device.requests.size() == 1 && device.requests.back().cpu == 130 && device.requests.back().address == UINT32_MAX);
    assert(result.find("\"value\":\"0xfedcba9876543210\"") != std::string::npos);
    for (const auto &value : {"18446744073709551615", "0xffffffffffffffff"}) {
        result = command(device, {"register-write", "--space", "msr", "--cpu", "130", "--address", "1", "--value", value, "--apply"}, 0);
        assert(device.requests.back().write && device.requests.back().value == UINT64_MAX);
        assert(result.find("\"submitted\":\"0xffffffffffffffff\"") != std::string::npos);
    }
    for (unsigned width : {1,2,4,8}) {
        const auto count = device.requests.size();
        const auto w = std::to_string(width);
        const auto address = std::to_string(UINT64_MAX - width + 1);
        command(device, {"register-read", "--space", "mmio", "--address", address, "--width", w}, 0);
        command(device, {"register-write", "--space", "mmio", "--address", address, "--width", w, "--value", "0", "--apply"}, 0);
        assert(device.requests.size() == count + 2 && device.requests.back().address == UINT64_MAX - width + 1);
        assert(device.requests.back().space == Space::Memory && device.requests.back().width == int(width));
    }
    for (unsigned width : {1,2,4}) {
        const auto w = std::to_string(width), address = std::to_string(256 - width);
        command(device, {"register-read", "--space", "pci", "--bus", "255", "--device", "31", "--function", "7", "--address", address, "--width", w}, 0);
        command(device, {"register-write", "--space", "pci", "--bus", "255", "--device", "31", "--function", "7", "--address", address, "--width", w, "--value", "0", "--apply"}, 0);
        const auto &r = device.requests.back();
        assert(r.space == Space::Pci && r.bus == 255 && r.device == 31 && r.function == 7 && r.address == 256 - width);
    }
    const auto count = device.requests.size(); device.error = -EACCES;
    result = command(device, {"register-read", "--space", "msr", "--cpu", "130", "--address", "0x606"}, 3);
    assert(result.find("\"value\":null") != std::string::npos);
    result = command(device, {"register-write", "--space", "msr", "--cpu", "130", "--address", "1", "--value", "0", "--apply"}, 3);
    assert(result.find("\"write_attempted\":true") != std::string::npos && result.find("\"submitted\":null") != std::string::npos);
    assert(device.requests.size() == count + 2); // No pre-read, retry or readback.
}
void activityWindowCommands() {
    IntelControlsFixture device; device.expectedCpu = 130;
    auto result = command(device, {"intel-set", "--cpu", "130", "--field", "hwp-window-us", "--value", "15333", "--apply"}, 0);
    assert(result.find("\"readback_value\":15000") != std::string::npos && device.writes == 1);
    result = command(device, {"intel-set", "--cpu", "130", "--field", "hwp-window-us", "--value", "0", "--apply"}, 0);
    assert(result.find("\"readback_value\":0") != std::string::npos && device.writes == 2);
    device.activityWindow = false;
    command(device, {"intel-set", "--cpu", "130", "--field", "hwp-window-us", "--value", "100", "--apply"}, 3);
    assert(device.writes == 2 && !device.wrongCpu);
}
void voltageCommands() {
    for (const std::string domain : {"core", "cache"}) {
        for (const std::string mode : {"adaptive", "override"}) {
            IntelOcFixture intel;
            const unsigned index = domain == "core" ? 0 : 2;
            const auto previous = intel.settings[index], other = intel.settings[2 - index];
            const std::vector<std::string> args = {"intel-oc-set", "--cpu", "130", "--domain", domain,
                "--field", "target-mv", "--value", "1234", "--mode", mode, "--apply"};
            command(intel, args, 0);
            assert(intel.settings[index] == ((previous & 0xffe000ffu) | 0x4f000u | (mode == "override" ? 1u << 20 : 0)));
            assert(intel.mutationCount == 1 && !intel.wrongCpu && intel.settings[2 - index] == other);
            const auto same = command(intel, args, 0);
            assert(same.find("\"write_attempted\":false") != std::string::npos && intel.mutationCount == 1);
        }
    }
    const std::vector<std::string> args = {"intel-oc-set", "--cpu", "130", "--domain", "core",
        "--field", "target-mv", "--value", "1234", "--mode", "override", "--apply"};
    for (unsigned failure = 0; failure < 6; ++failure) {
        IntelOcFixture intel;
        if (failure == 0) intel.locked = true;
        if (failure == 1) intel.model = 0x8f;
        if (failure == 2) intel.discardChange = true;
        if (failure == 3) { intel.failCommand = 0x11; intel.status = 3; }
        if (failure == 4) intel.afterRequest = [&] { if (intel.requests.size() == 4) intel.settings[0] ^= 1u << 31; };
        if (failure == 5) intel.failAt = 0;
        const auto result = command(intel, args, 3);
        assert(result.find("\"verified\":false") != std::string::npos);
        assert(intel.mutationCount == (failure == 2 ? 1u : 0u));
    }
}
void vfCommands() {
    IntelOcFixture intel;
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "core"}, 0);
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "cache", "--point", "15"}, 0);
    intel.failVfPoint = 8;
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "core"}, 3);
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "cache", "--point", "8"}, 3);
    intel.failVfPoint = 0; intel.clearTrace(); intel.failAt = 5;
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "core"}, 3);
    intel.clearTrace(); intel.model = 0x8f;
    command(intel, {"intel-vf-read", "--cpu", "130", "--domain", "core"}, 3);
    assert(intel.requests.empty() && !intel.mutationCount && !intel.wrongCpu);
}
struct PciDevice : Registers {
    std::uint32_t index = 0, response = 1, message = 0;
    bool fail = false;
    unsigned bus = 0, device = 0, function = 0, umcReads = 0;
    std::uint32_t args[6]{};
    PciDevice() { amd = true; }
    Reply execute(const Request &r) override {
        assert(r.space == Space::Pci && r.width == 4 && r.bus == bus && r.device == device && r.function == function);
        Reply out;
        if (!r.write && r.address == 0) out.value = 0x153a1022;
        else if (r.write && (r.address == 0xf8 || r.address == 0xe0)) { index = std::uint32_t(r.value); ++writes; }
        else if (r.address == 0xe4 && !r.write) { ++umcReads; out.value = (index & 0xffff) == 0x200 ? 1200 : 0x12345678; if (fail) out.error = -EACCES; }
        else if (r.address == 0xfc) {
            if (index == 0x3b1097c) { if (r.write) response = std::uint32_t(r.value); else out.value = response; }
            else if (index == 0x3b10930 && r.write) { message = std::uint32_t(r.value); response = fail ? 0xfe : 1; }
            else if (index >= 0x3b109c4 && index <= 0x3b109d8) {
                const auto i = (index - 0x3b109c4) / 4;
                if (r.write) args[i] = std::uint32_t(r.value); else out.value = args[i] + 100;
            } else assert(false);
        } else assert(false);
        return out;
    }
};
void pciCommands() {
    PciDevice d;
    command(d, {"amd-smu-probe", "--cpu", "130", "--profile", "shimada"}, 0); assert(!d.writes);
    command(d, {"amd-smu-read", "--cpu", "130", "--profile", "shimada", "--message", "2"}, 0); assert(d.message == 2);
    command(d, {"amd-smu-send", "--cpu", "130", "--profile", "shimada", "--message", "0x24", "--arg0", "123", "--arg5", "0xffffffff", "--apply"}, 0);
    assert(d.message == 0x24 && d.args[0] == 123 && d.args[5] == UINT32_MAX);
    d.fail = true;
    command(d, {"amd-smu-read", "--cpu", "130", "--profile", "shimada", "--message", "2"}, 3);
    d.fail = false; d.bus = 2; d.device = 3; d.function = 1;
    command(d, {"amd-umc-read", "--cpu", "130", "--bus", "2", "--device", "3", "--function", "1", "--bank", "22", "--refresh-slot", "15"}, 0);
    assert(d.umcReads == 56);
    d.fail = true;
    command(d, {"amd-umc-read", "--cpu", "130", "--bus", "2", "--device", "3", "--function", "1", "--bank", "22", "--refresh-slot", "15"}, 3);
    assert(d.umcReads == 57);
}
void inventory(const std::string &root) {
    const auto result = octool::platform::linuxInventoryNative(root);
    bool temperature = false, board = false, invalid = false, power = false, oversized = false;
    for (const auto &r : result.rows) {
        if (r.name == "CPU") { temperature = true; assert(r.value == "-1.25" && r.unit == "C" && r.status.find("fault") != std::string::npos); }
        if (r.name == "board_name") { board = true; assert(r.value == "Fixture board"); }
        if (r.name == "in1") { invalid = true; assert(r.error == -EINVAL && r.value.empty()); }
        if (r.name == "power1 (average)") { power = true; assert(r.value == "125" && r.unit == "W"); }
        if (r.name == "bios_version") { oversized = true; assert(r.error == -EFBIG && r.value.empty()); }
    }
    assert(temperature && board && invalid && power && oversized);
    assert(result.spd.size() == 2 && !result.spd[0].error && result.spd[0].bytes.size() == 1024 && result.spd[1].error == -EFBIG);
    std::vector<std::uint8_t> bytes{1, 2, 3};
    assert(octool::platform::readBoundedFile(root, bytes) == -EINVAL && bytes.empty());
    assert(octool::platform::readBoundedFile(root + "/fifo", bytes) == -EINVAL && bytes.empty());
}
}
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--inventory-fixture") { inventory(argv[2]); return 0; }
    emit = argc == 2 && std::string(argv[1]) == "--emit";
    invalidBeforeOpen(); registerCommands(); ocAndCurveCommands(); voltageCommands(); vfCommands(); pciCommands(); verifiedControls(); rawRegisters(); activityWindowCommands();
    assert(quote("\"\\\n\t") == "\"\\\"\\\\\\u000a\\u0009\"");
    assert(quote(std::string("\xff\xc0\x80", 3)) == "\"\\ufffd\\ufffd\\ufffd\"");
    if (!emit) std::cout << "10 CLI scenario groups passed (79 rejected commands, real core with simulated devices)\n";
}
