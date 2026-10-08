CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

HEADERS := ../hal/octool_hwio.h ../abi/octool_hwio_abi.h ../abi/octool_hwio_caps.h octool_parity_trace.h

loopback: hwio_loopback_test.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

octool_parity: octool_parity.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

hwio_smoke: hwio_smoke.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

transport: hwio_transport_test.c ../hal/octool_hwio.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(foreach call,open close mmap munmap read write pread pwrite __pread_chk ioctl,-Wl,--wrap=$(call))

bus-acpi bus-noacpi: hwio_bus_test.c ../kmod/octool_bus_access.h
	$(CC) $(CFLAGS) -Werror -DOCTOOL_TEST_ACPI=$(if $(filter bus-acpi,$@),1,0) -o $@ $<

port-thread: hwio_port_thread_test.c ../hal/octool_hwio.c $(HEADERS) io-stubs/sys/io.h
	$(CC) $(CFLAGS) -Werror -Iio-stubs -o $@ $(filter %.c,$^) -pthread -Wl,--wrap=open -Wl,--wrap=fopen

# LD_PRELOAD observer used to capture a real octool session's reads
octool_capture.so: octool_capture.c $(HEADERS)
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o $@ $< -ldl -lpthread

capture_backend.so: capture_backend.c capture_test.h
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o $@ $< -pthread

capture-harness: capture_harness.c capture_test.h
	$(CC) $(CFLAGS) -Werror -rdynamic -o $@ $< -ldl -pthread

capture-check: octool_capture.so capture_backend.so capture-harness octool_parity
	python3 capture-regression.py $(if $(CAPTURE_RESULTS),--output $(CAPTURE_RESULTS))

# offline checks: protocol loopback + parity-engine selftest + shim compiles
# (no kernel/hardware needed)
check: loopback transport bus-acpi bus-noacpi port-thread octool_parity capture-check
	./loopback
	./transport
	./bus-acpi
	./bus-noacpi
	./port-thread
	./octool_parity --selftest

clean:
	rm -f loopback transport bus-acpi bus-noacpi port-thread octool_parity octool_capture.so hwio_smoke capture-harness capture_backend.so
.PHONY: check clean capture-check
