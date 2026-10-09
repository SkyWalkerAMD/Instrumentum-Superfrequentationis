// SPDX-License-Identifier: GPL-2.0-only
#ifdef NDEBUG
#error "helper transport regressions require assertions"
#endif
#include "../platform/linux_helper.h"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace octool;

struct Fake final : core::HardwareBackend {
    unsigned calls = 0;
    core::Reply execute(const core::Request &request) override {
        ++calls; assert(!request.write && request.cpu == 513 && request.address == 0x123);
        core::Reply result; result.value = 0xfedcba9876543210ULL; return result;
    }
    core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        ++calls; assert(cpu == 513 && leaf == 7 && subleaf == 3);
        core::CpuIdReply result; result.error = -EACCES;
        for (auto &word : result.words) word = UINT32_MAX;
        return result;
    }
    core::Backend backend(core::Space) const override { return core::Backend::Module; }
};

int main()
{
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    Fake backend; int serverResult = -1;
    std::thread server([&] {
        serverResult = platform::serveHardwareRequests(backend, pair[1], pair[1]); close(pair[1]);
    });
    ipc::Frame frame; ipc::Response response;
    assert(platform::receiveHelperFrame(pair[0], frame, 1000) == 0);
    assert(ipc::decodeResponse(frame, ipc::Operation::Hello, response));
    {
        core::HardwareService service(platform::makeHelperBackend(pair[0]));
        assert(service.backend(core::Space::Msr) == core::Backend::Module);
        core::Request request; request.cpu = 513; request.address = 0x123;
        assert(service.execute(request).value == 0xfedcba9876543210ULL);
        const auto id = service.cpuid(513, 7, 3);
        assert(id.error == -EACCES);
        for (auto word : id.words) assert(word == 0);
        request.write = true; request.value = 256; request.space = core::Space::Memory; request.width = 1;
        assert(service.execute(request).error == -EINVAL);
    }
    server.join(); assert(serverResult == 0 && backend.calls == 2);

    // Fragmented delivery cannot expose incomplete words.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    ipc::Response expected; expected.value = 0x8877665544332211ULL;
    auto complete = ipc::encodeResponse(ipc::Operation::Execute, expected);
    std::thread writer([&] {
        for (unsigned i = 0; i < complete.size(); ++i) assert(send(pair[1], &complete[i], 1, MSG_NOSIGNAL) == 1);
        close(pair[1]);
    });
    assert(platform::receiveHelperFrame(pair[0], frame, 1000) == 0 && frame == complete);
    writer.join(); close(pair[0]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(send(pair[1], complete.data(), 7, MSG_NOSIGNAL) == 7); close(pair[1]);
    assert(platform::receiveHelperFrame(pair[0], frame, 1000) == -EPROTO); close(pair[0]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(platform::receiveHelperFrame(pair[0], frame, 20) == -ETIMEDOUT);
    std::atomic<bool> cancelled{true};
    assert(platform::receiveHelperFrame(pair[0], frame, 120000, &cancelled) == -ECANCELED);
    close(pair[0]); close(pair[1]);

    // Malformed input exits the server without dispatching a request.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    server = std::thread([&] {
        serverResult = platform::serveHardwareRequests(backend, pair[1], pair[1]); close(pair[1]);
    });
    assert(platform::receiveHelperFrame(pair[0], frame, 1000) == 0);
    frame.fill(0);
    assert(platform::sendHelperFrame(pair[0], frame, 1000) == 0);
    server.join(); close(pair[0]);
    assert(serverResult == 2 && backend.calls == 2);
    std::cout << "helper transport: 5 scenario groups passed\n";
}
