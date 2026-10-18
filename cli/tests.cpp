// SPDX-License-Identifier: GPL-2.0-only
#include "commands.h"
#include "json.h"
#include "platform/linux_inventory_native.h"
#include "platform/linux_amd_pm.h"
#include "tests/amd_pm_fixture.h"
#include "tests/amd_curve_fixture.h"
#include "tests/intel_oc_fixture.h"
#include "tests/intel_controls_fixture.h"
#include "tests/intel_turbo_fixture.h"
#include "tests/intel_uncore_fixture.h"
#include "tests/intel_energy_fixture.h"
#include "tests/intel_avx_fixture.h"
#include "tests/intel_bclk_voltage_fixture.h"
#include "tests/intel_iccmax_fixture.h"
#include "tests/intel_thermal_fixture.h"
#include "tests/intel_limits_fixture.h"
#include "tests/intel_memory_fixture.h"
#include "tests/intel_clock_fixture.h"
#include "tests/intel_tvb_fixture.h"
#include "tests/intel_tvb_table_fixture.h"
#include "tests/spd_fixture.h"
#include "tests/pstates_electrical_fixture.h"
#include <cassert>
#include <cerrno>
#include <iostream>
#include <map>
#include <sstream>
#include <fstream>
#include <unistd.h>

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

