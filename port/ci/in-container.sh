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
if [ "$action" = runtime ]; then
    bash port/ci/bootstrap-runtime.sh "$target" 2>&1 | tee "$out/bootstrap-$action.log"
else
    bash port/ci/bootstrap.sh "$target" "$mode" 2>&1 | tee "$out/bootstrap-$action.log"
fi
family=$(python3 - "$target" <<'PY'
import json, sys
print(next(t for t in json.load(open('port/ci/targets.json')) if t['id'] == sys.argv[1])['family'])
PY
)
if [ "$action" = desktop ] && [ "$family" = deb ]; then
    # The EL8 static SDK's .prl files require these link interfaces; installing
    # a small set needed to configure Qt on EL8 does not provide them on Debian.
    apt-get install -y --no-install-recommends libzstd-dev libx11-xcb-dev \
        libxcb-glx0-dev libxcb-randr0-dev libxcb-render0-dev libxcb-shape0-dev \
        libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinput-dev
fi
cat /etc/os-release > "$out/os-release-$action.txt"
if [ "$action" != runtime ]; then gcc --version > "$out/compiler-$action.txt"; fi
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
    if [ -s "$out/qt-sdk.tar.gz" ] && [ -s "$out/qt-sdk.sha256" ]; then
        (cd "$out" && sha256sum -c qt-sdk.sha256)
        tar -xzf "$out/qt-sdk.tar.gz" -C /opt
        echo 'Reusing verified EL8 Qt SDK cache' | tee "$out/qt-build.log"
    else
        bash port/ci/build-qt-el8.sh 2>&1 | tee "$out/qt-build.log"
        tar -czf "$out/qt-sdk.tar.gz" -C /opt octool-qt
        (cd "$out" && sha256sum qt-sdk.tar.gz) > "$out/qt-sdk.sha256"
    fi
    cp /opt/octool-qt/licenses/config.summary "$out/qt-config-summary.txt"
}
case "$action" in
    kernel)
        if [ "$family" = rpm ]; then
            python3 port/ci/select-kernel-pair.py --output "$out/kernel-selection.json"
            rpm -qa | sort > "$out/packages-kernel-selected.txt"
        fi
        make -C port/hal
        make -C port/tests check hwio_smoke CAPTURE_RESULTS="$out/capture-results.json" 2>&1 | tee "$out/offline.log"
        python3 -m unittest discover -s port/tests -p 'test_*.py' -v
        # Execute only eight pinned original wrapper bodies, with a fake
        # userspace mailbox and no device access. Every target checks the
        # optional guard as well as the unchanged caller's known failures.
        mkdir -p "$out/legacy-guard"
        chown nobody "$out/legacy-guard"
        runuser -u nobody -- python3 analysis/tools/test-legacy-guard.py \
            --build-dir "$out/legacy-guard" --target "$target" \
            --output "$out/legacy-guard/results.json"
        bash port/ci/build-kernels.sh "$out/kernels"
        bash port/ci/fetch-kernel-images.sh "$root/build/vm-inputs"
        python3 port/tools/build_packages.py --format "$family" --module-only --output "$out/packages"
        install_packages
        verify_dkms
        # Same-version RPM reinstall exercises the DKMS safe-upgrade lock;
        # apt reinstall exercises prerm/configure against an existing build.
        # These module package checks must not depend on GUI availability.
        if [ "$family" = deb ]; then
            apt-get install -y --reinstall "$out/packages/"*.deb
        else
            dnf reinstall -y "$out/packages/"*.rpm
        fi
        verify_dkms
        if [ "$family" = deb ]; then
            apt-get purge -y octool-hwio-dkms
        else
            dnf remove -y octool-hwio-dkms
        fi
        test -z "$(dkms status -m octool-hwio -v "$(cat VERSION)")"
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
        cp build/gui/regression/gui-tests.txt "$out/gui-tests.txt"
        cp build/gui/regression/vf-fixture.png "$out/vf-fixture.png"
        python3 port/tools/check_elf.py "$out/gui-stage/opt/octool/bin/octool-real" > "$out/abi.json"
        python3 port/tools/check_elf.py "$out/gui-stage/opt/octool/bin/octool-hwio-helper" > "$out/helper-abi.json"
        tar -czf "$out/gui-stage.tar.gz" -C "$out/gui-stage" .
        ;;
    desktop)
        gcc -std=c11 -O2 -Wall -Wextra -Werror port/ci/window-probe.c -lX11 -o "$out/window-probe"
        g++ -std=c++11 -O2 -Wall -Wextra -Werror -pthread port/ci/helper-client.cpp \
            gui/platform/linux_helper.cpp gui/platform/linux_cpu.cpp gui/core/hardware.cpp \
            gui/core/helper_protocol.cpp -o "$out/helper-client"
        (cd /inputs && sha256sum -c qt-sdk.sha256)
        tar -xzf /inputs/qt-sdk.tar.gz -C /opt
        mkdir -p /tmp/octool-release-stage
        tar -xzf /inputs/gui-stage.tar.gz -C /tmp/octool-release-stage
        python3 port/tools/build_gui.py --build-dir "$root/build/gui-$target" --stage "$out/native-stage"
        cp "build/gui-$target/regression/gui-tests.txt" "$out/gui-tests.txt"
        cp "build/gui-$target/regression/vf-fixture.png" "$out/vf-fixture.png"
        python3 port/tools/check_elf.py --inspect "$out/native-stage/opt/octool/bin/octool-real" > "$out/native-abi.json"
        python3 port/tools/check_elf.py --inspect "$out/native-stage/opt/octool/bin/octool-hwio-helper" > "$out/native-helper-abi.json"
        make -C port/tests check 2>&1 | tee "$out/offline.log"
        python3 port/tools/build_packages.py --format "$family" --gui-stage /tmp/octool-release-stage --output "$out/packages"
        ;;
    runtime)
        # Install only the GUI, using declared runtime dependencies. Driver
        # recommendation is optional until its matching kernel toolchain exists.
        if [ "$family" = deb ]; then
            apt-get install -y --no-install-recommends "$out/packages/octool-"[0-9]*.deb
        else
            dnf install -y --setopt=install_weak_deps=False "$out/packages/octool-"[0-9]*.rpm
        fi
        for tool in gcc g++ make dkms; do
            if command -v "$tool" >/dev/null; then echo "unexpected build dependency: $tool" >&2; exit 1; fi
        done
        python3 - <<'PY'
