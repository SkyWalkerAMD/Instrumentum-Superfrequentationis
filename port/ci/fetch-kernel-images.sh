#!/bin/bash
# Download the exact official kernel matching each tested header set. Extract
# only: do not install kernel packages or run their bootloader/initramfs hooks.
set -euo pipefail
test "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1
out=${1:?VM input directory required}
mkdir -p "$out"
if ! command -v apt-get >/dev/null; then dnf install -y cpio; fi
for tree in /lib/modules/*/build; do
    [ -f "$tree/Makefile" ] || continue
    kernel=${tree%/build}; kernel=${kernel##*/}
    dest=$out/$kernel
    mkdir -p "$dest/download" "$dest/extracted"
    if command -v apt-get >/dev/null; then
        (cd "$dest/download" && apt-get download "linux-image-$kernel")
        for package in "$dest/download/"*.deb; do dpkg-deb -x "$package" "$dest/extracted"; done
    else
        dnf download --destdir "$dest/download" "kernel-core-$kernel"
        for package in "$dest/download/"*.rpm; do
            (cd "$dest/extracted" && rpm2cpio "$package" | cpio -idm --quiet)
        done
    fi
    image=$(find "$dest/extracted" -type f \( -name "vmlinuz-$kernel" -o -path "*/$kernel/vmlinuz" \) -print)
    test -n "$image" && test "$(printf '%s\n' "$image" | wc -l)" -eq 1
    cp "$image" "$dest/vmlinuz"
    chmod 0644 "$dest/vmlinuz"
    (cd "$dest" && sha256sum vmlinuz) > "$dest/vmlinuz.sha256"
done
