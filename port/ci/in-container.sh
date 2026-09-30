#!/bin/bash
# Internal entry point for disposable Linux containers (never loads modules).
set -euo pipefail
action=${1:?action required}
target=${2:?target required}
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out=/out
mkdir -p "$out"
case "$action" in
    runtime|kernel) mode=$action;;
    sdk|baseline|desktop) mode=build;;
    *) echo 'unknown action' >&2; exit 2;;
esac
cat /etc/os-release > "$out/os-release-$action.txt"
bash port/ci/bootstrap.sh "$target" "$mode" 2>&1 | tee "$out/bootstrap-$action.log"
family=$(python3 - "$target" <<'PY'
import json, sys
print(next(t for t in json.load(open('port/ci/targets.json')) if t['id'] == sys.argv[1])['family'])
PY
)
cat /etc/os-release > "$out/os-release-$action.txt"
gcc --version > "$out/compiler-$action.txt"
ldd --version > "$out/libc-$action.txt"
if [ "$family" = deb ]; then
    dpkg-query -W > "$out/packages-$action.txt"
else
    rpm -qa | sort > "$out/packages-$action.txt"
fi
install_packages() {
    if [ "$family" = deb ]; then
        apt-get install -y --no-install-recommends "$out/packages/"*.deb
    else
        dnf install -y "$out/packages/"*.rpm
    fi
}
verify_dkms() {
    local n=0 tree kernel info
    for tree in /lib/modules/*/build; do
        [ -f "$tree/Makefile" ] || continue
        kernel=${tree%/build}; kernel=${kernel##*/}
        info=$(dkms status -m octool-hwio -v "$(cat VERSION)" -k "$kernel")
        printf '%s\n' "$info"
        grep -q ': installed' <<< "$info"
        test "$(modinfo -k "$kernel" -F vermagic octool_hwio | awk '{print $1}')" = "$kernel"
        test "$(modinfo -k "$kernel" -F version octool_hwio)" = "$(cat VERSION)"
        modinfo -k "$kernel" octool_hwio > "$out/dkms-$kernel.txt"
        n=$((n+1))
    done
    [ "$n" -gt 0 ]
}
build_sdk() {
    test "$target" = el8
    bash port/ci/build-qt-el8.sh 2>&1 | tee "$out/qt-build.log"
    tar -czf "$out/qt-sdk.tar.gz" -C /opt octool-qt
    sha256sum "$out/qt-sdk.tar.gz" > "$out/qt-sdk.sha256"
    cp /opt/octool-qt/licenses/config.summary "$out/qt-config-summary.txt"
}
case "$action" in
    kernel)
        make -C port/hal
        make -C port/tests check hwio_smoke 2>&1 | tee "$out/offline.log"
        python3 -m unittest discover -s port/tests -p 'test_*.py' -v
        bash port/ci/build-kernels.sh "$out/kernels"
        python3 port/tools/build_packages.py --format "$family" --module-only --output "$out/packages"
        install_packages
        verify_dkms
        ;;
    sdk)
        # Toolchain validation can proceed while GUI recovery is in progress.
        # This action does not satisfy the GUI or complete-matrix gates.
        build_sdk
        ;;
    baseline)
        test "$target" = el8
        python3 port/tools/build_gui.py --preflight
        build_sdk
        python3 port/tools/build_gui.py --stage "$out/gui-stage"
        python3 port/tools/check_elf.py "$out/gui-stage/opt/octool/bin/octool-real" > "$out/abi.json"
        tar -czf "$out/gui-stage.tar.gz" -C "$out/gui-stage" .
        ;;
    desktop)
        tar -xzf /inputs/qt-sdk.tar.gz -C /opt
        mkdir -p /tmp/octool-release-stage
        tar -xzf /inputs/gui-stage.tar.gz -C /tmp/octool-release-stage
        python3 port/tools/build_gui.py --build-dir "$root/build/gui-$target" --stage "$out/native-stage"
        python3 port/tools/check_elf.py --inspect "$out/native-stage/opt/octool/bin/octool-real" > "$out/native-abi.json"
        make -C port/tests check 2>&1 | tee "$out/offline.log"
        python3 port/tools/build_packages.py --format "$family" --gui-stage /tmp/octool-release-stage --output "$out/packages"
        ;;
    runtime)
        install_packages
        verify_dkms
        ldd /opt/octool/bin/octool-real | tee "$out/ldd-release.txt"
        ! grep -q 'not found' "$out/ldd-release.txt"
        ldd "$out/native-stage/opt/octool/bin/octool-real" | tee "$out/ldd-native.txt"
        ! grep -q 'not found' "$out/ldd-native.txt"
        display=$(python3 - "$target" <<'PY'
import json, sys
print(next(t for t in json.load(open('port/ci/targets.json')) if t['id'] == sys.argv[1])['display'])
PY
)
        useradd -m octool-smoke
        mkdir -p "$out/smoke"
        chown octool-smoke:octool-smoke "$out/smoke"
        runuser -u octool-smoke -- env HOME=/home/octool-smoke bash -c \
            'cd "$HOME"; exec bash "$1/port/ci/headless-smoke.sh" "$2" --log /out/smoke/release.log' \
            _ "$root" "$display"
        runuser -u octool-smoke -- env HOME=/home/octool-smoke bash -c \
            'cd "$HOME"; exec bash "$1/port/ci/headless-smoke.sh" "$2" --binary /out/native-stage/opt/octool/bin/octool-real --log /out/smoke/native.log' \
            _ "$root" "$display"
        # Exercise removal/reinstall in this disposable container too.
        if [ "$family" = deb ]; then
            apt-get purge -y octool octool-hwio-dkms
        else
            dnf remove -y octool octool-hwio-dkms
        fi
        test -z "$(dkms status -m octool-hwio -v "$(cat VERSION)")"
        install_packages
        verify_dkms
        ;;
    *) echo 'unknown action' >&2; exit 2;;
esac
