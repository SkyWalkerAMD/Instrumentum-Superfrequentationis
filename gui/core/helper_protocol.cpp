// SPDX-License-Identifier: GPL-2.0-only
#include "helper_protocol.h"
#include <cerrno>
#include <climits>

namespace octool {
namespace ipc {
namespace {
void put(Frame &frame, unsigned offset, std::uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) frame[offset + i] = std::uint8_t(value >> (8 * i));
}
std::uint64_t get(const Frame &frame, unsigned offset, unsigned count)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= std::uint64_t(frame[offset + i]) << (8 * i);
    return value;
}
Frame header(Operation operation, bool response)
{
    Frame frame{};
    frame[0] = 'O'; frame[1] = 'C'; frame[2] = 'H'; frame[3] = '1';
    frame[4] = 1; frame[5] = std::uint8_t(operation); frame[6] = response ? 1 : 0;
    return frame;
}
} // namespace

Frame encodeCommand(const Command &command)
{
    auto frame = header(command.operation, false);
    const auto &r = command.request;
    if (command.operation == Operation::Execute) {
        frame[7] = r.write ? 1 : 0;
        put(frame, 12, std::uint32_t(r.width), 4);
        put(frame, 16, r.address, 8); put(frame, 24, r.value, 8);
        put(frame, 32, r.bus, 4); put(frame, 36, r.device, 4); put(frame, 40, r.function, 4);
    }
    if (command.operation == Operation::Execute || command.operation == Operation::Cpuid)
        put(frame, 8, r.cpu, 4);
    if (command.operation == Operation::Execute || command.operation == Operation::Backend)
        put(frame, 52, unsigned(r.space), 4);
    if (command.operation == Operation::Cpuid) {
        put(frame, 44, command.leaf, 4); put(frame, 48, command.subleaf, 4);
    }
    return frame;
}

bool decodeCommand(const Frame &frame, Command &command)
{
    if (frame[5] < 1 || frame[5] > 3 || frame[7] > 1 ||
        get(frame, 12, 4) > INT_MAX || get(frame, 52, 4) > 2) return false;
    Command decoded;
    decoded.operation = Operation(frame[5]);
    auto &r = decoded.request;
    r.write = frame[7] != 0; r.cpu = unsigned(get(frame, 8, 4));
    r.width = int(get(frame, 12, 4));
    r.address = get(frame, 16, 8); r.value = get(frame, 24, 8);
    r.bus = unsigned(get(frame, 32, 4)); r.device = unsigned(get(frame, 36, 4));
    r.function = unsigned(get(frame, 40, 4)); r.space = core::Space(get(frame, 52, 4));
    decoded.leaf = std::uint32_t(get(frame, 44, 4)); decoded.subleaf = std::uint32_t(get(frame, 48, 4));
    // Re-encoding also rejects a wrong magic/version/direction, nonzero
    // reserved bytes and fields that do not belong to this operation.
    if (encodeCommand(decoded) != frame) return false;
    command = decoded;
    return true;
}

Frame encodeResponse(Operation operation, const Response &response)
{
    auto frame = header(operation, true);
    const int error = response.error > 0 || response.error < -4095 ? -EIO : response.error;
    put(frame, 56, std::uint32_t(error), 4);
    if (!error) {
        if (operation == Operation::Execute || operation == Operation::Backend)
            put(frame, 24, response.value, 8);
        if (operation == Operation::Cpuid)
            for (unsigned i = 0; i < 4; ++i) put(frame, 32 + 4 * i, response.words[i], 4);
    }
    return frame;
}

bool decodeResponse(const Frame &frame, Operation expected, Response &response)
{
    if (unsigned(expected) > 3) return false;
    const auto error = std::uint32_t(get(frame, 56, 4));
    if (error && error < 0xfffff001u) return false;
    Response decoded;
    decoded.error = error ? -int(UINT32_MAX - error + 1) : 0;
    decoded.value = get(frame, 24, 8);
    for (unsigned i = 0; i < 4; ++i) decoded.words[i] = std::uint32_t(get(frame, 32 + 4 * i, 4));
    if (expected == Operation::Backend && decoded.value > unsigned(core::Backend::Direct)) return false;
    if (encodeResponse(expected, decoded) != frame) return false;
    response = decoded;
    return true;
}

Response dispatch(core::HardwareBackend &backend, const Command &command)
{
    Response response;
    switch (command.operation) {
    case Operation::Execute: {
        if (core::validateRequest(command.request) != core::ValidationError::None) {
            response.error = -EINVAL; break;
        }
        const auto reply = backend.execute(command.request);
        response.error = reply.error;
        if (!reply.error) response.value = command.request.write ? command.request.value : reply.value;
        break;
    }
    case Operation::Cpuid: {
        const auto reply = backend.cpuid(command.request.cpu, command.leaf, command.subleaf);
        response.error = reply.error;
        if (!reply.error) for (unsigned i = 0; i < 4; ++i) response.words[i] = reply.words[i];
        break;
    }
    case Operation::Backend:
        if (command.request.space != core::Space::Msr && command.request.space != core::Space::Memory &&
            command.request.space != core::Space::Pci) response.error = -EINVAL;
        else response.value = unsigned(backend.backend(command.request.space));
        break;
    default: response.error = -EINVAL;
    }
    return response;
}

} // namespace ipc
} // namespace octool