void iccMaxCommands() {
    IntelIccMaxFixture d;
    auto result=command(d,{"intel-iccmax-read","--cpu","130"},0);
    assert(result.find("\"encoded_limit_amps\":100")!=std::string::npos && !d.settingsAttempts());
    for (const char *amps : {"1","100.25","125.5","511.75","511.75"}) {
        result=command(d,{"intel-iccmax-set","--cpu","130","--amps",amps,"--apply"},0);
        assert(result.find("\"verified\":true")!=std::string::npos && !d.wrongCpu);
    }
    assert(d.settingsAttempts()==4);
    for (unsigned mode=0;mode<6;++mode) {
        IntelIccMaxFixture r;
        if (mode==0) r.locked=true;
        if (mode==1) r.data|=0x80000000u;
        if (mode>=2) r.data=(r.data&~0x7ffu)|(mode-2);
        result=command(r,{"intel-iccmax-read","--cpu","130"},0);
        assert(result.find("\"editable\":false")!=std::string::npos);
        result=command(r,{"intel-iccmax-set","--cpu","130","--amps","125.25","--apply"},3);
        assert(!r.settingsAttempts() && result.find("\"write_attempted\":false")!=std::string::npos);
    }
    for (unsigned mode=0;mode<10;++mode) {
        IntelIccMaxFixture r;
        if (mode==0) r.model=0xb7;
        if (mode==1) r.stepping=3;
        if (mode==2) r.hypervisor=true;
        if (mode==3) r.msr=false;
        if (mode==4) r.topology|=0x1000u;
        if (mode==5) { r.failCommand=4; r.status=128; }
        if (mode==6) { r.failCommand=0x16; r.status=255; }
        if (mode==7) { r.failCommand=0x17; r.status=254; }
        if (mode==8) r.discardChange=true;
        if (mode==9) r.afterRequest=[&] { const auto &q=r.requests.back(); if (q.write && ((q.value>>32)&255)==0x16) r.topology^=1; };
        result=command(r,{"intel-iccmax-set","--cpu","130","--amps","125.25","--apply"},3);
        assert(result.find("\"verified\":false")!=std::string::npos && r.settingsAttempts()==unsigned(mode==7 || mode==8));
    }
}
void iccMaxInvalidBeforeOpen() {
    bool opened=false;
    auto factory=[&] { opened=true; return std::unique_ptr<HardwareBackend>(new IntelIccMaxFixture); };
    for (const char *amps : {"0","0.75","512","100.1","nan","inf","-1","1 A","","1e999"}) {
        std::ostringstream out;
        assert(run({"intel-iccmax-set","--cpu","130","--amps",amps,"--apply"},out,factory)==2 && !opened);
        if (emit) std::cout<<out.str();
    }
    for (auto args : std::vector<std::vector<std::string>>{
        {"intel-iccmax-read"},{"intel-iccmax-read","--cpu","130","--amps","1"},
        {"intel-iccmax-set","--cpu","130","--amps","100.25"},
        {"intel-iccmax-set","--cpu","130","--apply"},
        {"intel-iccmax-set","--cpu","130","--amps","100.25","--apply","--domain","core"}}) {
        std::ostringstream out; assert(run(args,out,factory)==2 && !opened);
        if (emit) std::cout<<out.str();
    }
}
void invalidBeforeOpen() {
    const std::vector<std::vector<std::string>> invalid = {
        {"intel-set","--cpu","130","--field","pl4","--value","0","--apply"},
        {"intel-set","--cpu","130","--field","pl4","--value","-1","--apply"},
        {"intel-set","--cpu","130","--field","pl4","--value","8192","--apply"},
        {"intel-set","--cpu","130","--field","pl4","--value","nan","--apply"},
        {"intel-set","--cpu","130","--field","pl4","--value","inf","--apply"},
        {"intel-set","--cpu","130","--field","pl4","--value","100"},
        {"intel-limits-read"},
        {"intel-limits-read","--cpu","0","--apply"},
        {"intel-limits-read","--cpu","0","--reason","all"},
        {"intel-limits-clear","--cpu","0","--reason","all"},
        {"intel-limits-clear","--cpu","0","--apply"},
        {"intel-limits-clear","--cpu","0","--reason","invalid","--apply"},
        {"intel-limits-clear","--cpu","0","--reason","","--apply"},
        {"intel-limits-clear","--cpu","0","--reason","all","--reason","thermal","--apply"},
        {"intel-limits-clear","--cpu","0","--reason","all","--value","0","--apply"},
        {"intel-limits-clear","--cpu","-1","--reason","all","--apply"},
        {"intel-thermal-read"},
        {"intel-thermal-read","--cpu","0","--apply"},
        {"intel-thermal-read","--cpu","0","--offset-degrees","1"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","1"},
        {"intel-thermal-set","--cpu","0","--apply"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","64","--apply"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","-1","--apply"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","1.5","--apply"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","1","--offset-degrees","2","--apply"},
        {"intel-thermal-set","--cpu","0","--offset-degrees","1","--offset","2","--apply"},
        {"intel-avx-read"},
        {"intel-avx-read","--cpu","0","--apply"},
        {"intel-avx-read","--cpu","0","--offset","1"},
        {"intel-avx-set","--cpu","0","--offset","1"},
        {"intel-avx-set","--cpu","0","--apply"},
        {"intel-avx-set","--cpu","0","--offset","32","--apply"},
        {"intel-avx-set","--cpu","0","--offset","-1","--apply"},
        {"intel-avx-set","--cpu","0","--offset","1.5","--apply"},
        {"intel-avx-set","--cpu","0","--offset","1","--offset","2","--apply"},
        {"intel-avx-set","--cpu","0","--offset","1","--domain","cache","--apply"},
        {"intel-bclk-voltage-read"},
        {"intel-bclk-voltage-read","--cpu","0","--apply"},
        {"intel-bclk-voltage-read","--cpu","0","--enabled","1"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","1"},
        {"intel-bclk-voltage-set","--cpu","0","--apply"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","2","--apply"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","-1","--apply"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","1.5","--apply"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","1","--enabled","2","--apply"},
        {"intel-bclk-voltage-set","--cpu","0","--enabled","1","--domain","cache","--apply"},
        {"intel-energy-read"},
        {"intel-energy-read", "--cpu", "0", "--apply"},
        {"intel-energy-read", "--cpu", "0", "--field", "efficient-turbo"},
        {"intel-energy-read", "--cpu", "-1"},
        {"intel-energy-set"},
        {"intel-energy-set", "--cpu", "0", "--field", "efficient-turbo", "--value", "0"},
        {"intel-energy-set", "--cpu", "0", "--value", "0", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "unknown", "--value", "0", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "prochot", "--value", "0", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "efficient-turbo", "--value", "2", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--value", "-1", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--value", "0.5", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--value", "4294967296", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--field", "efficient-turbo", "--value", "0", "--apply"},
        {"intel-energy-set", "--cpu", "0", "--field", "race-to-halt", "--value", "0", "--mode", "override", "--apply"},
        {"intel-tvb-table", "--cpu", "0", "--for-edit"},
        {"intel-tvb-table", "--cpu", "0", "--for-edit", "--active-cores", "0"},
        {"intel-tvb-table-set"},
        {"intel-tvb-table-set", "--cpu", "0", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "80"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--value", "80", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "unknown", "--value", "80", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "101", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-b", "--value", "101", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "down-bins-a", "--value", "11", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "down-bins-b", "--value", "11", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "-1", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "1.5", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "4294967296", "--apply"},
        {"intel-tvb-table-set", "--cpu", "0", "--active-cores", "1", "--field", "temperature-a", "--value", "80", "--apply", "--for-edit"},
        {"intel-tvb-table"},
        {"intel-tvb-table", "--cpu", "0", "--apply"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "0"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "9"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "-1"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "1.5"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "4294967296"},
        {"intel-tvb-table", "--cpu", "0", "--active-cores", "1", "--active-cores", "2"},
        {"intel-tvb-table", "--cpu", "0", "--domain", "core"},
        {"intel-tvb-read"},
        {"intel-tvb-read", "--cpu", "0", "--apply"},
        {"intel-tvb-read", "--cpu", "0", "--field", "ratio-clipping"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--value", "0"},
        {"intel-tvb-set", "--cpu", "0", "--value", "0", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "vmax", "--value", "0", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ring-down-bin", "--value", "2", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ring-down-bin", "--value", "-1", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ring-down-bin", "--value", "1.0", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ring-down-bin", "--value", "4294967296", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ring-down-bin", "--value", "0"},
        {"intel-tvb-set", "--cpu", "0", "--field", "vmax-stress", "--value", "0", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--value", "2", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--value", "-1", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--value", "1.0", "--apply"},
        {"intel-tvb-set", "--cpu", "0", "--field", "ratio-clipping", "--value", "1", "--value", "0", "--apply"},
        {"intel-uncore-read"},
        {"intel-uncore-read", "--cpu", "0", "--apply"},
        {"intel-uncore-read", "--cpu", "0", "--minimum-ratio", "8"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8", "--maximum-ratio", "45"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--maximum-ratio", "45", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "0", "--maximum-ratio", "45", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "46", "--maximum-ratio", "45", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8", "--maximum-ratio", "128", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "-1", "--maximum-ratio", "45", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8.5", "--maximum-ratio", "45", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8", "--maximum-ratio", "4294967296", "--apply"},
        {"intel-uncore-set", "--cpu", "0", "--minimum-ratio", "8", "--maximum-ratio", "45", "--maximum-ratio", "50", "--apply"},
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
        {"intel-set", "--cpu", "0", "--field", "hwp-window-us", "--value", "0.5", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-max", "--value", "256", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-min", "--value", "-1", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-epp", "--value", "nan", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-window-us", "--value", "1270000001", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-window-us", "--value", "0.5", "--apply"},
        {"intel-set", "--cpu", "0", "--field", "hwp-package-desired", "--value", "32"},
        {"intel-turbo-read", "--core-type", "p"},
        {"intel-turbo-read", "--cpu", "0"},
        {"intel-turbo-read", "--cpu", "0", "--core-type", "core"},
        {"intel-turbo-read", "--cpu", "0", "--core-type", "p", "--apply"},
        {"intel-turbo-read", "--cpu", "0", "--core-type", "p", "--group", "0"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "60"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--value", "60", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "8", "--value", "60", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "-1", "--value", "60", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "0", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "86", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "1.5", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "nan", "--apply"},
        {"intel-turbo-set", "--cpu", "0", "--core-type", "p", "--group", "0", "--value", "60", "--num-cores", "1", "--apply"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--for-edit"},
        {"intel-vf-read", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "-50"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "-50"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "0", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "16", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "-1", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--point", "8", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "fabric", "--point", "8", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "nan", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "inf", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "1000", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "-1000.01", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "1,5", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--point", "9", "--value", "-50", "--apply"},
        {"intel-vf-set", "--cpu", "0", "--domain", "core", "--point", "8", "--value", "-50", "--for-edit", "--apply"},
        {"amd-umc-decode"}, {"amd-umc-decode", "--file", ""},
        {"amd-umc-decode", "--file", "x", "--cpu", "0"}, {"amd-umc-decode", "--file", "x", "--apply"},
        {"amd-umc-decode", "--file", "x", "--timeout-ms", "100"}, {"amd-umc-decode", "--file", "x", "--file", "y"},
        {"amd-umc-diff"}, {"amd-umc-diff", "--file", "x"}, {"amd-umc-diff", "--compare", "x"},
        {"amd-umc-diff", "--file", "x", "--compare", ""}
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
        values[0xce] = UINT64_C(0x8765432140002a00);
        values[0x606] = 0xa0e03; values[0x610] = UINT64_C(0x0800800000008000) | 1000;
        values[0x770] = 1; values[0x771] = 0x08203040; values[0x774] = 0x80004008;
        values[0x611] = 0x10000; values[0x639] = 0x4000;
        values[0x777] = UINT64_C(0x8765432100000005);
        values[0x601] = UINT64_C(0x8765432140006800);
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
        else if (leaf == 1) { out.words[0] = amd ? 0xb00f20 : 0xb0670; out.words[3] = 1u << 5; }
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

struct LegacyPstates : Registers {
    unsigned signature = 0x00800f11, failLeaf = UINT32_MAX, failMsr = 0;
    bool advertised = true, extended = true, hypervisor = false;
    LegacyPstates() {
        amd = true; expectedCpu = 641;
        values[0xc0010061] = UINT64_C(0xfedcba9876543271);
        const std::uint64_t base = UINT64_C(0xa5a5a5a9abcdc000);
        const unsigned bits[] = {0x08a0,0x0cab,0x09a5,0x08a0,0x00a0,0x080f,0x1ba0,0x2ea0};
        for (unsigned i = 0; i < 8; ++i) values[0xc0010064+i] = (base | bits[i]) & (i == 3 ? ~(UINT64_C(1)<<63) : UINT64_MAX);
    }
    Reply execute(const Request &r) override {
        assert(!r.write && r.cpu == expectedCpu && r.space == Space::Msr);
        if (r.address == failMsr) { ++reads; Reply out; out.error = -EIO; out.value = UINT64_MAX; return out; }
        return Registers::execute(r);
    }
    CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        assert(cpu == expectedCpu && subleaf == 0);
        if (leaf == failLeaf) { ++cpuCalls; CpuIdReply out; out.error = -EACCES; for (auto &word : out.words) word = UINT32_MAX; return out; }
        auto out = Registers::cpuid(cpu, leaf, subleaf);
        if (leaf == 1) { out.words[0] = signature; out.words[2] = hypervisor ? UINT32_C(1) << 31 : 0; }
        if (leaf == 0x80000007) out.words[3] = advertised ? 128 : 0;
        if (leaf == 0x80000000 && !extended) out.words[0] = 0x80000006;
        return out;
    }
};

void legacyPstateCommands() {
    const auto signature = [](unsigned family, unsigned model) {
        return ((family-15)<<20) | ((model & 0xf0)<<12) | 0xf00 | ((model & 15)<<4) | 1;
    };
    for (unsigned family : {0x17u,0x19u}) for (unsigned model : {0x01u,0x31u,0x71u}) {
        LegacyPstates d; d.signature = signature(family, model);
        const auto result = command(d, {"amd-pstates","--cpu","641"}, 0);
        assert(result.find("\"layout\":\"fid8-dfs6\"") != std::string::npos);
        assert(result.find("\"frequency_mhz\":4000") != std::string::npos);
        assert(result.find("\"frequency_status\":\"divider_off\"") != std::string::npos);
        assert(d.reads == 9 && !d.writes);
    }
    for (unsigned family : {0x17u,0x19u}) for (unsigned mode = 0; mode < 10; ++mode) {
        LegacyPstates d; d.signature = signature(family,1);
        if (mode < 4) d.failLeaf = mode == 0 ? 0 : mode == 1 ? 1 : mode == 2 ? 0x80000000 : 0x80000007;
        if (mode == 4) d.failMsr = 0xc0010061;
        if (mode == 5) d.failMsr = 0xc0010064;
        if (mode == 6) d.failMsr = 0xc001006b;
        if (mode == 7) d.advertised = false;
        if (mode == 8) d.extended = false;
        if (mode == 9) d.values[0xc0010061] = 0x23;
        command(d, {"amd-pstates","--cpu","641"}, 3);
        assert(!d.writes);
        assert(d.reads == (mode == 4 || mode == 9 ? 1u : mode == 5 || mode == 6 ? 9u : 0u));
    }
    for (unsigned family : {0x16u,0x18u,0x1bu}) {
        LegacyPstates d; d.signature = signature(family,1);
        command(d, {"amd-pstates","--cpu","641"}, 3);
        assert(!d.reads && !d.writes);
    }
}
void electricalPstateCommands() {
    for (unsigned signature : {0x00810f11u,0x00810f81u,0x00820f01u,0x008a0f01u,0x00a50f01u})
        for (unsigned mode = 0; mode < 4; ++mode) {
            LegacyPstates d; d.signature = signature; d.expectedCpu = 642;
            d.values[0xc0010061] = mode == 3 ? 0x10 : 0x70;
            for (unsigned i = 0; i < 8; ++i) d.values[0xc0010064+i] = pstateElectricalFixture(signature == 0x008a0f01u, i);
            d.hypervisor = mode == 1; if (mode == 2) d.failMsr = 0xc0010066;
            const auto report = command(d, {"amd-pstates","--cpu","642"}, mode == 2 ? 3 : 0);
            assert(report.find("\"electrical_is_measurement\":false") != std::string::npos);
            assert(d.reads == (mode == 3 ? 3u : 9u) && d.cpuCalls == 4 && !d.writes);
        }
    for (unsigned signature : {0x00800f11u,0x008a0f11u,0x00a60f11u,0x00a70f41u,0x00a70f81u,0x00b00f21u,0x00a50f11u}) {
        LegacyPstates d; d.signature = signature; d.expectedCpu = 642;
        d.values[0xc0010061] = 0;
        d.values[0xc0010064] = pstateElectricalFixture(false, 2);
        const auto report = command(d, {"amd-pstates","--cpu","642"}, 0);
        assert(report.find("\"voltage_status\":\"unsupported_model\"") != std::string::npos);
        assert(d.reads == 2 && !d.writes);
    }
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
void energyCommands() {
    for (unsigned bits = 0; bits < 4; ++bits) {
        IntelEnergyFixture d; d.regs[0x1fc] |= std::uint64_t(bits) << 19;
        command(d, {"intel-energy-read", "--cpu", "130"}, 0);
        assert(!d.writes && d.reads == 1 && !d.wrongCpu);
    }
    for (const char *field : {"efficient-turbo", "race-to-halt"})
        for (auto raw : {UINT64_C(0), UINT64_MAX, UINT64_C(0xa5c3211098600243)}) for (unsigned enabled : {0,1}) {
            IntelEnergyFixture d; d.regs[0x1fc] = raw;
            const auto mask = UINT64_C(1) << (std::string(field) == "efficient-turbo" ? 19 : 20);
            const auto expected = enabled ? raw & ~mask : raw | mask;
            const std::vector<std::string> args = {"intel-energy-set", "--cpu", "130", "--field", field, "--value", std::to_string(enabled), "--apply"};
            const auto result = command(d, args, 0);
            assert(result.find("\"verified\":true") != std::string::npos && d.regs[0x1fc] == expected);
            assert(d.writes == unsigned(expected != raw));
            const auto unchanged = command(d, args, 0);
            assert(unchanged.find("\"unchanged\":true") != std::string::npos && d.writes == unsigned(expected != raw) && !d.wrongCpu);
            for (const auto &r : d.requests) assert(r.address == 0x1fc);
        }
    for (const char *field : {"efficient-turbo", "race-to-halt"}) for (unsigned mode = 0; mode < 12; ++mode) {
        IntelEnergyFixture d;
        if (mode == 0) d.hypervisor = true;
        if (mode == 1) d.model = 0x8f;
        if (mode == 2) d.amd = true;
        if (mode == 3) d.hasMsr = false;
        if (mode == 4) d.failRead = 0;
        if (mode == 5) d.afterRequest = [&](const Request &r) { if (!r.write && d.reads == 1) d.regs[0x1fc] ^= UINT64_C(1) << 63; };
        if (mode == 6) d.failRead = 1;
        if (mode == 7) d.failRead = 2;
        if (mode == 8) d.discardChange = true;
        if (mode == 9) d.failWrite = 0;
        if (mode == 10) d.failRead = 3;
        if (mode == 11) d.afterRequest = [&](const Request &r) { if (r.write) d.stepping = 1; };
        const auto result = command(d, {"intel-energy-set", "--cpu", "130", "--field", field, "--value", "0", "--apply"}, 3);
        assert(result.find("\"verified\":false") != std::string::npos);
        assert(d.writes == (mode >= 8 ? 1u : 0u) && !d.wrongCpu);
        if (mode <= 3) assert(d.requests.empty());
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        IntelEnergyFixture d;
        if (mode == 0) d.hypervisor = true;
        if (mode == 1) d.model = 0xad;
        if (mode == 2) d.hasMsr = false;
        if (mode == 3) d.failRead = 0;
        command(d, {"intel-energy-read", "--cpu", "130"}, 3); assert(!d.writes);
    }
}
void uncoreCommands() {
    const std::vector<std::string> args = {"intel-uncore-set", "--cpu", "130", "--minimum-ratio", "12", "--maximum-ratio", "42", "--apply"};
    for (unsigned model : {0xb7, 0x8f}) {
        IntelUncoreFixture d; d.model = model; const auto original = d.regs[0x620];
        command(d, {"intel-uncore-read", "--cpu", "130"}, 0);
        assert(!d.writes && !d.wrongCpu);
        const auto result = command(d, args, 0);
        assert(result.find("\"verified\":true") != std::string::npos && d.writes == 1);
        assert((original & ~UINT64_C(0x7f7f)) == (d.regs[0x620] & ~UINT64_C(0x7f7f)));
        const auto unchanged = command(d, args, 0);
        assert(unchanged.find("\"unchanged\":true") != std::string::npos && d.writes == 1 && !d.wrongCpu);
    }
    for (unsigned mode = 0; mode < 9; ++mode) {
        IntelUncoreFixture d;
        if (mode == 0) d.hypervisor = true;
        if (mode == 1) d.model = 0xad;
        if (mode == 2) d.failRead = 0;
        if (mode == 3) d.failWrite = 0;
        if (mode == 4) d.discardChange = true;
        if (mode == 5) d.afterRequest = [&](const Request &r) { if (!r.write && d.reads == 1) d.regs[0x620] ^= UINT64_C(1) << 40; };
        if (mode == 6) d.afterRequest = [&](const Request &r) { if (r.write) d.regs[0x620] ^= UINT64_C(1) << 63; };
        if (mode == 7) d.regs[0x620] = 0;
        if (mode == 8) d.failRead = 3;
        const auto result = command(d, args, 3);
        assert(result.find("\"verified\":false") != std::string::npos);
        assert(d.writes == (mode == 3 || mode == 4 || mode == 6 || mode == 8 ? 1u : 0u));
        if (mode <= 1) assert(d.requests.empty());
    }
    IntelUncoreFixture malformed; malformed.regs[0x620] = 0;
    const auto result = command(malformed, {"intel-uncore-read", "--cpu", "130"}, 0);
    assert(result.find("\"editable\":false") != std::string::npos && !malformed.writes);
}
void turboCommands() {
    for (const char *kind : {"p", "e"}) {
        IntelTurboFixture d;
        const auto original = d.regs;
        command(d, {"intel-turbo-read", "--cpu", "130", "--core-type", kind}, 0);
        assert(!d.writes && !d.wrongCpu);
        const char *ratio = std::string(kind) == "p" ? "56" : "40";
        const std::vector<std::string> args = {"intel-turbo-set", "--cpu", "130", "--core-type", kind, "--group", "3", "--value", ratio, "--apply"};
        const auto result = command(d, args, 0);
        assert(result.find("\"verified\":true") != std::string::npos && d.writes == 1);
        const auto unchanged = command(d, args, 0);
        assert(unchanged.find("\"unchanged\":true") != std::string::npos && d.writes == 1);
        assert(d.regs[0x1ae] == original.at(0x1ae) && d.regs[0x651] == original.at(0x651));
        command(d, {"intel-turbo-set", "--cpu", "130", "--core-type", kind, "--group", "3", "--value", "60", "--apply"}, 3);
        assert(d.writes == 1 && !d.wrongCpu);
    }
    for (unsigned mode = 0; mode < 9; ++mode) {
        IntelTurboFixture d;
        if (mode == 0) d.regs[0x194] |= UINT64_C(1) << 20;
        if (mode == 1) d.regs[0xce] &= ~(UINT64_C(1) << 28);
        if (mode == 2) d.discardChange = true;
        if (mode == 3) d.afterRequest = [&](const Request &r) {
            if (r.address == 0x1ae && d.reads == 4) d.regs[0x1ad] ^= UINT64_C(1) << 48;
        };
        if (mode == 4) d.failRead = 0;
        if (mode == 5) d.model = 0x8f;
        if (mode == 6) d.failWrite = 0;
        if (mode == 7) d.regs[0x1ae] = 0;
        if (mode == 8) d.afterRequest = [&](const Request &r) { if (r.write) d.regs[0x1ae] ^= UINT64_C(1) << 56; };
        const auto result = command(d, {"intel-turbo-set", "--cpu", "130", "--core-type", "p", "--group", "3", "--value", "56", "--apply"}, 3);
        assert(result.find("\"verified\":false") != std::string::npos);
        assert(d.writes == (mode == 2 || mode == 6 || mode == 8 ? 1u : 0u));
    }
    IntelTurboFixture nonhybrid; nonhybrid.hybrid = false;
    command(nonhybrid, {"intel-turbo-read", "--cpu", "130", "--core-type", "e"}, 3);
    assert(nonhybrid.requests.empty());
    IntelTurboFixture malformed; malformed.regs[0x1ae] = 0;
    const auto result = command(malformed, {"intel-turbo-read", "--cpu", "130", "--core-type", "p"}, 0);
    assert(result.find("\"layout_valid\":false") != std::string::npos && !malformed.writes);
}
void turboThresholdCommands() {
    for (const char *kind : {"p","e"}) {
        IntelTurboFixture d; const unsigned addr = std::string(kind) == "p" ? 0x1ae : 0x651;
        d.regs[addr] = UINT64_C(0x100e0c0a08060402); const auto before = d.regs;
        const std::vector<std::string> args = {"intel-turbo-set","--cpu","130","--core-type",kind,"--group","3","--field","active-cores","--value","9","--apply"};
        auto result = command(d,args,0); assert(d.writes == 1 && result.find("\"verified\":true") != std::string::npos);
        assert(d.regs[addr] == UINT64_C(0x100e0c0a09060402) && d.regs[0x1ad] == before.at(0x1ad) && d.regs[0x650] == before.at(0x650));
        result = command(d,args,0); assert(d.writes == 1 && result.find("\"unchanged\":true") != std::string::npos);
    }
    for (unsigned mode = 0; mode < 9; ++mode) {
        IntelTurboFixture d; unsigned group = mode == 0 ? 7 : 3;
        if (mode == 1) d.regs[0x651] &= ~(UINT64_C(255)<<24);
        if (mode == 2) d.regs[0x194] |= UINT64_C(1)<<20;
        if (mode == 3) d.discardChange = true;
        if (mode == 4) d.afterRequest = [&](const Request &r) { if (r.address == 0x651 && d.reads == 4) d.regs[0x650] ^= UINT64_C(1)<<48; };
        if (mode == 5) d.afterRequest = [&](const Request &r) { if (r.write) d.regs[0x650] ^= UINT64_C(1)<<48; };
        if (mode == 6) d.failWrite = 0;
        const auto value = mode == 7 ? "10" : mode == 8 ? "6" : "9";
        const auto result = command(d,{"intel-turbo-set","--cpu","130","--core-type","e","--group",std::to_string(group),"--field","active-cores","--value",value,"--apply"},3);
        assert(result.find("\"verified\":false") != std::string::npos && d.writes == (mode == 3 || mode == 5 || mode == 6 ? 1u : 0u));
    }
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (const char *value : {"0","256","1.5","nan","inf","-1"}) {
        std::ostringstream out;
        assert(run({"intel-turbo-set","--cpu","130","--core-type","p","--group","0","--field","active-cores","--value",value,"--apply"},out,factory) == 2);
        if (emit) std::cout << out.str();
    }
    for (const auto &args : {std::vector<std::string>{"intel-turbo-set","--cpu","130","--core-type","p","--group","0","--field","cores","--value","1","--apply"},
        std::vector<std::string>{"intel-turbo-set","--cpu","130","--core-type","p","--group","0","--field","active-cores","--value","1"},
        std::vector<std::string>{"intel-turbo-read","--cpu","130","--core-type","p","--field","active-cores"}}) {
        std::ostringstream out; assert(run(args,out,factory) == 2); if (emit) std::cout << out.str();
    }
    assert(!opens);
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
void vfEditingCommands() {
    for (const std::string domain : {"core", "cache"}) {
        IntelVfEditFixture d;
        command(d, {"intel-vf-read", "--cpu", "130", "--domain", domain, "--point", "8", "--for-edit"}, 0);
        auto args = std::vector<std::string>{"intel-vf-set", "--cpu", "130", "--domain", domain, "--point", "8", "--value", "-50", "--apply"};
        const auto changed = command(d, args, 0); assert(d.mutationCount == 1 && !d.wrongCpu);
        assert(changed.find("\"verified\":true") != std::string::npos);
        const auto same = command(d, args, 0); assert(d.mutationCount == 1);
        assert(same.find("\"unchanged\":true") != std::string::npos);
        args[8] = "0"; command(d, args, 0); assert(d.mutationCount == 2);
    }
    for (unsigned mode = 0; mode < 13; ++mode) {
        IntelVfEditFixture d; unsigned selectedQueries = 0;
        if (mode == 0) d.locked = true;
        if (mode == 1) d.control |= 8;
        if (mode == 2) d.settings[0] |= 1u << 8;
        if (mode == 3) d.settings[0] |= 1u << 20;
        if (mode == 4) d.settings[0] |= 1u << 21;
        if (mode == 5) d.discardChange = true;
        if (mode == 6) d.failAt = 0;
        if (mode == 7) d.model = 0x8f;
        if (mode == 8) { d.failCommand = 0x11; d.status = 0xfe; }
        if (mode == 9) { d.failCommand = 0x14; d.status = 3; }
        if (mode >= 10) d.afterRequest = [&] {
            const auto &r = d.requests.back(); const auto command = (r.value >> 32) & 255;
            if (mode == 10 && r.write && command == 0x10 && ((r.value >> 48) & 255) == 8 && ++selectedQueries == 1) d.vfSettings[0][8] ^= 1;
            if (mode == 11 && r.write && command == 0x11) d.control ^= 1;
            if (mode == 12 && r.write && command == 0x11) d.vfSettings[0][8] ^= 1;
        };
        const auto output = command(d, {"intel-vf-set", "--cpu", "130", "--domain", "core", "--point", "8", "--value", "-50", "--apply"}, 3);
        const bool attempted = mode == 5 || mode == 8 || mode == 11 || mode == 12;
        assert(output.find(attempted ? "\"write_attempted\":true" : "\"write_attempted\":false") != std::string::npos);
        assert(output.find("\"verified\":false") != std::string::npos);
    }
    IntelVfEditFixture locked; locked.locked = true;
    command(locked, {"intel-vf-read", "--cpu", "130", "--domain", "core", "--point", "8", "--for-edit"}, 0);
    assert(!locked.mutationCount);
    IntelVfEditFixture cache; cache.control |= 8;
    command(cache, {"intel-vf-read", "--cpu", "130", "--domain", "cache", "--point", "8", "--for-edit"}, 0);
    command(cache, {"intel-vf-set", "--cpu", "130", "--domain", "cache", "--point", "8", "--value", "-50", "--apply"}, 0);
    assert(cache.control == 0x12345008 && cache.mutationCount == 1);
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
void tvbTableCommands() {
    for (unsigned scenario = 0; scenario < 18; ++scenario) {
        IntelTvbTableFixture device;
        std::vector<std::string> args{"intel-tvb-table", "--cpu", "130"};
        if (scenario >= 1 && scenario <= 8) {
            args.push_back("--active-cores"); args.push_back(std::to_string(scenario));
        }
        if (scenario == 9) { device.entries[0] = 0; device.entries[7] = UINT32_MAX; device.locked = true; }
        if (scenario == 10) device.statuses[1] = 128;
        if (scenario == 11) device.statuses.fill(5);
        if (scenario == 12) device.model = 0x8f;
        if (scenario == 13) device.hypervisor = true;
        if (scenario == 14) device.msr = false;
        if (scenario == 15) device.failAt = 0;
        if (scenario == 16) device.busyEntry = 4;
        if (scenario == 17) device.afterRequest = [&] {
            if (device.selectors.size() == 3) device.failAt = int(device.calls);
        };
        const auto out = command(device,args,scenario <= 9 ? 0 : 3);
        assert(out.find("\"settings_write_attempted\":false") != std::string::npos);
        assert(device.mutations == 0 && !device.wrongCpu);
        for (const auto &r : device.requests) if (r.write) assert(((r.value >> 32) & 255) == 0x24);
        if (scenario == 10) assert(device.selectors.back() == 8);
        if (scenario >= 12 && scenario <= 15) assert(device.requests.empty());
    }
}
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
void offlineUmcDoesNotCreateBackend() {
    char path[] = "/tmp/octool-umc-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    const auto write = [&](const std::string &bytes) { std::ofstream file(path, std::ios::binary); file << bytes; file.close(); assert(file); };
    unsigned opens = 0;
    const auto factory = [&]() { ++opens; return std::unique_ptr<HardwareBackend>(); };
    const std::string bytes = R"({"format":"octool-amd-umc-v1","bank":0,"refresh_slot":0,"registers":[{"offset":516,"value":34}]})";
    write(bytes);
    for (const auto &args : std::vector<std::vector<std::string>>{{"amd-umc-decode", "--file", path}, {"amd-umc-diff", "--file", path, "--compare", path}}) {
        std::ostringstream out; assert(!run(args, out, factory));
        assert(out.str().find("\"hardware_accessed\":false") != std::string::npos);
    }
    for (const auto &invalid : {std::string("{}"), std::string(65537, ' ')}) {
        write(invalid); std::ostringstream out; assert(run({"amd-umc-decode", "--file", path}, out, factory) == 3);
    }
    std::vector<std::uint8_t> buffer{1};
    assert(octool::platform::readBoundedFile(path, buffer, 0) == -EINVAL && buffer.empty());
    assert(octool::platform::readBoundedFile(path, buffer, 65537) == -EINVAL);
    assert(octool::platform::readBoundedFile(std::string(path) + '\0' + "suffix", buffer, 65536) == -EINVAL);
    assert(!opens);
}
void offlineSpdDoesNotCreateBackend() {
    char path[] = "/tmp/octool-spd-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0;
    const auto factory = [&]() { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 101; ++scenario) {
        auto bytes = octool::test::spdFixture();
        int expected = 0;
        if (scenario == 1) { bytes[720] ^= 1; expected = 3; }
        if (scenario == 2) { bytes[660] ^= 1; expected = 3; }
        if (scenario == 3) { bytes.resize(800); expected = 3; }
        if (scenario == 4) bytes.resize(512);
        if (scenario == 5) { bytes[642] = 0x31; octool::test::spdSeal(bytes,640,62); expected = 3; }
        if (scenario == 6) bytes = octool::test::spdExpoFixture();
        if (scenario == 7) { bytes[655] = 0xff; octool::test::spdSeal(bytes,640,62); }
        if (scenario == 8) { bytes.resize(4097); expected = 3; }
        if (scenario >= 9) bytes = octool::test::spdExpoFixture();
        if (scenario == 9) bytes[640] = 0;
        if (scenario == 10) { bytes[852] ^= 1; expected = 3; }
        if (scenario == 11) { bytes.resize(959); expected = 3; }
        if (scenario == 12) { bytes[836] = 0x11; octool::test::spdSeal(bytes,832,126); expected = 3; }
        if (scenario == 13) { bytes[837] = 0x10; octool::test::spdWord(bytes,846,0); octool::test::spdSeal(bytes,832,126); }
        if (scenario == 14) { octool::test::spdWord(bytes,846,0); octool::test::spdSeal(bytes,832,126); expected = 3; }
        if (scenario == 15) {
            bytes[837] = 0; std::fill(bytes.begin()+842,bytes.begin()+922,std::uint8_t(0xff)); octool::test::spdSeal(bytes,832,126);
        }
        if (scenario == 16) { bytes[643] = 7; octool::test::spdSeal(bytes,640,62); expected = 3; }
        if (scenario == 17) { bytes[660] ^= 1; expected = 3; }
        if (scenario == 18) { octool::test::spdWord(bytes,886,0); octool::test::spdSeal(bytes,832,126); expected = 3; }
        if (scenario >= 19) bytes = octool::test::spdDdr4XmpFixture();
        if (scenario == 20) {
            bytes[386] = 2; bytes[388] = 15;
            std::fill(bytes.begin()+393,bytes.begin()+440,std::uint8_t(0xff));
        }
        if (scenario == 21) { bytes[388] = 1; expected = 3; }
        if (scenario == 22) { bytes.resize(486); expected = 3; }
        if (scenario == 23) { bytes[387] = 0x21; expected = 3; }
        if (scenario == 24) { bytes[396] = 1; bytes[431] = 0x80; expected = 3; }
        if (scenario == 25) bytes[384] = 0;
        if (scenario == 26) bytes.resize(385);
        if (scenario == 27) { bytes[389] = 4; expected = 3; }
        if (scenario == 28) { bytes.resize(392); expected = 3; }
        if (scenario >= 29) bytes = octool::test::spdFixture();
        if (scenario == 29 || scenario == 30) {
            std::fill(bytes.begin()+735,bytes.begin()+759,std::uint8_t(scenario == 29 ? 0 : 255));
            octool::test::spdSeal(bytes,704,62);
        }
        if (scenario == 31) { bytes[736] ^= 1; expected = 3; }
        if (scenario == 32) {
            bytes[736] ^= 1; bytes[643] = 6; octool::test::spdSeal(bytes,640,62);
        }
        if (scenario == 33) {
            octool::test::spdWord(bytes,735,1); bytes[737] = 255; octool::test::spdSeal(bytes,704,62);
        }
        if (scenario == 34) {
            std::fill(bytes.begin()+24,bytes.begin()+29,std::uint8_t(0)); octool::test::spdSeal(bytes,0,510);
            std::fill(bytes.begin()+711,bytes.begin()+716,std::uint8_t(0)); bytes[764] = 0; octool::test::spdSeal(bytes,704,62);
        }
        if (scenario == 35) {
            std::fill(bytes.begin()+711,bytes.begin()+716,std::uint8_t(255)); bytes[764] = 0xf3; octool::test::spdSeal(bytes,704,62);
        }
        if (scenario == 36) { bytes[764] = 0xa4; octool::test::spdSeal(bytes,704,62); }
        if (scenario == 37) { bytes[711] ^= 1; expected = 3; }
        if (scenario == 38) { bytes[1] = 0x20; octool::test::spdSeal(bytes,0,510); }
        if (scenario >= 39) bytes = octool::test::spdDdr4XmpFixture();
        if (scenario == 39 || scenario == 40) {
            bytes[23] |= std::uint8_t(scenario == 39 ? 0x80 : 0x40); octool::test::spdSeal(bytes,0,126);
        }
        if (scenario == 41) { bytes[20] ^= 1; expected = 3; }
        if (scenario >= 42) bytes = octool::test::spdBaseTimingsFixture(true);
        if (scenario == 43 || scenario == 46) {
            bytes[1] = std::uint8_t(scenario == 43 ? 0x11 : 0x20); octool::test::spdSeal(bytes,0,510);
        }
        if (scenario == 44 || scenario == 45) {
            std::fill(bytes.begin()+70,bytes.begin()+103,std::uint8_t(scenario == 44 ? 255 : 0));
            octool::test::spdSeal(bytes,0,510);
        }
        if (scenario == 47) bytes.resize(236);
        if (scenario == 48) { bytes[70] ^= 1; expected = 3; }
        if (scenario >= 49) bytes = octool::test::spdBaseTimingsFixture(false);
        if (scenario == 50 || scenario == 51) {
            std::fill(bytes.begin()+41,bytes.begin()+46,std::uint8_t(scenario == 50 ? 255 : 0));
            octool::test::spdSeal(bytes,0,126);
        }
        if (scenario == 52) { bytes[17] = 1; octool::test::spdSeal(bytes,0,126); }
        if (scenario == 53) { bytes[41] ^= 1; expected = 3; }
        if (scenario >= 54) bytes = octool::test::spdProfileGeometryFixture(false);
        if (scenario == 55) bytes[386] &= ~1u;
        if (scenario == 56) { bytes[388] = 1; expected = 3; }
        if (scenario == 57) { bytes.resize(440); expected = 3; }
        if (scenario >= 58) bytes = octool::test::spdProfileGeometryFixture(true);
        if (scenario == 59) { bytes[192] = 0x20; octool::test::spdSeal(bytes,0,510); }
        if (scenario == 60 || scenario == 61) { bytes[235] = std::uint8_t(scenario == 60 ? 0x62 : 0x82); octool::test::spdSeal(bytes,0,510); }
        if (scenario == 62) { bytes[1] = 0x20; octool::test::spdSeal(bytes,0,510); }
        if (scenario == 63) { bytes[48] ^= 1; expected = 3; }
        if (scenario >= 64) {
            bytes = octool::test::spdProfileGeometryFixture(false); bytes[4] = std::uint8_t(scenario == 64 ? 8 : 9);
            if (scenario == 65 || scenario == 66) bytes[6] = std::uint8_t(scenario == 65 ? 0xb2 : 0xb1);
            if (scenario == 67) bytes[1] = 0x20;
            octool::test::spdSeal(bytes,0,126);
        }
        if (scenario >= 68) { bytes = octool::test::spdUserFixture(); expected = 0; }
        if (scenario == 69) octool::test::spdZeroCrcUserSlot(bytes);
        if (scenario == 70) { bytes[917] ^= 1; expected = 3; }
        if (scenario == 71) bytes.resize(950);
        if (scenario == 72 || scenario == 73) std::fill(bytes.begin()+896,bytes.end(),std::uint8_t(scenario == 72 ? 0 : 255));
        if (scenario == 74) { octool::test::spdWord(bytes,901,0); octool::test::spdSeal(bytes,896,62); expected = 3; }
        if (scenario == 75 || scenario == 76) bytes = octool::test::spdUserFixture(true);
        if (scenario == 76) { bytes[958] ^= 1; expected = 3; }
        if (scenario == 77) { bytes[660] ^= 1; expected = 3; }
        if (scenario == 78) { bytes[642] = 0x31; octool::test::spdSeal(bytes,640,62); expected = 3; }
        if (scenario == 79) { bytes[643] = 0xf8; octool::test::spdSeal(bytes,640,62); }
        if (scenario >= 80) {
            bytes = octool::test::spdUserFixture(true);
            for (unsigned i = 0; i < 16; ++i) octool::test::spdWord(bytes,866+40*(i/8)+2*(i%8),1000+17*i);
            if (scenario == 81) bytes[837] = 0x10;
            if (scenario == 82) { bytes[836] = 0x11; expected = 3; }
            octool::test::spdSeal(bytes,832,126);
        }
        if (scenario >= 83) {
            bytes = octool::test::spdAsymmetricFixture();
            if (scenario == 84) bytes[8] = 0;
            if (scenario == 85) bytes[10] = 0xe0;
            if (scenario == 86) bytes[192] = 0x20;
            if (scenario == 89) { bytes[234] = 0x48; std::swap(bytes[4],bytes[8]); std::swap(bytes[6],bytes[10]); }
            if (scenario == 90) { bytes[234] &= ~0x40; bytes[8] = 255; bytes[10] = 255; }
            octool::test::spdSeal(bytes,0,510);
            if (scenario == 87) { bytes[10] ^= 1; expected = 3; }
            if (scenario == 88) bytes.resize(236);
        }
        if (scenario >= 91) {
            bytes = octool::test::spdDdr4AsymmetricFixture();
            if (scenario == 92) { bytes[6] = 0x91; bytes[10] = 4; }
            if (scenario == 93) { bytes[6] = 0xf2; bytes[10] = 0xe6; bytes[12] = 0x49; }
            if (scenario == 94) bytes[10] = 0x9d;
            if (scenario == 95) { bytes[10] = 0xe6; bytes[12] = 0x49; }
            if (scenario == 99) bytes[1] = 0x20;
            if (scenario == 100) { bytes[12] &= ~0x40; bytes[10] = 255; }
            octool::test::spdSeal(bytes,0,126);
            if (scenario == 96) { bytes[10] ^= 1; expected = 3; }
            if (scenario == 97) bytes.resize(128);
            if (scenario == 98) { bytes.resize(127); expected = 3; }
        }
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())); file.close(); assert(file);
        std::ostringstream out; assert(run({"spd-decode", "--file", path},out,factory) == expected);
        if (emit) std::cout << out.str();
        if (!scenario) assert(out.str().find("\"name\":\"Performance\"") != std::string::npos);
    }
    assert(!opens);
}
void intelClockCommands() {
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        intel_clock_fixture::Device d;
        if (scenario == 1) d.clock[0x5f60] = 0;
        if (scenario == 2) d.clock[0x5e04] |= 1u<<14;
        if (scenario == 3) d.clock[0x5e04] = 0;
        if (scenario == 4) d.model = 0xba;
        if (scenario == 5) d.pciId = 0xa7068086;
        if (scenario == 6) d.bar &= ~UINT64_C(1);
        if (scenario == 7) d.clock[0x5e00] |= 1u<<31;
        if (scenario == 8) d.failRead = 7;
        if (scenario == 9) d.afterRequest = [&](const Request &) { if (d.reads == 8) d.clock[0x5f60] ^= 1; };
        const auto report = command(d,{"intel-clock-read","--cpu","130"},scenario < 4 ? 0 : 3);
        assert(!d.writes && !d.wrongCpu);
        if (scenario >= 4) assert(report.find("\"clock\":null") != std::string::npos);
    }
}
void intelClockOffline() {
    char path[] = "/tmp/octool-clock-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    const auto capture = intelClockJson(decodeIntelClock(intel_clock_fixture::registers()));
    for (const auto &args : std::vector<std::vector<std::string>>{
        {"intel-clock-read"},{"intel-clock-read","--cpu","-1"},{"intel-clock-read","--cpu","1048576"},
        {"intel-clock-read","--cpu","130","--apply"},{"intel-clock-read","--cpu","130","--controller","1"},
        {"intel-clock-read","--cpu","130","--address","0x5f60"},{"intel-clock-read","--cpu","130","--cpu","131"},
        {"intel-clock-decode"},{"intel-clock-decode","--file",""},{"intel-clock-decode","--file",path,"--cpu","130"}}) {
        std::ostringstream out; assert(run(args,out,factory) == 2);
    }
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        auto text = capture;
        if (scenario == 1) text = "{\"schema_version\":1,\"command\":\"intel-clock-read\",\"ok\":true,\"error\":0,\"data\":{\"clock\":"+capture+"}}";
        if (scenario == 2) text.replace(text.find("\"reported_khz\":101250"),21,"\"reported_khz\":999999");
        if (scenario == 3) { auto r = intel_clock_fixture::registers(); r[2].raw = UINT64_C(0x8000000000018b82); text = intelClockJson(decodeIntelClock(r)); }
        if (scenario == 4) text = "{}";
        if (scenario == 5) text.replace(text.find("0x0000000049cc1120"),18,"0x0000000049cc112");
        if (scenario == 6) text.replace(text.find("0x00005e04"),10,"0x00005e00");
        if (scenario == 7) text.replace(text.find("0x0000000000140017"),18,"0x0000000080140017");
        if (scenario == 9) text = "{\"bad\":\"\xed\xa0\x80\"}";
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out;
        assert(run({"intel-clock-decode","--file",scenario == 8 ? std::string(path)+"-missing" : path},out,factory) == (scenario < 4 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryCommands() {
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        intel_memory_fixture::Device d;
        if (scenario == 1) d.controller = d.channel = 1;
        if (scenario == 2) { d.controller = 1; d.memory[0xd800] = 0; }
        if (scenario == 3) d.memory[0xe088] |= UINT64_C(1)<<30;
        if (scenario == 4) d.model = 0xba;
        if (scenario == 5) d.pciId = 0xa7068086;
        if (scenario == 6) d.bar &= ~UINT64_C(1);
        if (scenario == 7) d.memory[0xd80c] = 0x10001800;
        if (scenario == 8) d.failRead = 10;
        if (scenario == 9) d.afterRequest = [&](const Request &) { if (d.reads == 19) d.memory[0xe000] ^= 1; };
        const auto report = command(d,{"intel-memory-read","--cpu","130","--controller",std::to_string(d.controller),"--channel",std::to_string(d.channel)},scenario < 4 ? 0 : 3);
        assert(!d.writes && !d.wrongCpu);
        if (scenario >= 4) assert(report.find("\"memory\":null") != std::string::npos);
    }
}
void intelMemoryOffline() {
    char path[] = "/tmp/octool-imc-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    const auto capture = intelMemoryJson(decodeIntelMemory({},intel_memory_fixture::registers()));
    for (const auto &args : std::vector<std::vector<std::string>>{
        {"intel-memory-read"},{"intel-memory-read","--cpu","130"},
        {"intel-memory-read","--cpu","130","--controller","2","--channel","0"},
        {"intel-memory-read","--cpu","130","--controller","0","--channel","-1"},
        {"intel-memory-read","--cpu","130","--controller","0","--channel","0","--apply"},
        {"intel-memory-read","--cpu","130","--controller","0","--channel","0","--address","0xfedc0000"},
        {"intel-memory-read","--cpu","130","--controller","0","--channel","0","--channel","1"},
        {"intel-memory-decode"},{"intel-memory-decode","--file",""},{"intel-memory-decode","--file",path,"--cpu","130"}}) {
        std::ostringstream out; assert(run(args,out,factory) == 2);
    }
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        auto text = capture;
        if (scenario == 1) text = "{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":" + capture + "}}";
        if (scenario == 2) text.replace(text.find("\"encoded\":36"),12,"\"encoded\":99");
        if (scenario == 3) { auto v = intel_memory_fixture::registers(); v[3].raw |= UINT64_C(1)<<30; text = intelMemoryJson(decodeIntelMemory({},v)); }
        if (scenario == 4) text = "{}";
        if (scenario == 5) text.replace(text.find("0x814160500042402a"),18,"0x814160500042402");
        if (scenario == 6) text.replace(text.find("0x0000e454"),10,"0x0000d800");
        if (scenario == 7) text.replace(text.find("0x0000000000000003"),18,"0x0000000000000000");
        if (scenario == 9) text = "{\"bad\":\"\xed\xa0\x80\"}";
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out;
        assert(run({"intel-memory-decode","--file",scenario == 8 ? std::string(path)+"-missing" : path},out,factory) == (scenario < 4 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryExtendedCaptures() {
    char path[] = "/tmp/octool-imc-extended-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto registers = intel_memory_fixture::registers(scenario == 1 ? 1 : 0,scenario < 2 ? 1 : 2);
        if (scenario == 2) { registers[2].raw = 8; registers[12].raw = UINT64_C(0xffffffc034000000); }
        if (scenario == 3) { registers[3].raw &= ~(UINT64_C(31)<<56); registers[3].raw |= UINT64_C(1)<<30; }
        auto text = intelMemoryJson(decodeIntelMemory({},registers,scenario < 2 ? 1 : 2));
        if (scenario == 4) text.replace(text.find("memory-v2"),9,"memory-v1");
        if (scenario == 5) text.replace(text.find("0x0000e050"),10,"0x0000e058");
        if (scenario == 6) text.replace(text.find("0xf0e0d0c034302c28"),18,"0xf0e0d0c034302c00");
        if (scenario == 7) text.replace(text.find("0x34092c6585a78608"),18,"0xffffffffffffffff");
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out; assert(run({"intel-memory-decode","--file",path},out,factory) == (scenario < 4 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryDdrioCaptures() {
    char path[] = "/tmp/octool-imc-ddrio-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        auto registers = intel_memory_fixture::registers(scenario == 1 ? 0 : 1,4);
        if (scenario == 2) registers[18].raw = 0;
        if (scenario == 3) registers[3].raw |= UINT64_C(1)<<30;
        auto text = intelMemoryJson(decodeIntelMemory({},registers,4));
        if (scenario == 4) {
            text.replace(text.find("\"encoded\":21",text.find("DDRIO_self_refresh_latency")),12,"\"encoded\":99");
            text.replace(text.find("8*tCK"),5,"ns");
        }
        if (scenario == 5) text = "{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+text+"}}";
        if (scenario == 6) text.replace(text.find("memory-v4"),9,"memory-v3");
        if (scenario == 7) text.replace(text.find("0x0000e0b8"),10,"0x0000e0bc");
        if (scenario == 8) text.replace(text.find("0x00000000b9355953"),18,"0x00000001b9355953");
        if (scenario == 9) text.replace(text.find("0x00000000b9355953"),18,"0x00000000ffffffff");
        if (scenario == 10) text.replace(text.find("0x0000e0b8"),10,"0x0000e4c0");
        if (scenario == 11) text.replace(text.find("memory-v4"),9,"memory-v5");
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out; assert(run({"intel-memory-decode","--file",path},out,factory) == (scenario < 6 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryOdtCaptures() {
    char path[] = "/tmp/octool-imc-odt-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 16; ++scenario) {
        auto registers = intel_memory_fixture::registers(scenario == 1 ? 0 : 1,5);
        if (scenario == 2) registers[19].raw = registers[20].raw = 0;
        if (scenario == 3) registers[3].raw |= (UINT64_C(1)<<30)|(UINT64_C(1)<<16);
        if (scenario == 4) registers[2].raw = 8;
        if (scenario == 5) registers[2].raw = 15;
        auto text = intelMemoryJson(decodeIntelMemory({},registers,5));
        if (scenario == 6) {
            text.replace(text.find("\"encoded\":2",text.find("ODT_read_rank0_mask")),11,"\"encoded\":15");
            text.replace(text.find("\"encoded\":45",text.find("Write_delay_add_tCWL")),12,"\"encoded\":99");
            text.replace(text.find("odt_pin_mask"),12,"ohms");
        }
        if (scenario == 7) text = "{\"schema_version\":1,\"command\":\"intel-memory-decode\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+text+"}}";
        if (scenario == 8) text.replace(text.find("memory-v5"),9,"memory-v4");
        if (scenario == 9) text.replace(text.find("0x0000e478"),10,"0x0000e47c");
        if (scenario == 10) text.replace(text.find("0x000000007bde4812"),18,"0x000000017bde4812");
        if (scenario == 11) text.replace(text.find("0x00000000a5a51b4d"),18,"0x00000001a5a51b4d");
        if (scenario == 12) text.replace(text.find("0x000000007bde4812"),18,"0x00000000ffffffff");
        if (scenario == 13) text.replace(text.find("0x00000000a5a51b4d"),18,"0x00000000ffffffff");
        if (scenario == 14) text.replace(text.find("0x0000e080"),10,"0x0000e478");
        if (scenario == 15) text.replace(text.find("memory-v5"),9,"memory-v6");
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out; assert(run({"intel-memory-decode","--file",path},out,factory) == (scenario < 8 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryOdtLive() {
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        intel_memory_fixture::Device d; d.controller = 1;
        if (scenario == 0) { d.channel = 1; d.memory[0xe454] = 8; d.memory[0xe080] = d.memory[0xe478] = 0; }
        if (scenario == 1) d.memory[0xd800] = 0;
        const int failures[] = {24,25,51,52};
        if (scenario >= 2 && scenario <= 5) d.failRead = failures[scenario-2];
        if (scenario >= 6) d.afterRequest = [&](const Request &) { if (d.reads == 27) d.memory[scenario == 6 ? 0xe080 : 0xe478] ^= 1; };
        const auto report = command(d,{"intel-memory-read","--cpu","130","--controller","1","--channel",std::to_string(d.channel)},scenario < 2 ? 0 : 3);
        assert(!d.writes && !d.wrongCpu);
        if (scenario < 2) assert(d.reads == 59 && d.cpuCalls == 3);
        else assert(report.find("\"memory\":null") != std::string::npos);
    }
}
void intelMemoryTemperatureCaptures() {
    char path[] = "/tmp/octool-imc-temperature-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        auto registers = intel_memory_fixture::registers(scenario == 1 ? 1 : 0); registers[2].raw = 15;
        if (scenario == 2) registers[21].raw = 0;
        if (scenario == 3) registers[2].raw = 4;
        if (scenario == 4) registers[3].raw |= UINT64_C(1)<<30;
        auto text = intelMemoryJson(decodeIntelMemory({},registers));
        const auto replace = [&](const std::string &from, const std::string &to) {
            const auto p = text.find(from); assert(p != std::string::npos); text.replace(p,from.size(),to);
        };
        if (scenario == 5) {
            replace("\"temperature_class\":\"hot\"","\"temperature_class\":\"cold\"");
            replace("\"required_refresh_multiplier\":2","\"required_refresh_multiplier\":1");
            replace("\"temperature_freshness_verified\":false","\"temperature_freshness_verified\":true");
        }
        if (scenario == 6) text = "{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+text+"}}";
        if (scenario == 7) replace("memory-v6","memory-v5");
        if (scenario == 8) replace("0x0000e428","0x0000e424");
        if (scenario == 9) replace("0x0000000003020100","0x0000000103020100");
        if (scenario == 10) replace("0x0000000003020100","0x00000000ffffffff");
        if (scenario == 11) replace("0x0000e428","0x0000e478");
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out; assert(run({"intel-memory-decode","--file",path},out,factory) == (scenario < 7 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}
void intelMemoryTemperatureLive() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        intel_memory_fixture::Device d; d.controller = 1; d.memory[0xd800] = 0; d.memory[0xe454] = 15;
        if (scenario == 1) d.memory[0xe428] = 0;
        if (scenario == 2) d.failRead = 26;
        if (scenario == 3) d.failRead = 53;
        if (scenario == 4) d.afterRequest = [&](const Request &) { if (d.reads == 27) d.memory[0xe428] ^= 1; };
        if (scenario == 5) d.memory[0xe428] = UINT32_MAX;
        const auto report = command(d,{"intel-memory-read","--cpu","130","--controller","1","--channel","0"},scenario < 2 ? 0 : 3);
        assert(!d.writes && !d.wrongCpu);
        if (scenario < 2) assert(d.reads == 59 && d.cpuCalls == 3);
        else assert(report.find("\"memory\":null") != std::string::npos);
    }
}
void intelMemoryRefreshCaptures() {
    char path[] = "/tmp/octool-imc-refresh-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        auto registers = intel_memory_fixture::registers(scenario == 1 ? 0 : 1,3);
        if (scenario == 2) for (unsigned i = 14; i < 18; ++i) registers[i].raw = 0;
        if (scenario == 3) registers[3].raw |= UINT64_C(1)<<16;
        auto text = intelMemoryJson(decodeIntelMemory({},registers,3));
        if (scenario == 4) { text.replace(text.find("\"encoded\":66"),12,"\"encoded\":99"); text.replace(text.find("1024_DCLK"),9,"ns"); }
        if (scenario == 5) text = "{\"schema_version\":1,\"command\":\"intel-memory-read\",\"ok\":true,\"error\":0,\"data\":{\"memory\":"+text+"}}";
        if (scenario == 6) text.replace(text.find("memory-v3"),9,"memory-v2");
        if (scenario == 7) text.replace(text.find("0x0000e440"),10,"0x0000e438");
        if (scenario == 8) text.replace(text.find("0x00000000426a8713"),18,"0x00000001426a8713");
        if (scenario == 9) text.replace(text.find("0xa000045680049401"),18,"0xffffffffffffffff");
        if (scenario == 10) text.replace(text.find("0xe290000000001234"),18,"0xe29000000000123");
        if (scenario == 11) text.replace(text.find("memory-v3"),9,"memory-v4");
        std::ofstream file(path,std::ios::binary); file << text; file.close(); assert(file);
        std::ostringstream out; assert(run({"intel-memory-decode","--file",path},out,factory) == (scenario < 6 ? 0 : 3));
        if (emit) std::cout << out.str();
    }
    assert(!opens);
}

void pmCommands() {
    char path[] = "/tmp/octool-pm-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    const auto text = [&](const std::string &s) { std::ofstream f(path, std::ios::binary); f << s; f.close(); assert(f); };
    const auto binary = [&](const std::vector<std::uint8_t> &b) { text(std::string(b.begin(), b.end())); };
    const auto invoke = [&](const std::vector<std::string> &args, int code) {
        std::ostringstream out; assert(run(args, out, factory) == code);
        if (emit) std::cout << out.str();
        return out.str();
    };
    for (const auto &args : std::vector<std::vector<std::string>>{
        {"amd-pm-decode"}, {"amd-pm-decode","--file",""}, {"amd-pm-decode","--file",path,"--version","0"},
        {"amd-pm-decode","--file",path,"--version","0x730205"}, {"amd-pm-decode","--file",path,"--version","4294967296"},
        {"amd-pm-decode","--file",path,"--version","0x730204","--version","0x730404"},
        {"amd-pm-decode","--file",path,"--cpu","0"}, {"amd-pm-decode","--file",path,"--apply"},
        {"amd-pm-decode","--file",path,"--bclk-mhz","0"}, {"amd-pm-decode","--file",path,"--bclk-mhz","-1"},
        {"amd-pm-decode","--file",path,"--bclk-mhz","nan"}, {"amd-pm-decode","--file",path,"--bclk-mhz","1e999"},
        {"amd-pm-read","--file",path}, {"amd-pm-read","--version","0x730204"}, {"amd-pm-read","--cpu","0"},
        {"amd-pm-read","--apply"}, {"amd-pm-read","--timeout-ms","100"}, {"amd-pm-read","--bclk-mhz"," 100"}}) {
        std::ostringstream out; assert(run(args, out, factory) == 2);
    }
    for (const auto &layout : amdPmLayouts()) {
        binary(pm_fixture::bytes(layout.bytes));
        invoke({"amd-pm-decode","--file",path,"--version",std::to_string(layout.version)}, 0);
    }
    auto bytes = pm_fixture::bytes(); binary(bytes);
    invoke({"amd-pm-decode","--file",path,"--version","0x730204","--bclk-mhz","102.5"}, 0);
    pm_fixture::put(bytes, 0x20c, 0x7f800001); binary(bytes);
    invoke({"amd-pm-decode","--file",path,"--version","0x730204"}, 3);
    const auto capture = amdPmJson(decodeAmdPm(pm_fixture::bytes(), 0x730204)); text(capture);
    const auto report = invoke({"amd-pm-decode","--file",path}, 0);
    text(report); invoke({"amd-pm-decode","--file",path}, 0);
    binary(std::vector<std::uint8_t>(2843)); invoke({"amd-pm-decode","--file",path,"--version","0x730204"}, 3);
    binary(std::vector<std::uint8_t>(2845)); invoke({"amd-pm-decode","--file",path,"--version","0x730204"}, 3);
    text("{}"); invoke({"amd-pm-decode","--file",path}, 3);
    invoke({"amd-pm-decode","--file",std::string(path) + "-missing"}, 3);
    text(capture); invoke({"amd-pm-decode","--file",path,"--bclk-mhz","102.5"}, 0);
    auto tampered = capture; const auto pos = tampered.find("\"stored_value\":2000"); assert(pos != std::string::npos);
    tampered.replace(pos, std::string("\"stored_value\":2000").size(), "\"stored_value\":99999"); text(tampered);
    invoke({"amd-pm-decode","--file",path}, 0);
    tampered = capture; tampered.replace(tampered.find("\"schema\":1"), 10, "\"schema\":2"); text(tampered);
    invoke({"amd-pm-decode","--file",path}, 3);
    text("{\"bad\":\"\xed\xa0\x80\"}"); invoke({"amd-pm-decode","--file",path}, 3);
    assert(!opens);
}
void pmLimitCommands() {
    char path[] = "/tmp/octool-pm-limits-XXXXXX"; const int fd = mkstemp(path); assert(fd >= 0); close(fd);
    struct Remove { const char *path; ~Remove() { unlink(path); } } cleanup{path};
    unsigned opens = 0; const auto factory = [&] { ++opens; return std::unique_ptr<HardwareBackend>(); };
    const auto text = [&](const std::string &s) { std::ofstream f(path,std::ios::binary); f << s; f.close(); assert(f); };
    const auto binary = [&](const std::vector<std::uint8_t> &b) { text(std::string(b.begin(),b.end())); };
    const auto invoke = [&](std::vector<std::string> options, int code) {
        std::vector<std::string> args{"amd-pm-decode","--file",path}; args.insert(args.end(),options.begin(),options.end());
        std::ostringstream out; assert(run(args,out,factory)==code);
        if (emit) std::cout << out.str();
        return out.str();
    };
    for (const auto &layout : amdPmLayouts()) {
        binary(pm_fixture::bytes(layout.bytes));
        invoke({"--version",std::to_string(layout.version)},0);
    }
    const unsigned offsets[] = {0,0x3c,0x1c8,0x1e4};
    const std::uint32_t invalid[] = {0x7f800000,0xff800000,0x7fc12345,0x7f800001};
    for (unsigned i=0; i<4; ++i) {
        auto b=pm_fixture::bytes(); pm_fixture::put(b,offsets[i],invalid[i]); binary(b);
        invoke({"--version","0x730204"},3);
    }
    auto b=pm_fixture::bytes(); const std::uint32_t finite[] = {0,0x80000000,0xbf800000,1,0x7f7fffff};
    for (unsigned i=0; i<5; ++i) pm_fixture::put(b,i*4,finite[i]);
    binary(b); invoke({"--version","0x730204"},0);
    const auto capture=amdPmJson(decodeAmdPm(pm_fixture::bytes(),0x730204));
    const auto extension=capture.find(",\"original_limits_units_verified\""); assert(extension!=std::string::npos);
    text(capture.substr(0,extension)+"}"); invoke({},0);
    auto forged=capture;
    const auto replace=[&](const std::string &before,const std::string &after) {
        const auto pos=forged.find(before); assert(pos!=std::string::npos); forged.replace(pos,before.size(),after);
    };
    replace("\"original_limits_units_verified\":false","\"original_limits_units_verified\":true");
    replace("\"unit\":\"unverified\"","\"unit\":\"W\""); replace("\"stored_value\":400.5","\"stored_value\":1");
    text(forged); const auto report=invoke({},0);
    text(report); invoke({},0);
    text(capture); invoke({"--bclk-mhz","123.25"},0);
    b=pm_fixture::bytes(); b.resize(0x1e7); binary(b); invoke({"--version","0x730204"},3);
    assert(!opens);
}
void pl4Commands() {
    for (const char *watts : {"0.125","100.12","1023.875"}) {
        IntelControlsFixture d; d.expectedCpu=130;
        const std::vector<std::string> args={"intel-set","--cpu","130","--field","pl4","--value",watts,"--apply"};
        command(d,args,0); command(d,args,0);
        assert(d.writes==1 && !d.wrongCpu && (d.regs[0x601]&~UINT64_C(0x1fff))==UINT64_C(0x8765432140006000));
    }
    for (unsigned mode=0;mode<9;++mode) {
        IntelControlsFixture d; d.expectedCpu=130;
        if (mode==0) d.regs[0x601]|=UINT64_C(1)<<31;
        if (mode==1) d.failRead=0;
        if (mode==2) d.failRead=9;
        if (mode==3) d.model=0x8f;
        if (mode==4) d.virtualized=true;
        if (mode==5) d.discardChange=true;
        if (mode==6 || mode==7) d.afterRequest=[&](const Request &r) {
            if (r.write) d.regs[mode==6?0x606:0x601]^=UINT64_C(1)<<(mode==6?0:13);
        };
        command(d,{"intel-set","--cpu","130","--field","pl4","--value",mode==8?"1024":"100","--apply"},3);
        assert(!d.wrongCpu && d.writes==unsigned(mode>=5 && mode<=7));
    }
}

void hwpContextCommands() {
    // Eighteen independent read reports: full samples, capability omissions,
    // four source failures and the disabled/failed HWP enable guard.
    for (unsigned mode=0;mode<18;++mode) {
        IntelControlsFixture d; d.expectedCpu=130; d.enableHwpContext();
        if (mode==1 || mode==2) for (unsigned msr : {0x772,0x775,0x776,0x773,0x777}) d.regs[msr]=mode==1?0:UINT64_MAX;
        if (mode==3) d.packageRequest=false;
        if (mode==4) d.peci=false;
        if (mode==5) d.control=false;
        if (mode==6) d.notifications=false;
        if (mode==7) d.flexible=false;
        if (mode==8) d.highestChange=false;
        if (mode==9) d.epp=false;
        if (mode==10) d.activityWindow=false;
        if (mode==11) d.hwp=false;
        if (mode>=12 && mode<16) d.failRead=int(mode-7);
        if (mode==16) d.regs[0x770]=0;
        if (mode==17) d.failRead=2;
        command(d,{"intel-read","--cpu","130"},mode>=12?3:0);
        assert(!d.writes && !d.wrongCpu);
    }
    for (unsigned mode=0;mode<8;++mode) {
        IntelControlsFixture d; d.expectedCpu=130; d.enableHwpContext(); d.regs[0x775]=0;
        if (mode==1) { d.regs[0x776]=1; d.regs[0x774]|=UINT64_C(1)<<42; }
        if (mode==2) d.regs[0x775]=UINT64_C(1)<<60;
        if (mode==3) d.regs[0x776]=1;
        if (mode==4) d.failRead=6;
        if (mode==5) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x775]=1; };
        if (mode==6) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x776]=1; };
        if (mode==7) d.afterRequest=[&](const Request &r) { if (!r.write && d.reads==14) d.regs[0x775]=1; };
        command(d,{"intel-set","--cpu","130","--field","hwp-epp","--value","192","--apply"},mode<2?0:3);
        assert(d.writes==unsigned(mode<2 || mode==5 || mode==6) && !d.wrongCpu);
        for (const auto &request:d.requests) if (request.write) assert(request.address==0x774);
    }
}

void hwpPackageCommands() {
    const char *fields[]={"hwp-package-min","hwp-package-max","hwp-package-desired","hwp-package-epp","hwp-package-window-us"};
    const char *values[]={"24","48","32","192","15333"};
    const unsigned shifts[]={0,8,16,24,32}, codes[]={24,48,32,192,399};
    for (unsigned i=0;i<5;++i) {
        IntelControlsFixture d; d.model=0xff; d.expectedCpu=130; d.enableHwpContext(); d.regs[0x775]=0;
        const auto before=d.regs;
        for (unsigned noop=0;noop<2;++noop) {
            const auto result=command(d,{"intel-set","--cpu","130","--field",fields[i],"--value",values[i],"--apply"},0);
            assert(result.find("\"shared_package_hint\":true")!=std::string::npos);
            assert(result.find("\"msr\":\"0x00000772\"")!=std::string::npos);
            assert(result.find("\"selection_flags_changed\":false")!=std::string::npos);
            const auto mask=std::uint64_t(i==4?1023:255)<<shifts[i];
            assert(d.regs[0x772]==((before.at(0x772)&~mask)|(std::uint64_t(codes[i])<<shifts[i])));
            assert(d.writes==1 && !d.wrongCpu);
            for (const auto &item:before) if (item.first!=0x772) assert(d.regs.at(item.first)==item.second);
        }
    }
    for (unsigned mode=0;mode<12;++mode) {
        IntelControlsFixture d; d.model=0xff; d.expectedCpu=130; d.enableHwpContext(); d.regs[0x775]=0;
        if (mode==0) d.packageRequest=false;
        if (mode==1) d.hwp=false;
        if (mode==2) d.regs[0x770]=0;
        if (mode==3) d.epp=false;
        if (mode==4) d.regs[0x775]=UINT64_C(1)<<60;
        if (mode==5) d.failRead=3;
        if (mode==6) d.failWrite=0;
        if (mode==7) d.discardChange=true;
        if (mode==8) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x774]^=1; };
        if (mode==9) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x775]^=1; };
        if (mode==10) d.afterRequest=[&](const Request &r) { if (!r.write && d.reads==8) d.regs[0x772]^=UINT64_C(1)<<55; };
        if (mode==11) d.afterRequest=[&](const Request &r) { if (r.write) d.packageRequest=false; };
        const auto result=command(d,{"intel-set","--cpu","130","--field","hwp-package-epp","--value","192","--apply"},3);
        assert(result.find("\"verified\":false")!=std::string::npos && result.find("\"readback_value\":null")!=std::string::npos);
        assert(d.writes==unsigned((mode>=6 && mode<=9) || mode==11) && !d.wrongCpu);
        for (const auto &r:d.requests) if (r.write) assert(r.address==0x772);
    }
}

void hwpHistoryCommands() {
    for (std::uint64_t raw : {UINT64_C(0),UINT64_C(1),UINT64_C(4),UINT64_C(5),UINT64_C(2),UINT64_MAX}) {
        IntelControlsFixture d; d.expectedCpu=130; d.regs[0x777]=raw;
        command(d,{"intel-read","--cpu","130"},0); assert(!d.writes && !d.wrongCpu);
    }
    for (unsigned mode=0;mode<3;++mode) {
        IntelControlsFixture d; d.expectedCpu=130;
        if (mode==0) d.model=0xff;
        if (mode==1) d.hwp=false;
        if (mode==2) d.failRead=8;
        command(d,{"intel-read","--cpu","130"},mode==2?3:0); assert(!d.writes && !d.wrongCpu);
        if (mode==1) for (const auto &r:d.requests) assert(r.address!=0x777);
    }
}
void telemetryCommands() {
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned exponent : {0u,14u,16u,31u}) {
        IntelControlsFixture d; d.model=model; d.expectedCpu=130;
        d.regs[0x606]=(d.regs[0x606]&~UINT64_C(0x1f00))|(std::uint64_t(exponent)<<8);
        command(d,{"intel-read","--cpu","130"},0); assert(!d.writes && !d.wrongCpu);
    }
    for (unsigned mode=0;mode<7;++mode) {
        IntelControlsFixture d; d.expectedCpu=130;
        if (mode==0) d.failRead=0;
        if (mode==1) d.failRead=3;
        if (mode==2) d.failRead=6;
        if (mode==3) d.failRead=7;
        if (mode==4) d.msr=false;
        if (mode==5) d.virtualized=true;
        if (mode==6) d.failCpuid=3;
        command(d,{"intel-read","--cpu","130"},3); assert(!d.writes && !d.wrongCpu);
    }
}
void thermalEventCommands() {
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned bits : {0u,0xfffu,0xaaau,0x555u}) {
        IntelPackageThermalFixture d; d.model=model; d.regs[0x1b1]|=bits;
        command(d,{"intel-read","--cpu","130"},0); assert(!d.writes && !d.wrongCpu);
    }
    for (unsigned model : {0xb7,0x8f,0xcf}) for (bool targetFailed : {false,true}) {
        IntelPackageThermalFixture d; d.model=model; d.regs[0x1b1]|=0xfff;
        d.failRead=targetFailed?2:4;
        command(d,{"intel-read","--cpu","130"},3); assert(!d.writes && !d.wrongCpu);
    }
}
void nonTurboRatioCommands() {
    for (unsigned model : {0xb7,0x8f,0xcf}) for (unsigned ratio : {1u,42u,255u}) {
        IntelControlsFixture d; d.model=model; d.expectedCpu=130;
        d.regs[0xce]=UINT64_C(0x876543214000005a) | (std::uint64_t(ratio)<<8);
        command(d,{"intel-read","--cpu","130"},0); assert(!d.writes && !d.wrongCpu);
    }
    for (unsigned model : {0xb7,0x8f,0xcf}) for (bool denied : {false,true}) {
        IntelControlsFixture d; d.model=model; d.expectedCpu=130;
        if (denied) d.failRead=5; else d.regs[0xce]&=~UINT64_C(0xff00);
        command(d,{"intel-read","--cpu","130"},3); assert(!d.writes);
    }
}
void limitsCommands() {
    for (std::uint64_t raw : {UINT64_C(0),UINT64_MAX,UINT64_C(0x8765432100030000)}) {
        IntelLimitsFixture d; d.regs[0x64f]=raw; command(d,{"intel-limits-read","--cpu","130"},0); assert(!d.writes);
    }
    const char *reasons[]={"all","prochot","thermal","residency","ratl","vr-thermal","vr-current","other","pl1","pl2","turbo","transition"};
    for (const char *reason:reasons) {
        IntelLimitsFixture d; d.regs[0x64f]=UINT64_MAX;
        command(d,{"intel-limits-clear","--cpu","130","--reason",reason,"--apply"},0); assert(d.writes==1 && !d.wrongCpu);
    }
    IntelLimitsFixture noop; noop.regs[0x64f]=0;
    command(noop,{"intel-limits-clear","--cpu","130","--reason","all","--apply"},0); assert(!noop.writes);
    IntelLimitsFixture events; events.regs[0x64f]=UINT64_C(1)<<16; events.newEvents=UINT64_C(1)<<17;
    command(events,{"intel-limits-clear","--cpu","130","--reason","prochot","--apply"},0);
    assert(events.regs[0x64f]==(UINT64_C(1)<<17) && events.writes==1);
    for (unsigned mode=0;mode<4;++mode) {
        IntelLimitsFixture d;
        if (mode==0) d.model=0x8f;
        if (mode==1) d.hypervisor=true;
        if (mode==2) d.hasMsr=false;
        if (mode==3) d.failRead=0;
        command(d,{"intel-limits-read","--cpu","130"},3); assert(!d.writes);
    }
    for (unsigned mode=0;mode<8;++mode) {
        IntelLimitsFixture d;
        if (mode==0) d.afterRequest=[&](const Request &r) { if (!r.write && d.reads==1) d.regs[0x64f]^=UINT64_C(1)<<63; };
        if (mode==1) d.discardChange=true;
        if (mode==2) d.failWrite=0;
        if (mode==3) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x64f]^=UINT64_C(1)<<63; };
        if (mode==4) d.afterRequest=[&](const Request &r) { if (r.write) d.stepping=1; };
        if (mode==5) d.reassert=UINT64_C(1)<<16;
        if (mode==6) d.failCpuid=3;
        if (mode==7) d.failRead=1;
        command(d,{"intel-limits-clear","--cpu","130","--reason","all","--apply"},3); assert(d.writes<=1);
    }
}
void thermalCommands() {
    for (unsigned model : {0xb7,0x8f,0xcf}) {
        IntelThermalFixture d; d.model=model; command(d,{"intel-thermal-read","--cpu","130"},0); assert(!d.writes);
    }
    for (unsigned degrees : {0u,1u,7u,63u}) {
        IntelThermalFixture d; const auto before=d.regs[0x1a2];
        command(d,{"intel-thermal-set","--cpu","130","--offset-degrees",std::to_string(degrees),"--apply"},0);
        assert(d.regs[0x1a2]==((before & ~(UINT64_C(0x3f)<<24))|(std::uint64_t(degrees)<<24)) && !d.wrongCpu);
    }
    IntelThermalFixture noop;
    command(noop,{"intel-thermal-set","--cpu","130","--offset-degrees","5","--apply"},0); assert(!noop.writes);
    IntelThermalFixture locked; locked.regs[0x1a2]|=UINT64_C(1)<<31;
    command(locked,{"intel-thermal-read","--cpu","130"},0);
    IntelThermalFixture fixed; fixed.regs[0xce]&=~(UINT64_C(1)<<30);
    command(fixed,{"intel-thermal-read","--cpu","130"},0);
    for (unsigned mode=0;mode<9;++mode) {
        IntelThermalFixture d;
        if (mode==0) d.regs[0x1a2]|=UINT64_C(1)<<31;
        if (mode==1) d.regs[0xce]&=~(UINT64_C(1)<<30);
        if (mode==2) d.hypervisor=true;
        if (mode==3) d.discardChange=true;
        if (mode==4) d.failWrite=0;
        if (mode==5) d.model=0xad;
        if (mode==6) d.afterRequest=[&](const Request &r) { if (r.write) d.regs[0x1a2]^=UINT64_C(1)<<63; };
        if (mode==7) d.regs[0x1a2]&=~(UINT64_C(255)<<16);
        if (mode==8) d.failCpuid=0;
        command(d,{"intel-thermal-set","--cpu","130","--offset-degrees","7","--apply"},3); assert(d.writes<=1);
    }
    for (unsigned mode=0;mode<4;++mode) {
        IntelThermalFixture d;
        if (mode==0) d.dts=false;
        if (mode==1) d.hasMsr=false;
        if (mode==2) d.regs[0x1a2]&=~(UINT64_C(255)<<16);
        if (mode==3) d.failRead=0;
        command(d,{"intel-thermal-read","--cpu","130"},3); assert(!d.writes);
    }
}
void avxCommands() {
    for (std::uint32_t raw : {0u,UINT32_MAX,0xdeadbeefu}) {
        IntelAvxFixture d; d.data=raw; command(d,{"intel-avx-read","--cpu","130"},0);
        assert(!d.settingsAttempts());
    }
    for (unsigned bins : {0u,1u,7u,31u}) {
        IntelAvxFixture d; const auto before=d.data;
        command(d,{"intel-avx-set","--cpu","130","--offset",std::to_string(bins),"--apply"},0);
        assert(d.data==((before & ~0x3e0u)|(bins<<5)) && !d.wrongCpu);
    }
    IntelAvxFixture noop;
    command(noop,{"intel-avx-set","--cpu","130","--offset",std::to_string((noop.data>>5)&31),"--apply"},0);
    assert(!noop.settingsAttempts());
    IntelAvxFixture locked; locked.locked=true;
    command(locked,{"intel-avx-read","--cpu","130"},0);
    for (unsigned mode=0;mode<9;++mode) {
        IntelAvxFixture d;
        if (mode==0) d.locked=true;
        if (mode==1) d.avx2=false;
        if (mode==2) d.hypervisor=true;
        if (mode==3) d.discardChange=true;
        if (mode==4) { d.failCommand=0x1b; d.status=255; }
        if (mode==5) d.model=0x8f;
        if (mode==6) d.afterRequest=[&] { const auto &r=d.requests.back(); if (r.write && ((r.value>>32)&255)==0x1b) d.data ^= 1u<<31; };
        if (mode==7) { d.failCommand=0x1a; d.status=7; }
        if (mode==8) d.failAt=0;
        command(d,{"intel-avx-set","--cpu","130","--offset","7","--apply"},3);
        assert(d.settingsAttempts()<=1);
    }
    for (unsigned mode=0;mode<4;++mode) {
        IntelAvxFixture d;
        if (mode==0) d.avx=false;
        if (mode==1) d.msr=false;
        if (mode==2) { d.failCommand=0x1a; d.status=128; }
        if (mode==3) d.failAt=0;
        command(d,{"intel-avx-read","--cpu","130"},3);
        assert(!d.settingsAttempts());
    }
}
void bclkVoltageCommands() {
    for (std::uint32_t raw : {0u,UINT32_MAX,0xdeadbeefu}) {
        IntelBclkVoltageFixture d; d.data=raw; command(d,{"intel-bclk-voltage-read","--cpu","130"},0);
        assert(!d.settingsAttempts());
    }
    for (unsigned enabled : {0u,1u}) {
        IntelBclkVoltageFixture d; const auto before=d.data;
        command(d,{"intel-bclk-voltage-set","--cpu","130","--enabled",std::to_string(enabled),"--apply"},0);
        assert(d.data==((before & ~4u)|(enabled<<2)) && !d.wrongCpu);
    }
    IntelBclkVoltageFixture noop;
    command(noop,{"intel-bclk-voltage-set","--cpu","130","--enabled",std::to_string((noop.data>>2)&1),"--apply"},0);
    assert(!noop.settingsAttempts());
    IntelBclkVoltageFixture locked; locked.locked=true;
    command(locked,{"intel-bclk-voltage-read","--cpu","130"},0);
    for (unsigned mode=0;mode<9;++mode) {
        IntelBclkVoltageFixture d;
        if (mode==0) d.locked=true;
        if (mode==1) d.msr=false;
        if (mode==2) d.hypervisor=true;
        if (mode==3) d.discardChange=true;
        if (mode==4) { d.failCommand=0x15; d.status=255; }
        if (mode==5) d.model=0x8f;
        if (mode==6) d.afterRequest=[&] { const auto &r=d.requests.back(); if (r.write && ((r.value>>32)&255)==0x15) d.data ^= 1u<<31; };
        if (mode==7) { d.failCommand=0x14; d.status=7; }
        if (mode==8) d.failAt=0;
        command(d,{"intel-bclk-voltage-set","--cpu","130","--enabled","1","--apply"},3);
        assert(d.settingsAttempts()<=1);
    }
    for (unsigned mode=0;mode<4;++mode) {
        IntelBclkVoltageFixture d;
        if (mode==0) d.amd=true;
        if (mode==1) d.msr=false;
        if (mode==2) { d.failCommand=0x14; d.status=128; }
        if (mode==3) d.failAt=0;
        command(d,{"intel-bclk-voltage-read","--cpu","130"},3);
        assert(!d.settingsAttempts());
    }
}
void tvbCommands() {
    for (unsigned bits : {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u}) {
        IntelTvbFixture d; d.data |= bits;
        const auto json = command(d, {"intel-tvb-read", "--cpu", "130"}, 0);
        assert(json.find("\"raw\":\"0xa5c3211") != std::string::npos && !d.settingsAttempts() && !d.wrongCpu);
    }
    for (const char *field : {"ratio-clipping", "voltage-optimization", "ring-down-bin"}) for (unsigned value : {0u, 1u}) {
        IntelTvbFixture d; const unsigned bit = std::string(field) == "ratio-clipping" ? 4 : std::string(field) == "voltage-optimization" ? 8 : 2;
        d.data = (d.data & ~bit) | (value ? bit : 0);
        const auto raw = d.data;
        const auto json = command(d, {"intel-tvb-set", "--cpu", "130", "--field", field, "--value", std::to_string(value), "--apply"}, 0);
        assert(json.find("\"verified\":true") != std::string::npos && d.settingsAttempts() == 1 && !d.wrongCpu);
        assert((d.data & ~bit) == (raw & ~bit) && bool(d.data & bit) == !value);
    }
    for (const char *field : {"ratio-clipping", "voltage-optimization", "ring-down-bin"}) {
        IntelTvbFixture noop;
        const auto json = command(noop, {"intel-tvb-set", "--cpu", "130", "--field", field, "--value", "1", "--apply"}, 0);
        assert(json.find("\"unchanged\":true") != std::string::npos && !noop.settingsAttempts());
    }
    IntelTvbFixture locked; locked.locked = true;
    command(locked, {"intel-tvb-read", "--cpu", "130"}, 0);
    for (const char *field : {"ratio-clipping", "ring-down-bin"}) for (unsigned mode = 0; mode < 7; ++mode) {
        IntelTvbFixture d;
        if (mode == 0) d.locked = true;
        if (mode == 1) d.model = 0x8f;
        if (mode == 2) d.hypervisor = true;
        if (mode == 3) d.discardChange = true;
        if (mode == 4 || mode == 5) { d.failCommand = mode == 4 ? 0x18 : 0x19; d.status = 5; }
        if (mode == 6) d.afterRequest = [&] {
            const auto &r = d.requests.back();
            if (r.write && d.commands.size() == 1) d.data ^= UINT32_C(1) << 31;
        };
        const auto result = command(d, {"intel-tvb-set", "--cpu", "130", "--field", field, "--value", "0", "--apply"}, 3);
        assert(result.find("\"verified\":false") != std::string::npos && d.settingsAttempts() == (mode == 3 || mode == 5 ? 1u : 0u));
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        IntelTvbFixture d;
        if (mode == 0) d.hypervisor = true;
        if (mode == 1) { d.failCommand = 0x18; d.status = 0xfe; }
        if (mode == 2) d.failAt = 6;
        const auto result = command(d, {"intel-tvb-read", "--cpu", "130"}, 3);
        assert(result.find("\"raw\":null") != std::string::npos && !d.settingsAttempts());
    }
}
void tvbTableEditingCommands() {
    for (unsigned entry = 1; entry <= 8; ++entry) {
        IntelTvbTableFixture d;
        const auto result = command(d,{"intel-tvb-table","--cpu","130","--active-cores",std::to_string(entry),"--for-edit"},0);
        assert(result.find("\"editable\":" + std::string(entry == 1 ? "true" : "false")) != std::string::npos);
        assert(!d.tableSettingsAttempts() && !d.wrongCpu);
    }
    IntelTvbTableFixture locked; locked.locked = true;
    command(locked,{"intel-tvb-table","--cpu","130","--active-cores","1","--for-edit"},0);
    const char *fields[] = {"temperature-a","down-bins-a","temperature-b","down-bins-b"};
    const unsigned shifts[] = {8,0,16,24};
    for (unsigned field = 0; field < 4; ++field) for (unsigned value : {0u,field % 2 == 0 ? 100u : 10u}) {
        IntelTvbTableFixture d; const auto previous = d.entries;
        const auto expected = (previous[0] & ~(UINT32_C(255) << shifts[field])) | (std::uint32_t(value) << shifts[field]);
        const auto result = command(d,{"intel-tvb-table-set","--cpu","130","--active-cores","1","--field",fields[field],"--value",std::to_string(value),"--apply"},0);
        assert(result.find("\"verified\":true") != std::string::npos && d.entries[0] == expected);
        assert(d.tableSettingsAttempts() == (expected == previous[0] ? 0u : 1u) && !d.wrongCpu && !d.settingsAttempts());
        for (unsigned n = 1; n < 8; ++n) assert(d.entries[n] == previous[n]);
    }
    IntelTvbTableFixture noop;
    const auto same = command(noop,{"intel-tvb-table-set","--cpu","130","--active-cores","1","--field","temperature-a","--value","70","--apply"},0);
    assert(same.find("\"unchanged\":true") != std::string::npos && !noop.tableSettingsAttempts());
    for (unsigned mode = 0; mode < 10; ++mode) {
        IntelTvbTableFixture d;
        if (mode == 0) d.locked = true;
        if (mode == 1) d.model = 0x8f;
        if (mode == 2) d.hypervisor = true;
        if (mode == 3) d.entries[0] |= UINT32_C(255) << 16;
        if (mode == 4) d.discardChange = true;
        if (mode >= 5 && mode <= 7) { d.failCommand = mode == 5 ? 0x18 : mode == 6 ? 0x24 : 0x25; d.status = 128; }
        if (mode >= 8) d.afterRequest = [&] {
            if (!d.requests.back().write || d.commands.size() != 2) return;
            if (mode == 8) d.entries[0] ^= 1; else d.data ^= 4;
        };
        const auto result = command(d,{"intel-tvb-table-set","--cpu","130","--active-cores","1","--field","temperature-a","--value","80","--apply"},3);
        assert(result.find("\"verified\":false") != std::string::npos);
        assert(d.tableSettingsAttempts() == (mode == 4 || mode == 7 ? 1u : 0u));
    }
}
void pmFilesystem(const std::string &root) {
    const auto reader = octool::platform::linuxAmdPmReader(root);
    const auto good = readAmdPmDriver(reader); assert(!good.snapshot.error && good.snapshot.fields[0].value == 2000);
    std::vector<std::uint8_t> bytes{1};
    assert(reader("../pm_table", bytes, 2844) == -EINVAL && bytes.empty());
    assert(octool::platform::linuxAmdPmReader(std::string(root) + '\0')("pm_table", bytes, 2844) == -EINVAL);
    const auto broken = readAmdPmDriver(octool::platform::linuxAmdPmReader(root + "/bad"));
    assert(good.snapshot.originalLimits.size()==24 && good.snapshot.originalLimits[0].value==400.5);
    assert(broken.snapshot.error == -EINVAL && broken.snapshot.fields.empty() && broken.snapshot.originalLimits.empty());
    const auto huge = readAmdPmDriver(octool::platform::linuxAmdPmReader(root + "/huge"));
    assert(huge.snapshot.error == -EFBIG && huge.snapshot.fields.empty() && huge.snapshot.originalLimits.empty());
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
    if (argc == 3 && std::string(argv[1]) == "--pm-fixture") { pmFilesystem(argv[2]); return 0; }
    emit = argc == 2 && std::string(argv[1]) == "--emit";
    invalidBeforeOpen(); registerCommands(); ocAndCurveCommands(); voltageCommands(); vfCommands(); pciCommands(); verifiedControls(); rawRegisters(); activityWindowCommands(); turboCommands(); vfEditingCommands();
    offlineUmcDoesNotCreateBackend(); uncoreCommands(); offlineSpdDoesNotCreateBackend(); pmCommands(); turboThresholdCommands(); intelMemoryCommands(); intelMemoryOffline(); intelMemoryExtendedCaptures(); intelMemoryRefreshCaptures(); intelMemoryDdrioCaptures(); intelMemoryOdtCaptures(); intelMemoryOdtLive(); intelMemoryTemperatureCaptures(); intelMemoryTemperatureLive(); thermalEventCommands();
    intelClockCommands(); intelClockOffline(); tvbCommands(); tvbTableCommands(); tvbTableEditingCommands(); energyCommands(); avxCommands(); bclkVoltageCommands(); thermalCommands(); limitsCommands(); nonTurboRatioCommands(); telemetryCommands();
    hwpHistoryCommands(); pmLimitCommands();
    iccMaxCommands(); iccMaxInvalidBeforeOpen();
    pl4Commands(); hwpContextCommands(); hwpPackageCommands(); legacyPstateCommands(); electricalPstateCommands();
    assert(quote("\"\\\n\t") == "\"\\\"\\\\\\u000a\\u0009\"");
    assert(quote(std::string("\xff\xc0\x80", 3)) == "\"\\ufffd\\ufffd\\ufffd\"");
    if (!emit) std::cout << "48 CLI scenario groups passed (invalid inputs and real core with simulated devices)\n";
}
