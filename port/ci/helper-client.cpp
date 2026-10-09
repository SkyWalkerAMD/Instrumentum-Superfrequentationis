// SPDX-License-Identifier: GPL-2.0-only
// Use the production authorization transport, including its child lifecycle.
// This probe never submits an MSR, MMIO or PCI operation.
#include "../../gui/platform/linux_helper.h"
#include "../../gui/platform/linux_cpu.h"
#include <cerrno>
#include <cstring>
#include <iostream>

int main(int argc, char **argv)
{
    std::atomic<bool> cancelled{false};
    auto connection = octool::platform::authorizeHardware(cancelled);
    if (argc == 2 && std::strcmp(argv[1], "--expect-denied") == 0) {
        std::cout << "{\"authorization_error\":" << connection.error << "}\n";
        return connection.error == -EACCES && !connection.backend ? 0 : 1;
    }
    if (connection.error || !connection.backend) {
        std::cerr << "authorization failed: " << connection.error << '\n'; return 1;
    }
    const auto info = octool::platform::systemInfo();
    if (info.affinityError || info.allowedCpus.empty()) return 2;
    const auto reply = connection.backend->cpuid(info.allowedCpus.front(), 0, 0);
    if (reply.error || !reply.words[0]) return 3;
    char vendor[13]{};
    std::memcpy(vendor, &reply.words[1], 4);
    std::memcpy(vendor + 4, &reply.words[3], 4);
    std::memcpy(vendor + 8, &reply.words[2], 4);
    if (std::strcmp(vendor, "GenuineIntel") && std::strcmp(vendor, "AuthenticAMD")) return 4;
    std::cout << "{\"cpu\":" << info.allowedCpus.front() << ",\"vendor\":\"" << vendor
              << "\",\"register_access_tested\":false}\n";
    return 0;
}
