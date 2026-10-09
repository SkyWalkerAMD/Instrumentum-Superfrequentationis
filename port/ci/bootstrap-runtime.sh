#!/bin/bash
# Fresh runtime image: deliberately no DKMS, compiler, SDK or kernel headers.
set -euo pipefail
target=${1:?target required}
test "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1
test "$(uname -m)" = x86_64
. /etc/os-release
case "$target" in
    el8|el9|el10)
        case "$ID" in rocky|almalinux|rhel) ;; *) exit 2;; esac
        test "${VERSION_ID%%.*}" = "${target#el}";;
    ubuntu20.04|ubuntu22.04|ubuntu24.04|ubuntu26.04)
        test "$ID" = ubuntu && test "$VERSION_ID" = "${target#ubuntu}";;
    debian11|debian12|debian13)
        test "$ID" = debian && test "${VERSION_ID%%.*}" = "${target#debian}";;
    *) exit 2;;
esac
export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null; then
    if [ "$target" = debian11 ]; then
        # Same official, signed final-LTS snapshot as the build environment.
        cat > /etc/apt/octool-bullseye-snapshot.list <<'EOF'
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20260831T235959Z/ bullseye main
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20260831T235959Z/ bullseye-security main
EOF
        cat > /etc/apt/apt.conf.d/99octool-snapshot <<'EOF'
Dir::Etc::sourcelist "/etc/apt/octool-bullseye-snapshot.list";
Dir::Etc::sourceparts "-";
Acquire::Retries "3";
EOF
    fi
    apt-get update
    apt-get install -y --no-install-recommends python3 ca-certificates util-linux passwd dbus-x11
    if [ "$target" = ubuntu26.04 ]; then
        apt-get install -y --no-install-recommends xwayland-run mutter xwayland xauth
    else
        apt-get install -y --no-install-recommends xvfb xauth
    fi
else
    dnf install -y --setopt=install_weak_deps=False dnf-plugins-core epel-release
    for repo in crb powertools; do
        if dnf repolist --all | awk '{print $1}' | grep -qx "$repo"; then
            dnf config-manager --set-enabled "$repo"
        fi
    done
    dnf install -y --setopt=install_weak_deps=False ca-certificates util-linux shadow-utils /usr/bin/dbus-run-session /usr/bin/dbus-send
    if [ "$target" = el8 ]; then
        dnf install -y --setopt=install_weak_deps=False python39
        mkdir -p /usr/local/bin
        ln -sf /usr/bin/python3.9 /usr/local/bin/python3
    else
        dnf install -y --setopt=install_weak_deps=False python3
    fi
    if [ "$target" = el10 ]; then
        dnf install -y --setopt=install_weak_deps=False xwayland-run mutter xorg-x11-server-Xwayland /usr/bin/xauth
    else
        dnf install -y --setopt=install_weak_deps=False xorg-x11-server-Xvfb xorg-x11-xauth
    fi
fi
