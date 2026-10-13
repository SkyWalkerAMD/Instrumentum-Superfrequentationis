#!/bin/bash
# EL7 CI only. Oracle's signed EL7 repositories avoid expired CentOS mirrors.
set -euo pipefail
mode=${1:?build, kernel, runtime, cli-build or cli-runtime required}
test "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1
. /etc/os-release
test "$ID" = ol && test "${VERSION_ID%%.*}" = 7
test "$(uname -m)" = x86_64
case "$mode" in build|kernel|runtime|cli-build|cli-runtime) ;; *) exit 2;; esac
yum install -y oracle-softwarecollection-release-el7 oracle-epel-release-el7 yum-utils
yum-config-manager --enable ol7_optional_latest ol7_software_collections ol7_developer_EPEL
yum install -y ca-certificates util-linux shadow-utils rh-python38-python
# Only Python gets the SCL library path. Installed OCTool processes must use
# the system libraries, without an enabled compiler toolset or SDK.
mkdir -p /usr/local/bin
cat > /usr/local/bin/python3 <<'EOF'
#!/bin/bash
export LD_LIBRARY_PATH="/opt/rh/rh-python38/root/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec /opt/rh/rh-python38/root/usr/bin/python3 "$@"
EOF
chmod 755 /usr/local/bin/python3
if [ "$mode" = cli-runtime ]; then exit 0; fi
if [ "$mode" = runtime ]; then
    yum install -y dbus-x11 xorg-x11-server-Xvfb xorg-x11-xauth
    exit 0
fi
yum install -y gcc gcc-c++ make binutils rpm-build
if [ "$mode" = cli-build ]; then
    yum install -y devtoolset-11-gcc-c++ cmake3
    ln -sf /usr/bin/cmake3 /usr/local/bin/cmake
    ln -sf /usr/bin/ctest3 /usr/local/bin/ctest
    exit 0
fi
yum install -y dkms kmod elfutils-libelf-devel openssl mokutil kernel-devel
for tree in /usr/src/kernels/*; do
    [ -f "$tree/Makefile" ] && [ -s "$tree/include/config/kernel.release" ] || continue
    kernel=$(cat "$tree/include/config/kernel.release")
    [[ "$kernel" =~ ^[a-zA-Z0-9._+-]+$ ]] || exit 1
    test -s "$tree/Module.symvers"
    mkdir -p "/lib/modules/$kernel"
    link=/lib/modules/$kernel/build
    if [ -e "$link" ] || [ -L "$link" ]; then
        test "$(readlink -f "$link")" = "$(readlink -f "$tree")"
    else
        ln -s "$tree" "$link"
    fi
done
if [ "$mode" = kernel ]; then exit 0; fi
yum install -y devtoolset-11-gcc-c++ pkgconfig curl xz perl nasm \
    mesa-libGL-devel mesa-libEGL-devel libX11-devel libXext-devel libXrender-devel \
    libxcb-devel xcb-util-devel xcb-util-image-devel xcb-util-keysyms-devel \
    xcb-util-renderutil-devel xcb-util-wm-devel libxkbcommon-devel libxkbcommon-x11-devel \
    fontconfig-devel freetype-devel dbus-devel glib2-devel systemd-devel hwloc-devel \
    at-spi2-core-devel bluez-libs-devel cups-devel wayland-devel libXcomposite-devel libdrm-devel
