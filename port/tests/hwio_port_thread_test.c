// SPDX-License-Identifier: GPL-2.0
/* Real HAL, fake sys/io.h: never grant privileges or execute IN/OUT. */
#include "../hal/octool_hwio.h"
#include <sys/io.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static _Thread_local int granted, permission_error;
static _Thread_local unsigned permission_calls, accesses;

int __wrap_open(const char *path, int flags, ...)
{
    (void)flags; assert(!strcmp(path, "/dev/mydev"));
    errno = ENOENT; return -1;
}
FILE *__wrap_fopen(const char *path, const char *mode)
{
    assert(!strcmp(path, "/sys/kernel/security/lockdown") && !strcmp(mode, "r"));
    errno = ENOENT; return NULL;
}
int iopl(int level)
{
    assert(level == 3); permission_calls++;
    if (permission_error) { errno = permission_error; return -1; }
    granted = 1; return 0;
}
static void access_port(uint16_t port)
{
    assert(port == 0x1234 && granted); accesses++;
}
uint8_t inb(uint16_t port) { access_port(port); return 0x98; }
uint16_t inw(uint16_t port) { access_port(port); return 0xba98; }
uint32_t inl(uint16_t port) { access_port(port); return 0xfedcba98; }
void outb(uint8_t v, uint16_t p) { access_port(p); assert(v == 0x98); }
void outw(uint16_t v, uint16_t p) { access_port(p); assert(v == 0xba98); }
void outl(uint32_t v, uint16_t p) { access_port(p); assert(v == 0xfedcba98); }

static void allowed(hwio_t *h)
{
    for (int width = 1; width <= 4; width *= 2) {
        uint32_t v = 0;
        uint32_t mask = width == 4 ? UINT32_MAX : (1u << (width * 8)) - 1;
        unsigned calls = permission_calls, count = accesses;
        assert(hwio_io_read(h, 0x1234, width, &v) == 0 && v == (0xfedcba98u & mask));
        assert(hwio_io_write(h, 0x1234, width, 0xfedcba98) == 0);
        assert(permission_calls == calls + 2 && accesses == count + 2);
    }
}
static void denied(hwio_t *h)
{
    uint32_t v = 0xabcdef;
    unsigned calls = permission_calls, count = accesses;
    permission_error = EPERM; granted = 0;
    assert(hwio_io_read(h, 0x1234, 4, &v) == -EPERM && v == 0xabcdef);
    assert(hwio_io_write(h, 0x1234, 4, 0xfedcba98) == -EPERM);
    assert(permission_calls == calls + 2 && accesses == count);
    permission_error = 0;
}
static void *other_thread(void *ctx)
{
    hwio_t *h = ctx;
    assert(!granted && !permission_calls && !accesses);
    /* Handle was already used successfully on another thread. This one has
     * no permission and must return an error before touching a port. */
    denied(h);
    allowed(h);
    return NULL;
}
int main(void)
{
    pthread_t thread;
    hwio_t *h = hwio_open(NULL);
    uint32_t value = 7;
    assert(h && hwio_backend_for(h, HWIO_FAM_IO) == HWIO_BE_DIRECT);
    assert(hwio_io_read(h, 0x1234, 3, &value) == -EINVAL && value == 7);
    assert(hwio_io_write(h, 0x1234, 8, 0) == -EINVAL);
    assert(!permission_calls && !accesses);
    allowed(h);
    assert(!pthread_create(&thread, NULL, other_thread, h));
    assert(!pthread_join(thread, NULL)); /* All handle use stays serialized. */
    /* Permission may also be revoked on the original thread. */
    denied(h);
    allowed(h);
    hwio_close(h);
    puts("PASS: direct I/O permission on each calling thread, revocation, no unauthorized instructions");
    return 0;
}
