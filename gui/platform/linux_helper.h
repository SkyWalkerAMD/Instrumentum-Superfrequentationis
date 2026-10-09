// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hardware_factory.h"
#include "../core/helper_protocol.h"

namespace octool {
namespace platform {

int sendHelperFrame(int fd, const ipc::Frame &frame, int timeoutMs);
int receiveHelperFrame(int fd, ipc::Frame &frame, int timeoutMs,
                       const std::atomic<bool> *cancelled = nullptr);
// The server only dispatches the bounded protocol. Privilege/descriptor checks
// belong to helper_main; tests inject a synthetic backend and socket pair.
int serveHardwareRequests(core::HardwareBackend &backend, int input, int output);
// Takes ownership of an already connected descriptor after the Hello reply.
// childPid is only for waitpid/cleanup, never used as an authorization identity.
std::unique_ptr<core::HardwareBackend> makeHelperBackend(int fd, int childPid = -1);

} // namespace platform
} // namespace octool