from pathlib import Path
headers = list(Path('/usr/src').glob('*linux*')) + list(Path('/usr/src/kernels').glob('*'))
assert not headers, headers
PY
        printf '%s\n' 'GUI installed without compiler, make, DKMS or kernel headers' > "$out/minimal-runtime.txt"
        ldd /opt/octool/bin/octool-real | tee "$out/ldd-release.txt"
        ! grep -q 'not found' "$out/ldd-release.txt"
        ldd "$out/native-stage/opt/octool/bin/octool-real" | tee "$out/ldd-native.txt"
        ! grep -q 'not found' "$out/ldd-native.txt"
        ldd /opt/octool/bin/octool-hwio-helper | tee "$out/ldd-helper.txt"
        ! grep -q 'not found' "$out/ldd-helper.txt"
        display=$(python3 - "$target" <<'PY'
import json, sys
print(next(t for t in json.load(open('port/ci/targets.json')) if t['id'] == sys.argv[1])['display'])
PY
)
        useradd -m octool-smoke
        python3 port/ci/helper-smoke.py --output "$out/helper-smoke.json"
        mkdir -p "$out/smoke"
        chown octool-smoke:octool-smoke "$out/smoke"
        runuser -u octool-smoke -- env HOME=/home/octool-smoke bash -c \
            'cd "$HOME"; exec bash "$1/port/ci/headless-smoke.sh" "$2" --log /out/smoke/release.log' \
            _ "$root" "$display"
        runuser -u octool-smoke -- env HOME=/home/octool-smoke bash -c \
            'cd "$HOME"; exec bash "$1/port/ci/headless-smoke.sh" "$2" --binary /out/native-stage/opt/octool/bin/octool-real --log /out/smoke/native.log' \
            _ "$root" "$display"
        # Only now install the toolchain, headers and optional driver.
        bash port/ci/bootstrap.sh "$target" kernel 2>&1 | tee "$out/bootstrap-driver.log"
        install_packages
        verify_dkms
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
