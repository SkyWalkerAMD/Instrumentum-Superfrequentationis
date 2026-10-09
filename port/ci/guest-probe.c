// SPDX-License-Identifier: GPL-2.0-only
// Runs only inside a disposable VM. No MSR/MMIO/PCI/IO/EC register operations.
#define _GNU_SOURCE
#include "../abi/octool_hwio_abi.h"
#include "../abi/octool_hwio_caps.h"
#include "../hal/octool_hwio.h"
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "guest probe line %d: %s (errno=%d)\n", __LINE__, #x, errno); return 1; } } while (0)

int main(void)
{
    struct utsname uts;
    struct stat metadata;
    struct octool_hwio_caps caps = {0};
    uint64_t token;
    long page = sysconf(_SC_PAGESIZE);
    REQUIRE(uname(&uts) == 0 && getuid() == 0 && page > 0);
    REQUIRE(stat("/dev/mydev", &metadata) == 0 && S_ISCHR(metadata.st_mode));
    REQUIRE((metadata.st_mode & 0777) == 0600 && metadata.st_uid == 0);
    int fd = open("/dev/mydev", O_RDWR | O_CLOEXEC);
    REQUIRE(fd >= 0);
    REQUIRE(read(fd, &token, sizeof(token)) == (ssize_t)sizeof(token) && token != 0);
    REQUIRE(ioctl(fd, OCTOOL_HWIO_GET_CAPS_V1, &caps) == 0);
    REQUIRE(caps.magic == OCTOOL_CAPS_MAGIC && caps.version == 1 && caps.size == sizeof(caps));
    REQUIRE((caps.features & OCTOOL_CAP_CPU) && !caps.reserved[0] && !caps.reserved[1]);
    errno = 0; REQUIRE(ioctl(fd, _IO('O', 0x7f)) == -1 && errno == ENOTTY);
    volatile uint64_t *mailbox = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    REQUIRE(mailbox != MAP_FAILED);
    REQUIRE(mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd, page) == MAP_FAILED && errno == EINVAL);
    struct octool_hwio_req request = { .cmd = OCTOOL_OP_CPUID, .user_id = UINT64_MAX };
    REQUIRE(sizeof(request) == 96);
    REQUIRE(write(fd, &request, sizeof(request) - 1) == -1 && errno == EINVAL);
    mailbox[0] = 0; mailbox[1] = UINT64_MAX;
    REQUIRE(write(fd, &request, sizeof(request)) == (ssize_t)sizeof(request));
    REQUIRE(mailbox[0] == ((uint64_t)(uint32_t)-EINVAL << 32 | 1) && mailbox[1] == 0);
    REQUIRE(munmap((void *)mailbox, (size_t)page) == 0 && close(fd) == 0);

    hwio_t *handle = hwio_open(NULL);
    REQUIRE(handle != NULL && hwio_backend_for(handle, HWIO_FAM_CPU) == HWIO_BE_MODULE);
    cpu_set_t saved;
    REQUIRE(sched_getaffinity(0, sizeof(saved), &saved) == 0);
    REQUIRE(sysconf(_SC_NPROCESSORS_ONLN) == 2);
    for (unsigned cpu = 0; cpu < 2; ++cpu) {
        cpu_set_t selected; CPU_ZERO(&selected); CPU_SET(cpu, &selected);
        REQUIRE(sched_setaffinity(0, sizeof(selected), &selected) == 0);
        for (unsigned leaf = 0; leaf < 2; ++leaf) {
            unsigned native[4], actual[4], a = leaf, b, c = 0, d;
            __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
            native[0] = a; native[1] = b; native[2] = c; native[3] = d;
            REQUIRE(hwio_cpuid(handle, cpu, leaf, 0, actual) == 0);
            REQUIRE(memcmp(native, actual, sizeof(actual)) == 0);
        }
    }
    REQUIRE(sched_setaffinity(0, sizeof(saved), &saved) == 0);
    unsigned cores = 0;
    REQUIRE(hwio_cpu_cores(handle, &cores) == 0 && cores == 2);
    hwio_close(handle);

    // Prove the capability gate as well as the installed 0600 node mode.
    // This temporary permissive mode exists only in the isolated guest.
    REQUIRE(chmod("/dev/mydev", 0666) == 0);
    pid_t child = fork(); REQUIRE(child >= 0);
    if (!child) {
        if (setgid(65534) || setuid(65534)) _exit(2);
        fd = open("/dev/mydev", O_RDWR);
        _exit(fd == -1 && errno == EPERM ? 0 : 3);
    }
    int status;
    REQUIRE(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    REQUIRE(chmod("/dev/mydev", 0600) == 0);
    printf("OCTOOL_GUEST_PROBE={\"kernel\":\"%s\",\"cpus\":2,\"module_hal_cpuid\":true,"
           "\"caps_ioctl\":true,\"invalid_cpu\":true,\"capability_denial\":true,\"node_mode\":\"0600\","
           "\"register_access_tested\":false}\n", uts.release);
    return 0;
}
