// SPDX-License-Identifier: GPL-2.0-only
#ifdef NDEBUG
#error "Core regression assertions must stay enabled"
#endif
#include "../core/helper_protocol.h"
#include <cassert>
#include <cerrno>
#include <iostream>

using namespace octool;

struct Backend final : core::HardwareBackend {
    unsigned reads = 0, writes = 0, cpus = 0;
    int error = 0;
    core::Request last;
    core::Reply execute(const core::Request &request) override {
        last = request; request.write ? ++writes : ++reads;
        core::Reply reply; reply.error = error; reply.value = 0x8877665544332211ULL;
        return reply;
    }
    core::CpuIdReply cpuid(unsigned cpu, std::uint32_t leaf, std::uint32_t subleaf) override {
        assert(cpu == 513 && leaf == 0x8000001e && subleaf == 3); ++cpus;
        core::CpuIdReply reply; reply.error = error;
        for (unsigned i = 0; i < 4; ++i) reply.words[i] = 0x87654321 + i;
        return reply;
    }
    core::Backend backend(core::Space space) const override {
        return space == core::Space::Memory ? core::Backend::Unavailable : core::Backend::Module;
    }
};

static void wireAndWidth()
{
    ipc::Command command; command.operation = ipc::Operation::Execute;
    command.request.cpu = 513; command.request.address = 0xc0010064;
    command.request.value = 0xfedcba9876543210ULL; command.request.write = true;
    const auto frame = ipc::encodeCommand(command);
    assert(frame[0] == 'O' && frame[3] == '1' && frame[4] == 1 && frame[5] == 1 && frame[6] == 0 && frame[7] == 1);
    assert(frame[8] == 1 && frame[9] == 2 && frame[12] == 8);
    assert(frame[16] == 0x64 && frame[17] == 0 && frame[18] == 1 && frame[19] == 0xc0);
    const unsigned expected[] = {0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe};
    for (unsigned i = 0; i < 8; ++i) assert(frame[24+i] == expected[i]);
    ipc::Command decoded;
    assert(ipc::decodeCommand(frame, decoded));
    assert(decoded.request.cpu == 513 && decoded.request.address == 0xc0010064);
    assert(decoded.request.value == 0xfedcba9876543210ULL && decoded.request.write);
}

static void malformedFrames()
{
    ipc::Command command; command.operation = ipc::Operation::Cpuid;
    command.request.cpu = 513; command.leaf = 0x8000001e; command.subleaf = 3;
    const auto frame = ipc::encodeCommand(command);
    ipc::Command decoded;
    assert(ipc::decodeCommand(frame, decoded));
    for (unsigned offset : {0,3,4,6,7,12,16,24,32,36,40,52,56,60,63}) {
        auto invalid = frame; invalid[offset] ^= 0x80;
        assert(!ipc::decodeCommand(invalid, decoded));
    }
    auto invalid = frame; invalid[5] = 0; assert(!ipc::decodeCommand(invalid, decoded));
    invalid[5] = 4; assert(!ipc::decodeCommand(invalid, decoded));
    ipc::Response response; response.error = -EACCES; response.value = UINT64_MAX;
    response.words.fill(UINT32_MAX);
    const auto failure = ipc::encodeResponse(ipc::Operation::Execute, response);
    assert(failure[56] == 0xf3 && failure[57] == 0xff && failure[59] == 0xff);
    assert(ipc::decodeResponse(failure, ipc::Operation::Execute, response));
    assert(response.error == -EACCES && response.value == 0);
    for (auto word : response.words) assert(word == 0);
    invalid = failure; invalid[24] = 1; assert(!ipc::decodeResponse(invalid, ipc::Operation::Execute, response));
    invalid = failure; invalid[56] = 1; invalid[57] = invalid[58] = invalid[59] = 0;
    assert(!ipc::decodeResponse(invalid, ipc::Operation::Execute, response));
    assert(!ipc::decodeResponse(failure, ipc::Operation::Cpuid, response));
}

static void invalidRequestsNeverDispatch()
{
    Backend backend;
    ipc::Command command; command.operation = ipc::Operation::Execute;
    command.request.address = 0x100000000ULL;
    assert(ipc::dispatch(backend, command).error == -EINVAL);
    command.request.space = core::Space::Memory; command.request.address = 1;
    assert(ipc::dispatch(backend, command).error == -EINVAL);
    command.request.address = 0; command.request.width = 1; command.request.write = true; command.request.value = 256;
    assert(ipc::dispatch(backend, command).error == -EINVAL);
    command.operation = ipc::Operation::Hello;
    assert(ipc::dispatch(backend, command).error == -EINVAL);
    command.operation = ipc::Operation::Backend; command.request.space = static_cast<core::Space>(99);
    assert(ipc::dispatch(backend, command).error == -EINVAL);
    assert(backend.reads == 0 && backend.writes == 0 && backend.cpus == 0);
}

static void dispatchAndErrors()
{
    Backend backend;
    ipc::Command command; command.operation = ipc::Operation::Execute;
    command.request.cpu = 513; command.request.address = 0x123;
    assert(ipc::dispatch(backend, command).value == 0x8877665544332211ULL);
    assert(backend.last.cpu == 513 && backend.reads == 1);
    command.request.write = true; command.request.value = UINT64_MAX;
    assert(ipc::dispatch(backend, command).value == UINT64_MAX);
    assert(backend.reads == 1 && backend.writes == 1);
    backend.error = -EIO;
    const auto failure = ipc::dispatch(backend, command);
    assert(failure.error == -EIO && failure.value == 0);
    command.operation = ipc::Operation::Cpuid; command.leaf = 0x8000001e; command.subleaf = 3;
    auto id = ipc::dispatch(backend, command);
    assert(id.error == -EIO && id.words[0] == 0 && id.words[3] == 0);
    backend.error = 0; id = ipc::dispatch(backend, command);
    assert(id.error == 0 && id.words[0] == 0x87654321 && id.words[3] == 0x87654324);
    command.operation = ipc::Operation::Backend;
    assert(ipc::dispatch(backend, command).value == unsigned(core::Backend::Module));
    command.request.space = core::Space::Memory;
    assert(ipc::dispatch(backend, command).value == unsigned(core::Backend::Unavailable));
}

int main()
{
    wireAndWidth(); malformedFrames(); invalidRequestsNeverDispatch(); dispatchAndErrors();
    std::cout << "helper protocol: 4 scenario groups passed\n";
}
