# Userspace HAL: static lib + headers. Link liboctool_hwio.a into octool.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC
AR      ?= ar
liboctool_hwio.a: octool_hwio.o
	$(AR) rcs $@ $^
octool_hwio.o: octool_hwio.c octool_hwio.h ../abi/octool_hwio_abi.h
	$(CC) $(CFLAGS) -c -o $@ octool_hwio.c
clean:
	rm -f octool_hwio.o liboctool_hwio.a
.PHONY: clean
