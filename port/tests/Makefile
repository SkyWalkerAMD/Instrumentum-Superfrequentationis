CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

HEADERS := ../hal/octool_hwio.h ../abi/octool_hwio_abi.h octool_parity_trace.h

loopback: hwio_loopback_test.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

octool_parity: octool_parity.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

hwio_smoke: hwio_smoke.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

transport: hwio_transport_test.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(foreach call,open close mmap munmap read write pread pwrite __pread_chk,-Wl,--wrap=$(call))

# LD_PRELOAD observer used to capture a real octool session's reads
octool_capture.so: octool_capture.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< -ldl -lpthread

# offline checks: protocol loopback + parity-engine selftest + shim compiles
# (no kernel/hardware needed)
check: loopback transport octool_parity octool_capture.so
	./loopback
	./transport
	./octool_parity --selftest

clean:
	rm -f loopback transport octool_parity octool_capture.so hwio_smoke
.PHONY: check clean
