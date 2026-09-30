#!/bin/bash
# Run only inside the selected disposable target container as root.
set -euo pipefail
target=${1:?target id required}
mode=${2:-build}
root=$(cd "$(dirname "$0")/../.." && pwd)
case "$mode" in build|kernel|runtime) ;; *) echo "invalid bootstrap mode: $mode" >&2; exit 2;; esac
if [ "${OCTOOL_DISPOSABLE_CONTAINER:-}" != 1 ]; then
    echo 'bootstrap must run in a disposable container with OCTOOL_DISPOSABLE_CONTAINER=1' >&2
    exit 2
fi
# Validate the image before installing anything. A mistyped target must not
# silently build against another distribution's headers and report success.
. /etc/os-release
case "$target" in
    el8|el9|el10)
        case "$ID" in rocky|almalinux|rhel) ;; *) echo "wrong image ID: $ID" >&2; exit 2;; esac
        test "${VERSION_ID%%.*}" = "${target#el}"
        ;;
    ubuntu20.04|ubuntu22.04|ubuntu24.04|ubuntu26.04)
        test "$ID" = ubuntu && test "$VERSION_ID" = "${target#ubuntu}"
        ;;
    debian11|debian12|debian13)
        test "$ID" = debian && test "${VERSION_ID%%.*}" = "${target#debian}"
        ;;
    *) echo "unknown target: $target" >&2; exit 2;;
esac
test "$(uname -m)" = x86_64
export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null; then
    apt-get update
    apt-get install -y --no-install-recommends ca-certificates python3 gcc g++ make \
        dkms kmod libelf-dev libc6-dev openssl mokutil util-linux passwd
else
    dnf install -y dnf-plugins-core epel-release
    for repo in crb powertools; do
        if dnf repolist --all | awk '{print $1}' | grep -qx "$repo"; then
            dnf config-manager --set-enabled "$repo"
        fi
    done
    dnf install -y gcc gcc-c++ make dkms kmod elfutils-libelf-devel openssl mokutil util-linux shadow-utils
    if [ "$target" = el8 ]; then
        dnf install -y python39
        # Container-local interpreter; EL8's default Python 3.6 is too old.
        mkdir -p /usr/local/bin
        ln -sf /usr/bin/python3.9 /usr/local/bin/python3
    else
        dnf install -y python3
    fi
fi
# A process substitution hides Python failures from set -e. Capture it first.
kernel_list=$(python3 - "$root/port/ci/targets.json" "$target" <<'PY'
import json, sys
target = next(t for t in json.load(open(sys.argv[1])) if t['id'] == sys.argv[2])
assert target['kernel_packages'], 'empty kernel package list'
print('\n'.join(target['kernel_packages']))
PY
)
mapfile -t kernels <<< "$kernel_list"
if command -v apt-get >/dev/null; then
    apt-get install -y --no-install-recommends "${kernels[@]}"
    for tree in /lib/modules/*/build; do
        [ -r "$tree/include/generated/autoconf.h" ] || continue
        compiler=$(awk '$2 == "CONFIG_GCC_VERSION" {print int($3/10000)}' "$tree/include/generated/autoconf.h")
        if [ -n "$compiler" ] && [ "$compiler" -gt 0 ]; then
            apt-get install -y --no-install-recommends "gcc-$compiler"
        fi
    done
else
    dnf install -y "${kernels[@]}"
fi
if [ "$mode" = kernel ]; then
    if command -v apt-get >/dev/null; then
        apt-get install -y --no-install-recommends dpkg-dev binutils
    else
        dnf install -y rpm-build binutils
    fi
    exit 0
fi
if [ "$mode" = runtime ]; then
    display=$(python3 - "$root/port/ci/targets.json" "$target" <<'PY'
import json, sys
print(next(t for t in json.load(open(sys.argv[1])) if t['id'] == sys.argv[2])['display'])
PY
)
    if command -v apt-get >/dev/null; then
        apt-get install -y --no-install-recommends x11-utils dbus-x11
        if [ "$display" = xwayland ]; then
            apt-get install -y --no-install-recommends xwayland-run mutter xwayland
        else
            apt-get install -y --no-install-recommends xvfb xauth
        fi
    else
        dnf install -y /usr/bin/xwininfo /usr/bin/xprop /usr/bin/dbus-run-session
        if [ "$display" = xwayland ]; then
            dnf install -y xwayland-run mutter xorg-x11-server-Xwayland
        else
            dnf install -y xorg-x11-server-Xvfb xorg-x11-xauth
        fi
    fi
    exit 0
fi
if command -v apt-get >/dev/null; then
    apt-get install -y --no-install-recommends dpkg-dev fakeroot binutils pkg-config curl xz-utils perl nasm \
        libgl1-mesa-dev libegl1-mesa-dev libx11-dev libxext-dev libxrender-dev libxcb1-dev \
        libxcb-util0-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-render-util0-dev \
        libxcb-icccm4-dev libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev \
        libxkbcommon-x11-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev \
        libglib2.0-dev libudev-dev libhwloc-dev libbluetooth-dev libcups2-dev \
        libwayland-dev libxcomposite-dev libdrm-dev
else
    dnf install -y rpm-build binutils pkgconf-pkg-config curl xz perl nasm \
        mesa-libGL-devel mesa-libEGL-devel libX11-devel libXext-devel libXrender-devel \
        libxcb-devel xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel \
        xcb-util-renderutil-devel xcb-util-wm-devel libxkbcommon-devel libxkbcommon-x11-devel \
        fontconfig-devel freetype-devel dbus-devel glib2-devel systemd-devel hwloc-devel \
        bluez-libs-devel cups-devel wayland-devel libXcomposite-devel libdrm-devel
fi
