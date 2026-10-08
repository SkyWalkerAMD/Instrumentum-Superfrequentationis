#!/bin/sh
# Usage: dkms-install.sh [version]   (run as root)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
VER="${1:-$(cat "$HERE/../VERSION")}"
case "$VER" in ''|*[!0-9.]*) echo 'version must contain digits and dots' >&2; exit 2;; esac
NAME=octool-hwio
SRC=/usr/src/${NAME}-${VER}
mkdir -p "$SRC/kmod" "$SRC/abi"
cp "$HERE/kmod/octool_hwio.c" "$HERE/kmod/Kbuild" "$HERE/kmod/Makefile" "$HERE/kmod/octool_hwio_abi.h" "$SRC/kmod/"
cp "$HERE/kmod/class_create_probe.c" "$SRC/kmod/"
cp "$HERE/kmod/octool_bus_access.h" "$SRC/kmod/"
cp "$HERE/abi/octool_hwio_abi.h" "$SRC/abi/"
cp "$HERE/abi/octool_hwio_caps.h" "$SRC/abi/"
cp "$HERE/packaging/dkms.conf" "$SRC/dkms.conf"
sed -i "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$VER\"/" "$SRC/dkms.conf"
# Compiler selection follows the installed headers inside dkms.conf.
sh "$HERE/packaging/dkms-register.sh" "$VER"
echo "installed. Load with: modprobe octool_hwio  (creates /dev/mydev)"
