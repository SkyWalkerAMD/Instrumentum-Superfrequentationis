#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory required}
mkdir -p "$out"
count=0
for tree in /lib/modules/*/build; do
    [ -f "$tree/Makefile" ] || continue
    kernel=${tree%/build}
    kernel=${kernel##*/}
    [ -s "$tree/Module.symvers" ] || { echo "missing Module.symvers: $tree" >&2; exit 1; }
    cc=gcc
    compiler=$(awk '$2 == "CONFIG_GCC_VERSION" {print int($3/10000)}' "$tree/include/generated/autoconf.h")
    if [ -n "$compiler" ] && command -v "gcc-$compiler" >/dev/null; then cc=gcc-$compiler; fi
    dest=$out/$kernel
    mkdir -p "$dest"
    make -C "$root/port/kmod" KDIR="$tree" clean
    make -C "$root/port/kmod" KDIR="$tree" CC="$cc" V=1 2>&1 | tee "$dest/build.log"
    module=$root/port/kmod/octool_hwio.ko
    test -s "$module"
    test "$(modinfo -F vermagic "$module" | awk '{print $1}')" = "$kernel"
    cp "$module" "$dest/"
    modinfo "$module" > "$dest/modinfo.txt"
    # Exercise the actual target sign-file with an ephemeral CI certificate.
    # This checks signing, not firmware enrollment or Secure Boot loading.
    signing=$(mktemp -d)
    chmod 700 "$signing"
    openssl req -new -x509 -newkey rsa:2048 -nodes -days 1 \
        -subj '/CN=OCTool CI test only/' -keyout "$signing/mok.key" \
        -outform DER -out "$signing/mok.der" >/dev/null 2>&1
    chmod 600 "$signing/mok.key"
    cp "$module" "$dest/octool_hwio.test-signed.ko"
    OCTOOL_MOK_KEY="$signing/mok.key" OCTOOL_MOK_CERT="$signing/mok.der" \
        sh "$root/port/packaging/dkms-sign.sh" "$kernel" "$dest/octool_hwio.test-signed.ko"
    test "$(modinfo -F sig_hashalgo "$dest/octool_hwio.test-signed.ko")" = sha256
    rm -f "$signing/mok.key" "$signing/mok.der"
    rmdir "$signing"
    count=$((count+1))
done
[ "$count" -gt 0 ] || { echo 'No target kernel headers; refusing to test the host uname kernel.' >&2; exit 1; }
