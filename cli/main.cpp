// SPDX-License-Identifier: GPL-2.0-only
#include "commands.h"
#include "platform/hardware_factory.h"
#include <iostream>

int main(int argc, char **argv) {
    return octool::cli::run(std::vector<std::string>(argv + 1, argv + argc), std::cout,
                           octool::platform::makeHardwareBackend);
}
