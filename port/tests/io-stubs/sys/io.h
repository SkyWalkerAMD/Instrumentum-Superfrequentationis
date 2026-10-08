/* SPDX-License-Identifier: GPL-2.0 */
/* Test include path ONLY. Replace privileged instructions with assertions. */
#ifndef OCTOOL_TEST_SYS_IO_H
#define OCTOOL_TEST_SYS_IO_H
#include <stdint.h>
int iopl(int level);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void outb(uint8_t value, uint16_t port);
void outw(uint16_t value, uint16_t port);
void outl(uint32_t value, uint16_t port);
#endif
