// SPDX-License-Identifier: GPL-2.0-only
#include "linux_helper.h"
#include <cstdio>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **)
{
    // No arbitrary paths, commands, diagnostic bypass, SUID bit or GUI code.
    // pkexec authenticates the session and leaves only this connected stream.
    if (argc != 1 || getuid() != 0 || geteuid() != 0) {
        std::fputs("octool helper: administrator authorization required\n", stderr);
        return 77;
    }
    ucred input{}, output{};
    socklen_t size = sizeof(input);
    if (getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &input, &size) || size != sizeof(input)) return 64;
    size = sizeof(output);
    if (getsockopt(STDOUT_FILENO, SOL_SOCKET, SO_PEERCRED, &output, &size) || size != sizeof(output) ||
        input.pid != output.pid || input.uid != output.uid || input.gid != output.gid) return 64;
    const rlimit noCore{0, 0};
    if (setrlimit(RLIMIT_CORE, &noCore)) return 1;
    auto backend = octool::platform::makeHardwareBackend();
    if (!backend) return 1;
    return octool::platform::serveHardwareRequests(*backend, STDIN_FILENO, STDOUT_FILENO);
}
