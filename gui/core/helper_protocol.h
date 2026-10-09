// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware.h"
#include <array>

namespace octool {
namespace ipc {

// Private userspace IPC, independent of the existing 96-byte kernel ABI.
// Fixed 64-byte little-endian frames; no native structs, paths or commands.
using Frame = std::array<std::uint8_t, 64>;
enum class Operation : unsigned { Hello = 0, Execute = 1, Cpuid = 2, Backend = 3 };
struct Command {
    Operation operation = Operation::Hello;
    core::Request request;
    std::uint32_t leaf = 0, subleaf = 0;
};
struct Response {
    int error = 0;
    std::uint64_t value = 0;
    std::array<std::uint32_t, 4> words{};
};
Frame encodeCommand(const Command &command);
bool decodeCommand(const Frame &frame, Command &command);
Frame encodeResponse(Operation operation, const Response &response);
bool decodeResponse(const Frame &frame, Operation expected, Response &response);
Response dispatch(core::HardwareBackend &backend, const Command &command);

} // namespace ipc
} // namespace octool
