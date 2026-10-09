// SPDX-License-Identifier: GPL-2.0-only
#include "linux_hwio.h"
#include <cerrno>
#include <sched.h>

namespace octool {
namespace platform {
namespace {

class LinuxHardwareBackend final : public core::HardwareBackend {
public:
    explicit LinuxHardwareBackend(hwio_t *handle) : handle_(handle) {}
    ~LinuxHardwareBackend() override { hwio_close(handle_); }

    core::Reply execute(const core::Request &r) override {
        core::Reply result;
        // Defend the adapter as well as the service: direct callers must not
        // truncate addresses, PCI fields or register values before the HAL.
        if (core::validateRequest(r) != core::ValidationError::None) {
            result.error = -EINVAL; return result;
        }
        if (!handle_) { result.error = -ENOMEM; return result; }
        std::uint64_t value = 0;
        std::uint32_t pci = 0;
        switch (r.space) {
        case core::Space::Msr:
            result.error = r.write ? hwio_wrmsr(handle_, r.cpu, std::uint32_t(r.address), r.value) :
                hwio_rdmsr(handle_, r.cpu, std::uint32_t(r.address), &value);
            break;
        case core::Space::Memory:
            result.error = r.write ? hwio_mem_write(handle_, r.address, r.width, r.value) :
                hwio_mem_read(handle_, r.address, r.width, &value);
            break;
        case core::Space::Pci:
            result.error = r.write ? hwio_pci_write(handle_, r.bus, r.device, r.function,
                                                  r.address, r.width, std::uint32_t(r.value)) :
                hwio_pci_read(handle_, r.bus, r.device, r.function, r.address, r.width, &pci);
            value = pci;
            break;
        }
        if (!result.error) result.value = r.write ? r.value : value;
        return result;
    }

    core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        core::CpuIdReply reply;
        if (!handle_) { reply.error = -ENOMEM; return reply; }
        if (hwio_backend_for(handle_, HWIO_FAM_CPU) == HWIO_BE_MODULE) {
            reply.error = hwio_cpuid(handle_, cpu, leaf, subleaf, reply.words);
        } else {
            // The direct HAL uses the calling CPU. Pin only this worker and
            // restore its affinity before returning it to the caller's pool.
            if (cpu >= CPU_SETSIZE) { reply.error = -ERANGE; return reply; }
            cpu_set_t previous, selected;
            if (sched_getaffinity(0, sizeof(previous), &previous)) {
                reply.error = -errno; return reply;
            }
            CPU_ZERO(&selected); CPU_SET(cpu, &selected);
            if (sched_setaffinity(0, sizeof(selected), &selected)) {
                reply.error = -errno; return reply;
            }
            reply.error = hwio_cpuid(handle_, cpu, leaf, subleaf, reply.words);
            if (sched_setaffinity(0, sizeof(previous), &previous)) reply.error = -errno;
        }
        if (reply.error) for (auto &word : reply.words) word = 0;
        return reply;
    }

    core::Backend backend(core::Space space) const override {
        if (!handle_) return core::Backend::Unavailable;
        const auto family = space == core::Space::Msr ? HWIO_FAM_MSR :
            space == core::Space::Memory ? HWIO_FAM_MMIO : HWIO_FAM_PCI;
        switch (hwio_backend_for(handle_, family)) {
        case HWIO_BE_MODULE: return core::Backend::Module;
        case HWIO_BE_DIRECT: return core::Backend::Direct;
        default: return core::Backend::Unavailable;
        }
    }
private:
    hwio_t *handle_;
};

} // namespace

std::unique_ptr<core::HardwareBackend> makeLinuxHardwareBackend(hwio_t *handle)
{
    return std::unique_ptr<core::HardwareBackend>(new LinuxHardwareBackend(handle));
}

std::unique_ptr<core::HardwareBackend> makeHardwareBackend()
{
    return makeLinuxHardwareBackend(hwio_open(nullptr));
}

} // namespace platform
} // namespace octool
