#!/bin/bash
# Separate minimal images prove there is no hidden desktop/Qt dependency.
set -euo pipefail
action=${1:?build or runtime required}
target=${2:?target required}
test "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1
test "$(uname -m)" = x86_64
case "$action" in build|runtime) ;; *) exit 2;; esac
. /etc/os-release
case "$target" in
    el8|el9|el10) test "$ID" = rocky && test "${VERSION_ID%%.*}" = "${target#el}"; family=rpm;;
    ubuntu20.04|ubuntu22.04|ubuntu24.04|ubuntu26.04) test "$ID" = ubuntu && test "$VERSION_ID" = "${target#ubuntu}"; family=deb;;
    debian11|debian12|debian13) test "$ID" = debian && test "${VERSION_ID%%.*}" = "${target#debian}"; family=deb;;
    *) exit 2;;
esac
mkdir -p /out
cat /etc/os-release > "/out/os-release-$action.txt"
unset DISPLAY WAYLAND_DISPLAY QT_QPA_PLATFORM DBUS_SESSION_BUS_ADDRESS
export DEBIAN_FRONTEND=noninteractive
py=python3
if [ "$family" = deb ]; then
    if [ "$target" = debian11 ]; then
        # Same signed final-LTS snapshot as the existing portability workflow.
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
    apt-get install -y --no-install-recommends python3 ca-certificates util-linux passwd
    if [ "$action" = build ]; then
        apt-get install -y --no-install-recommends cmake gcc g++ make dpkg-dev binutils
    fi
else
    if [ "$target" = el8 ]; then py=python3.9; package_python=python39; else package_python=python3; fi
    dnf install -y --setopt=install_weak_deps=False "$package_python" ca-certificates util-linux shadow-utils
    if [ "$action" = build ]; then
        dnf install -y --setopt=install_weak_deps=False cmake gcc gcc-c++ make rpm-build binutils
    fi
    if [ "$target" = el8 ]; then "$py" port/ci/container-nss.py --output "/out/nss-$action.json"; fi
fi
if [ "$action" = build ]; then
    cmake -S cli -B build/cli -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE="$(command -v "$py")"
    cmake --build build/cli --parallel 2
    (cd build/cli && ctest --output-on-failure) 2>&1 | tee /out/ctest.txt
    cp build/cli/Testing/Temporary/LastTest.log /out/LastTest.log
    "$py" port/tools/build_cli.py --format "$family" --output /out/packages
else
    if [ "$family" = deb ]; then
        apt-get install -y --no-install-recommends /out/packages/*.deb
    else
        dnf install -y --setopt=install_weak_deps=False /out/packages/*.rpm
    fi
    "$py" port/ci/cli-smoke.py --output /out/runtime.json
    runuser -u nobody -- /usr/bin/octool-cli diagnose > /out/unprivileged-diagnose.json
    "$py" -c 'import json; r=json.load(open("/out/unprivileged-diagnose.json")); assert r["ok"] and r["data"]["effective_uid"] != 0'
    if [ "$family" = deb ]; then
        dpkg-query -W > /out/runtime-packages.txt
        apt-get remove -y octool-cli
        test ! -e /usr/bin/octool-cli
        apt-get install -y --no-install-recommends /out/packages/*.deb
    else
        rpm -qa | sort > /out/runtime-packages.txt
        dnf remove -y octool-cli
        test ! -e /usr/bin/octool-cli
        dnf install -y --setopt=install_weak_deps=False /out/packages/*.rpm
    fi
    octool-cli diagnose > /out/reinstalled-diagnose.json
    "$py" -c 'import json; assert json.load(open("/out/reinstalled-diagnose.json"))["ok"]'
fi
