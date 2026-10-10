// SPDX-License-Identifier: GPL-2.0-only
#include "linux_inventory_native.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <locale>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace octool { namespace platform {
int readBoundedFile(const std::string &path, std::vector<std::uint8_t> &bytes, std::size_t maximum) {
    bytes.clear();
    if (!maximum || maximum > 65536 || path.find('\0') != std::string::npos) return -EINVAL;
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -errno;
    struct Close { int fd; ~Close() { close(fd); } } closeFile{fd};
    struct stat st{};
    if (fstat(fd, &st)) return -errno;
    if (!S_ISREG(st.st_mode)) return -EINVAL;
    std::vector<std::uint8_t> data(maximum + 1);
    std::size_t size = 0;
    while (size < data.size()) {
        const auto n = read(fd, data.data() + size, data.size() - size);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        if (!n) break;
        size += std::size_t(n);
    }
    if (size > maximum) return -EFBIG;
    data.resize(size); bytes.swap(data); return 0;
}
namespace {
std::string text(const std::string &path, int &error) {
    std::vector<std::uint8_t> data;
    error = readBoundedFile(path, data);
    if (error) return {};
    const std::string value(data.begin(), data.end());
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::vector<std::string> entries(const std::string &path, int &error) {
    error = 0;
    DIR *dir = opendir(path.c_str());
    if (!dir) { error = -errno; return {}; }
    struct Close { DIR *dir; ~Close() { closedir(dir); } } closeDir{dir};
    std::vector<std::string> names;
    for (;;) {
        errno = 0;
        const auto *entry = readdir(dir);
        if (!entry) { if (errno) error = -errno; break; }
        if (entry->d_name[0] == '.') continue;
        if (names.size() >= 8192) { error = -E2BIG; break; }
        names.emplace_back(entry->d_name);
    }
    std::sort(names.begin(), names.end());
    return names;
}
void rowError(NativeInventorySnapshot &out, const std::string &source, int error) {
    NativeInventoryRow row;
    row.group = "Inventory"; row.name = "Read error"; row.source = source;
    row.error = error; row.status = std::strerror(-error); out.rows.push_back(row);
}
}
NativeInventorySnapshot linuxInventoryNative(const std::string &root) {
    NativeInventorySnapshot out;
    for (const char *key : {"board_vendor", "board_name", "board_version", "bios_vendor", "bios_version", "bios_date", "product_name"}) {
        NativeInventoryRow row;
        row.group = "Motherboard / BIOS"; row.name = key; row.source = root + "/class/dmi/id/" + key;
        row.value = text(row.source, row.error); row.status = row.error ? std::strerror(-row.error) : "Read";
        out.rows.push_back(row);
    }
    const std::regex sensor("^(temp|in|curr|power|fan|freq)([0-9]+)_(input|average)$"), hwmon("hwmon[0-9]+");
    int directoryError = 0;
    const auto devices = entries(root + "/class/hwmon", directoryError);
    if (directoryError && directoryError != -ENOENT) rowError(out, root + "/class/hwmon", directoryError);
    unsigned sensors = 0;
    for (const auto &device : devices) {
        if (!std::regex_match(device, hwmon)) continue;
        const std::string dir = root + "/class/hwmon/" + device + "/";
        int ignored = 0;
        std::string name = text(dir + "name", ignored);
        if (name.empty()) name = device;
        const auto files = entries(dir, directoryError);
        if (directoryError) rowError(out, dir, directoryError);
        for (const auto &file : files) {
            std::smatch match;
            if (!std::regex_match(file, match, sensor)) continue;
            if (out.rows.size() >= 4096) { rowError(out, dir, -E2BIG); return out; }
            ++sensors;
            const std::string kind = match[1], base = kind + match[2].str();
            NativeInventoryRow row;
            row.group = name; row.name = text(dir + base + "_label", ignored);
            if (row.name.empty()) row.name = base;
            if (match[3] == "average") row.name += " (average)";
            row.source = dir + file;
            const auto raw = text(row.source, row.error);
            long long value = 0;
            if (!row.error) {
                std::istringstream stream(raw); stream.imbue(std::locale::classic());
                if (!(stream >> value) || !stream.eof()) row.error = -EINVAL;
            }
            row.status = row.error == -EINVAL ? "Invalid numeric attribute" : row.error ? std::strerror(-row.error) : "Read";
            for (const char *suffix : {"_fault", "_alarm"}) {
                int flagError = 0;
                const auto flag = text(dir + base + suffix, flagError);
                if (flagError == -ENOENT) continue;
                if (flagError) row.status += "; " + std::string(suffix + 1) + " unavailable";
                else if (flag == "1") row.status += "; " + std::string(suffix + 1);
            }
            const double divisor = kind == "power" ? 1000000.0 : kind == "fan" || kind == "freq" ? 1.0 : 1000.0;
            row.unit = kind == "temp" ? "C" : kind == "in" ? "V" : kind == "curr" ? "A" : kind == "power" ? "W" : kind == "fan" ? "RPM" : "Hz";
            if (!row.error) {
                std::ostringstream stream; stream.imbue(std::locale::classic());
                stream << std::setprecision(12) << double(value) / divisor; row.value = stream.str();
            }
            out.rows.push_back(row);
        }
    }
    if (!sensors) {
        NativeInventoryRow row; row.group = "Sensors"; row.name = "hwmon";
        row.source = root + "/class/hwmon"; row.status = "No kernel sensor driver exposed"; out.rows.push_back(row);
    }
    std::set<std::string> seen;
    const std::regex address("^[0-9]+-00[0-9a-fA-F]{2}$");
    for (const char *driver : {"ee1004", "spd5118"}) {
        const std::string dir = root + "/bus/i2c/drivers/" + driver;
        const auto names = entries(dir, directoryError);
        if (directoryError && directoryError != -ENOENT) rowError(out, dir, directoryError);
        for (const auto &name : names) {
            if (!std::regex_match(name, address)) continue;
            NativeSpdDevice spd; spd.path = dir + "/" + name + "/eeprom";
            std::unique_ptr<char, decltype(&std::free)> canonical(realpath(spd.path.c_str(), nullptr), &std::free);
            if (canonical && !seen.insert(canonical.get()).second) continue;
            if (out.spd.size() >= 128) { rowError(out, dir, -E2BIG); return out; }
            spd.error = readBoundedFile(spd.path, spd.bytes); out.spd.push_back(spd);
        }
    }
    return out;
}
} }
