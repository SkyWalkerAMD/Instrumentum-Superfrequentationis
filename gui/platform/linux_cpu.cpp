// SPDX-License-Identifier: GPL-2.0-only
#include "linux_cpu.h"
#include <cerrno>
#include <fstream>
#include <memory>
#include <sched.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace octool {
namespace platform {
namespace {
constexpr unsigned maximumCpuCount = 1024 * 1024;
struct FreeCpuSet { void operator()(cpu_set_t *set) const { CPU_FREE(set); } };
using CpuSet = std::unique_ptr<cpu_set_t, FreeCpuSet>;

int capture(CpuSet &mask, unsigned &capacity)
{
    for (capacity = CPU_SETSIZE; capacity <= maximumCpuCount; capacity *= 2) {
        mask.reset(CPU_ALLOC(capacity));
        if (!mask) return -ENOMEM;
        const auto bytes = CPU_ALLOC_SIZE(capacity);
        CPU_ZERO_S(bytes, mask.get());
        if (sched_getaffinity(0, bytes, mask.get()) == 0) return 0;
        if (errno != EINVAL) return -errno;
    }
    return -ERANGE;
}

std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace

std::string parseLinuxCpuModel(const std::string &cpuinfo)
{
    std::istringstream lines(cpuinfo);
    std::string line;
    while (std::getline(lines, line)) {
        const auto colon = line.find(':');
        if (colon != std::string::npos && trim(line.substr(0, colon)) == "model name") {
            const auto model = trim(line.substr(colon + 1));
            if (!model.empty()) return model;
        }
    }
    return {};
}

SystemInfo systemInfo()
{
    SystemInfo info;
    std::ifstream input("/proc/cpuinfo");
    if (input) {
        std::ostringstream text; text << input.rdbuf();
        info.cpuModel = parseLinuxCpuModel(text.str());
    }
    info.onlineCpus = sysconf(_SC_NPROCESSORS_ONLN);
    CpuSet mask; unsigned capacity = 0;
    info.affinityError = capture(mask, capacity);
    if (!info.affinityError) {
        for (unsigned i = 0; i < capacity; ++i)
            if (CPU_ISSET_S(i, CPU_ALLOC_SIZE(capacity), mask.get())) info.allowedCpus.push_back(i);
    }
    struct stat metadata{};
    info.moduleLoaded = stat("/sys/module/octool_hwio", &metadata) == 0 && S_ISDIR(metadata.st_mode);
    info.helperInstalled = stat("/opt/octool/bin/octool-hwio-helper", &metadata) == 0 &&
        S_ISREG(metadata.st_mode) && access("/opt/octool/bin/octool-hwio-helper", X_OK) == 0;
    return info;
}

int onLinuxCpu(unsigned cpu, const std::function<int()> &operation)
{
    if (cpu >= maximumCpuCount) return -ERANGE;
    CpuSet previous; unsigned capacity = 0;
    int error = capture(previous, capacity);
    if (error) return error;
    if (cpu >= capacity) return -ERANGE;
    const auto bytes = CPU_ALLOC_SIZE(capacity);
    CpuSet selected(CPU_ALLOC(capacity));
    if (!selected) return -ENOMEM;
    CPU_ZERO_S(bytes, selected.get()); CPU_SET_S(cpu, bytes, selected.get());
    if (sched_setaffinity(0, bytes, selected.get())) return -errno;
    struct Restore {
        std::size_t bytes; cpu_set_t *mask; bool active = true;
        Restore(std::size_t n, cpu_set_t *m) : bytes(n), mask(m) {}
        int restore() {
            if (sched_setaffinity(0, bytes, mask)) return -errno;
            active = false; return 0;
        }
        ~Restore() { if (active) sched_setaffinity(0, bytes, mask); }
    } guard(bytes, previous.get());
    error = operation();
    const int restored = guard.restore();
    return restored ? restored : error;
}
} // namespace platform
} // namespace octool
