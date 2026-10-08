// SPDX-License-Identifier: GPL-2.0
/* Metadata-only probe for the controlled runner. No read/write/mmap request,
 * MSR, MMIO, PCI, port I/O or EC access, and no original GUI execution. */
#include "../../port/abi/octool_hwio_caps.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    struct octool_hwio_caps caps = {0};
    const uint64_t required = OCTOOL_CAP_ALL & ~OCTOOL_CAP_EC;
    int fd = open("/dev/mydev", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open metadata device"); return 1; }
    int result = ioctl(fd, OCTOOL_HWIO_GET_CAPS_V1, &caps);
    int valid = result == 0 && sizeof(caps) == 32 && caps.magic == OCTOOL_CAPS_MAGIC &&
        caps.version == OCTOOL_CAPS_VERSION && caps.size == sizeof(caps) &&
        (caps.features & required) == required && !(caps.features & ~OCTOOL_CAP_ALL) &&
        !caps.reserved[0] && !caps.reserved[1];
    errno = 0;
    int unknown = ioctl(fd, _IO('O', 0x81), 0) == -1 && errno == ENOTTY;
    errno = 0;
    int bad_pointer = ioctl(fd, OCTOOL_HWIO_GET_CAPS_V1, NULL) == -1 && errno == EFAULT;
    int closed = close(fd) == 0;
    printf("{\"caps_valid\":%s,\"magic\":\"0x%08" PRIx32 "\",\"version\":%u,"
           "\"size\":%u,\"features\":\"0x%016" PRIx64 "\","
           "\"unknown_command_enotty\":%s,\"bad_pointer_efault\":%s,\"close_succeeded\":%s}\n",
           valid ? "true" : "false", (uint32_t)caps.magic, (unsigned)caps.version,
           (unsigned)caps.size, (uint64_t)caps.features, unknown ? "true" : "false",
           bad_pointer ? "true" : "false", closed ? "true" : "false");
    return valid && unknown && bad_pointer && closed ? 0 : 1;
}
