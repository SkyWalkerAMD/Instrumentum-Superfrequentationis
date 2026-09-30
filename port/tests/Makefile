CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

HEADERS := ../hal/octool_hwio.h ../abi/octool_hwio_abi.h octool_parity_trace.h

loopback: hwio_loopback_test.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

octool_parity: octool_parity.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

hwio_smoke: hwio_smoke.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

# LD_PRELOAD observer used to capture a real octool session's reads
octool_capture.so: octool_capture.c $(HEADERS)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< -ldl -lpthread

# offline checks: protocol loopback + parity-engine selftest + shim compiles
# (no kernel/hardware needed)
check: loopback octool_parity octool_capture.so
	./loopback
	./octool_parity --selftest

clean:
	rm -f loopback octool_parity octool_capture.so hwio_smoke
.PHONY: check clean
