# Source only in EL7 userspace build steps, never during runtime validation or
# kernel builds. The native GCC remains responsible for the EL7 3.10 kernel.
test "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1
test -x /opt/rh/devtoolset-11/root/usr/bin/g++
export PATH="/opt/rh/devtoolset-11/root/usr/bin:$PATH"
export OCTOOL_ABI_FLOOR=el7
