// SPDX-License-Identifier: GPL-2.0
/* Native execution of only the eight unchanged, hash-pinned wrapper bodies. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern int legacy_fd;
extern uint64_t *legacy_mailbox, legacy_request[12];
extern void fake_configure(uint64_t *, const char *);
extern uint64_t legacy_0(uint64_t, uint64_t), legacy_1(uint64_t, uint64_t);
extern uint64_t legacy_2(uint64_t, uint64_t), legacy_3(uint64_t, uint64_t);
extern uint64_t legacy_4(uint64_t, uint64_t), legacy_5(uint64_t, uint64_t);
extern uint64_t legacy_6(uint64_t, uint64_t), legacy_7(uint64_t, uint64_t);

int main(int argc, char **argv)
{
    uint64_t (*functions[])(uint64_t, uint64_t) = {
        legacy_0, legacy_1, legacy_2, legacy_3,
        legacy_4, legacy_5, legacy_6, legacy_7
    };
    uint64_t result;
    int index;
    unsigned i;
    if (argc != 3)
        return 92;
    legacy_fd = 600; /* Deliberately never an opened OS/device descriptor. */
    legacy_mailbox = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (legacy_mailbox == MAP_FAILED)
        return 93;
    for (i = 0; i < 12; ++i)
        legacy_request[i] = UINT64_C(0xa0a0000000000000) + i;
    legacy_request[1] = 71;
    fake_configure(legacy_mailbox, argv[2]);
    if (!strcmp(argv[1], "foreign")) {
        ssize_t n = write(600, legacy_request, 96);
        int error = errno;
        printf("{\"event\":\"foreign\",\"wrote\":%ld,\"errno\":%d}\n",
               (long)n, error);
        return n == -1 && error == EIO ? 0 : 94;
    }
    index = atoi(argv[1]);
    if (index < 0 || index > 7)
        return 95;
    result = functions[index](UINT64_C(0x12345000), UINT64_C(0x1122334455667788));
    printf("{\"event\":\"returned\",\"value\":\"%016lx\","
           "\"done\":\"%016lx\",\"result\":\"%016lx\"}\n",
           (unsigned long)result, (unsigned long)legacy_mailbox[0],
           (unsigned long)legacy_mailbox[1]);
    return 0;
}
