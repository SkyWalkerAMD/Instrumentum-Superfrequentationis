// SPDX-License-Identifier: GPL-2.0-only
// No Qt, OS adapter, device access or privileged instructions.
#ifdef NDEBUG
#error "Core regression assertions must stay enabled"
#endif
#include "../core/hardware.h"
#include <cassert>
#include <cerrno>
#include <atomic>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

using namespace octool::core;

struct Reference : HardwareBackend {
    int calls = 0, cpuCalls = 0, error = 0;
    mutable int backendCalls = 0;
    Request last;
    unsigned lastCpu = 0;
    std::uint32_t lastLeaf = 0, lastSubleaf = 0;
    Reply execute(const Request &request) override {
        ++calls; last = request;
        Reply reply; reply.error = error; reply.value = 0xfedcba9876543210ULL;
        return reply;
    }
    CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        ++cpuCalls; lastCpu = cpu; lastLeaf = leaf; lastSubleaf = subleaf;
        CpuIdReply reply; reply.error = error;
        // Poison on error too: the service must suppress every output word.
        for (auto &word : reply.words) word = UINT32_MAX;
        return reply;
    }
    Backend backend(Space) const override {
        ++backendCalls;
        return Backend::Module;
    }
};

static void invalidRequestsDoNotReachBackend()
{
    std::unique_ptr<Reference> owner(new Reference);
    auto &reference = *owner;
    HardwareService service(std::move(owner));
    auto reject = [&](const Request &r, ValidationError expected) {
        assert(validateRequest(r) == expected);
        const auto result = service.execute(r);
        assert(result.error == -EINVAL && result.value == 0);
        assert(reference.calls == 0);
    };
    Request r;
    r.space = static_cast<Space>(99); reject(r, ValidationError::InvalidSpace);
    assert(service.backend(r.space) == Backend::Unavailable && reference.backendCalls == 0);
    for (auto space : {Space::Msr, Space::Memory, Space::Pci}) {
        for (int width : {-1, 0, 3, 9}) {
            r = Request{}; r.space = space; r.width = width;
            reject(r, ValidationError::InvalidWidth);
        }
    }
    r = Request{}; r.address = 0x100000000ULL; reject(r, ValidationError::InvalidMsr);
    r.address = 0; r.width = 4; reject(r, ValidationError::InvalidMsr);
    for (unsigned field = 0; field < 6; ++field) {
        r = Request{}; r.space = Space::Pci; r.width = 4;
        if (field == 0) r.bus = 256;
        if (field == 1) r.device = 32;
        if (field == 2) r.function = 8;
        if (field == 3) r.address = 256;
        if (field == 4) r.address = 254;
        if (field == 5) r.width = 8;
        reject(r, ValidationError::InvalidPci);
    }
    r = Request{}; r.space = Space::Pci; r.width = 4; r.address = 2;
    reject(r, ValidationError::UnalignedAddress);
    r = Request{}; r.space = Space::Memory; r.address = UINT64_MAX;
    reject(r, ValidationError::UnalignedAddress);
    for (int width : {1, 2, 4}) {
        r = Request{}; r.space = Space::Memory; r.width = width; r.write = true;
        r.value = std::uint64_t(1) << (width * 8);
        reject(r, ValidationError::ValueOverflow);
    }
}

static void validRequestsRetainAllFields()
{
    std::unique_ptr<Reference> owner(new Reference);
    auto &reference = *owner;
    HardwareService service(std::move(owner));
    assert(reference.calls == 0 && reference.cpuCalls == 0 && reference.backendCalls == 0);
    Request r; r.address = UINT32_MAX; r.cpu = 513;
    assert(service.execute(r).value == 0xfedcba9876543210ULL);
    assert(reference.last.cpu == 513 && reference.last.address == UINT32_MAX && !reference.last.write);
    for (int width : {1, 2, 4, 8}) {
        r = Request{}; r.space = Space::Memory; r.width = width;
        r.address = UINT64_MAX - std::uint64_t(width - 1);
        assert(service.execute(r).error == 0);
        assert(reference.last.address == r.address && reference.last.width == width);
        r.write = true; r.value = width == 8 ? UINT64_MAX : (std::uint64_t(1) << (8 * width)) - 1;
        assert(service.execute(r).value == r.value);
        assert(reference.last.write && reference.last.value == r.value);
    }
    for (int width : {1, 2, 4}) {
        r = Request{}; r.space = Space::Pci; r.width = width;
        r.bus = 255; r.device = 31; r.function = 7; r.address = unsigned(256 - width);
        assert(service.execute(r).error == 0);
        assert(reference.last.bus == 255 && reference.last.device == 31 && reference.last.function == 7);
        assert(reference.last.address == r.address && reference.last.width == width);
    }
    const auto id = service.cpuid(513, 0x8000001e, 7);
    assert(id.error == 0 && id.words[3] == UINT32_MAX);
    assert(reference.lastCpu == 513 && reference.lastLeaf == 0x8000001e && reference.lastSubleaf == 7);
    for (auto space : {Space::Msr, Space::Memory, Space::Pci}) assert(service.backend(space) == Backend::Module);
}

