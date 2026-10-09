// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>

namespace octool {
namespace core {

// Application requests, not the kernel wire format. Each OS backend owns its
// transport encoding; no Qt or OS types belong in this interface.
enum class Space { Msr, Memory, Pci };
struct Request {
    Space space = Space::Msr;
    bool write = false;
    unsigned cpu = 0;
    std::uint64_t address = 0, value = 0;
    unsigned bus = 0, device = 0, function = 0;
    int width = 8;
};
struct Reply { int error = 0; std::uint64_t value = 0; };
struct CpuIdReply { int error = 0; std::uint32_t words[4]{}; };

enum class ValidationError {
    None, InvalidSpace, InvalidWidth, InvalidMsr, InvalidPci,
    UnalignedAddress, AddressOverflow, ValueOverflow
};
ValidationError validateRequest(const Request &request);

enum class Backend { Unavailable, Module, Direct };

class HardwareBackend {
public:
    virtual ~HardwareBackend() = default;
    // Called only after validation. Return 0 or negative errno in error.
    // Native errors on another OS must be mapped by that OS's adapter.
    virtual Reply execute(const Request &request) = 0;
    // Select the requested OS logical CPU; restore any changed thread state.
    virtual CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) = 0;
    virtual Backend backend(Space space) const = 0;
};

// Own one backend and serialize ALL of its calls, including diagnostics and
// CPUID. This is per-operation serialization, not a multi-register transaction.
// Construction never executes a register request. Destroy only after callers
// have released the service (the GUI workers retain their shared owner).
class HardwareService {
public:
    explicit HardwareService(std::unique_ptr<HardwareBackend> backend);
    Reply execute(const Request &request);
    CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf = 0);
    Backend backend(Space space) const;
private:
    std::unique_ptr<HardwareBackend> backend_;
    mutable std::mutex mutex_;
};

} // namespace core
} // namespace octool
