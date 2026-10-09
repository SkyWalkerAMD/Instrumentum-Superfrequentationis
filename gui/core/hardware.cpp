// SPDX-License-Identifier: GPL-2.0-only
#include "hardware.h"
#include <cerrno>
#include <limits>
#include <utility>
#include <chrono>

namespace octool {
namespace core {

static bool validSpace(Space space)
{
    return space == Space::Msr || space == Space::Memory || space == Space::Pci;
}

ValidationError validateRequest(const Request &r)
{
    if (!validSpace(r.space)) return ValidationError::InvalidSpace;
    if (r.width != 1 && r.width != 2 && r.width != 4 && r.width != 8)
        return ValidationError::InvalidWidth;
    if (r.space == Space::Msr && (r.width != 8 || r.address > UINT32_MAX))
        return ValidationError::InvalidMsr;
    if (r.space == Space::Pci && (r.width > 4 || r.bus > 255 || r.device > 31 ||
        r.function > 7 || r.address > 255 || r.address + r.width > 256))
        return ValidationError::InvalidPci;
    if (r.space != Space::Msr && r.address % std::uint64_t(r.width))
        return ValidationError::UnalignedAddress;
    if (r.address > std::numeric_limits<std::uint64_t>::max() - std::uint64_t(r.width - 1))
        return ValidationError::AddressOverflow;
    if (r.write && r.width < 8 && r.value >= (std::uint64_t(1) << (r.width * 8)))
        return ValidationError::ValueOverflow;
    return ValidationError::None;
}

HardwareService::HardwareService(std::unique_ptr<HardwareBackend> backend)
    : backend_(std::move(backend)) {}

Reply HardwareService::execute(const Request &request)
{
    Reply result;
    if (validateRequest(request) != ValidationError::None) {
        result.error = -EINVAL;
        return result;
    }
    std::lock_guard<std::timed_mutex> guard(mutex_);
    if (!backend_) { result.error = -ENODEV; return result; }
    result = backend_->execute(request);
    // Error outputs cannot be mistaken for a reading; successful writes report
    // the submitted value, never a fabricated readback from the adapter.
    if (result.error) result.value = 0;
    else if (request.write) result.value = request.value;
    return result;
}

CpuIdReply HardwareService::cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf)
{
    std::lock_guard<std::timed_mutex> guard(mutex_);
    CpuIdReply result;
    if (!backend_) { result.error = -ENODEV; return result; }
    result = backend_->cpuid(cpu, leaf, subleaf);
    if (result.error) for (auto &word : result.words) word = 0;
    return result;
}

Backend HardwareService::backend(Space space) const
{
    if (!validSpace(space)) return Backend::Unavailable;
    std::lock_guard<std::timed_mutex> guard(mutex_);
    return backend_ ? backend_->backend(space) : Backend::Unavailable;
}

bool HardwareService::replaceBackend(std::unique_ptr<HardwareBackend> backend,
                                     const std::atomic<bool> *cancelled)
{
    std::lock_guard<std::timed_mutex> guard(mutex_);
    if (cancelled && cancelled->load()) return false;
    backend_ = std::move(backend);
    return true;
}

int HardwareService::transaction(const std::function<int(HardwareSession &)> &operation,
                                int timeoutMs, const std::atomic<bool> *cancelled)
{
    if (!operation || timeoutMs <= 0 || timeoutMs > 120000) return -EINVAL;
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    struct Session final : HardwareSession {
        HardwareBackend *target;
        Clock::time_point deadline;
        const std::atomic<bool> *cancelled;
        Session(HardwareBackend *t, Clock::time_point d, const std::atomic<bool> *c)
            : target(t), deadline(d), cancelled(c) {}
        int checkpoint() const override {
            if (cancelled && cancelled->load()) return -ECANCELED;
            if (Clock::now() >= deadline) return -ETIMEDOUT;
            return target ? 0 : -ENODEV;
        }
        Reply execute(const Request &request) override {
            Reply reply;
            reply.error = checkpoint();
            if (!reply.error && validateRequest(request) != ValidationError::None) reply.error = -EINVAL;
            if (reply.error) return reply;
            reply = target->execute(request);
            if (reply.error) reply.value = 0;
            else if (request.write) reply.value = request.value;
            return reply;
        }
        CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
            CpuIdReply reply; reply.error = checkpoint();
            if (reply.error) return reply;
            reply = target->cpuid(cpu, leaf, subleaf);
            if (reply.error) for (auto &word : reply.words) word = 0;
            return reply;
        }
    };
    std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
    while (true) {
        if (cancelled && cancelled->load()) return -ECANCELED;
        if (Clock::now() >= deadline) return -ETIMEDOUT;
        const auto next = Clock::now() + std::chrono::milliseconds(20);
        if (lock.try_lock_until(next < deadline ? next : deadline)) break;
    }
    Session session(backend_.get(), deadline, cancelled);
    const int error = session.checkpoint();
    return error ? error : operation(session);
}

} // namespace core
} // namespace octool
