/* SPDX-License-Identifier: GPL-2.0 */
#ifndef OCTOOL_CAPTURE_TEST_H
#define OCTOOL_CAPTURE_TEST_H
#include <stdatomic.h>
#include <stdint.h>
struct capture_test_state {
    const char *mode;
    uint64_t *mailbox;
    _Atomic int calls, entered, release;
    int trace_stage;
    unsigned char wire[96];
};
extern struct capture_test_state capture_test;
#endif