static void failedOutputsAreDiscarded()
{
    std::unique_ptr<Reference> owner(new Reference);
    auto &reference = *owner;
    HardwareService service(std::move(owner));
    Request r;
    assert(service.execute(r).value == 0xfedcba9876543210ULL);
    for (int error : {-EIO, -EACCES, -ENODEV}) {
        reference.error = error;
        for (bool write : {false, true}) {
            r.write = write; r.value = UINT64_MAX;
            const auto result = service.execute(r);
            assert(result.error == error && result.value == 0);
        }
        const auto result = service.cpuid(7, 1);
        assert(result.error == error);
        for (auto word : result.words) assert(word == 0);
    }
    reference.error = 0; r.write = true; r.value = 0x8877665544332211ULL;
    // The adapter's unrelated value is not a write readback.
    assert(service.execute(r).value == r.value);
}

static void missingBackendIsUnavailable()
{
    HardwareService service{std::unique_ptr<HardwareBackend>()};
    assert(service.execute(Request{}).error == -ENODEV);
    const auto id = service.cpuid(0, 0);
    assert(id.error == -ENODEV);
    for (auto word : id.words) assert(word == 0);
    assert(service.backend(Space::Msr) == Backend::Unavailable);
}

static void ownershipIsReleasedOnce()
{
    struct Owned final : Reference {
        explicit Owned(int &closed) : closed_(closed) {}
        ~Owned() override { ++closed_; }
        int &closed_;
    };
    int closed = 0;
    {
        HardwareService service(std::unique_ptr<HardwareBackend>(new Owned(closed)));
        assert(closed == 0);
        service.execute(Request{});
    }
    assert(closed == 1);
}

static void concurrentCallsShareOneLock()
{
    struct Concurrent final : HardwareBackend {
        mutable std::atomic<int> active{0}, calls{0};
        mutable std::atomic<bool> overlap{false};
        void visit() const {
            if (active.fetch_add(1) != 0) overlap.store(true);
            for (int i = 0; i < 16; ++i) std::this_thread::yield();
            ++calls; --active;
        }
        Reply execute(const Request &) override { visit(); return {}; }
        CpuIdReply cpuid(unsigned, std::uint32_t, std::uint32_t) override { visit(); return {}; }
        Backend backend(Space) const override { visit(); return Backend::Module; }
    };
    std::unique_ptr<Concurrent> owner(new Concurrent);
    auto &reference = *owner;
    HardwareService service(std::move(owner));
    std::atomic<bool> start{false};
    std::atomic<int> ready{0};
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < 6; ++worker) {
        workers.emplace_back([&, worker] {
            ++ready;
            while (!start.load()) std::this_thread::yield();
            for (unsigned i = 0; i < 90; ++i) {
                switch ((worker + i) % 3) {
                case 0: assert(service.execute(Request{}).error == 0); break;
                case 1: assert(service.cpuid(worker, 0).error == 0); break;
                default: assert(service.backend(Space::Msr) == Backend::Module); break;
                }
            }
        });
    }
    while (ready.load() != 6) std::this_thread::yield();
    start.store(true);
    for (auto &worker : workers) worker.join();
    assert(reference.calls.load() == 540 && reference.active.load() == 0 && !reference.overlap.load());
}

int main()
{
    invalidRequestsDoNotReachBackend();
    validRequestsRetainAllFields();
    failedOutputsAreDiscarded();
    missingBackendIsUnavailable();
    ownershipIsReleasedOnce();
    concurrentCallsShareOneLock();
    std::cout << "hardware core: 6 scenario groups passed\n";
}
