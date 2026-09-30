#!/bin/sh
# Invoked by package postinstall; root required. No module is loaded here.
set -eu
version=$1
status=$(dkms status -m octool-hwio -v "$version")
if [ -z "$status" ]; then
    dkms add -m octool-hwio -v "$version"
fi
count=0
for tree in /lib/modules/*/build; do
    [ -f "$tree/Makefile" ] || continue
    kernel=${tree%/build}
    kernel=${kernel##*/}
    # A newer Ubuntu HWE may need a separately installed compiler. Select by
    # compiler metadata, never by the kernel release or distribution version.
    cc=gcc
    if [ -r "$tree/include/generated/autoconf.h" ]; then
        gcc_version=$(awk '$2 == "CONFIG_GCC_VERSION" {print $3}' "$tree/include/generated/autoconf.h")
        if [ -n "$gcc_version" ] && [ "$gcc_version" -gt 0 ]; then
            major=$((gcc_version / 10000))
            if command -v "gcc-$major" >/dev/null 2>&1; then cc="gcc-$major"; fi
        fi
    fi
    # Reinstalling a package release of the same module version must use the
    # newly unpacked sources, not DKMS's previous build cache.
    OCTOOL_KERNEL_CC="$cc" dkms build -m octool-hwio -v "$version" -k "$kernel" --force
    dkms install -m octool-hwio -v "$version" -k "$kernel" --force
    count=$((count + 1))
done
if [ "$count" -eq 0 ]; then
    echo 'octool: no configured kernel headers in /lib/modules/*/build; install matching headers and reconfigure this package.' >&2
    exit 1
fi
echo 'octool: DKMS installed. Enroll the signing certificate if Secure Boot is enabled, then modprobe octool_hwio.'
