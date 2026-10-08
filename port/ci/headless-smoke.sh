#!/bin/bash
set -euo pipefail
backend=${1:?xvfb or xwayland required}
shift
root=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(id -u)" != 0 ] || { echo 'GUI smoke must run as an unprivileged user' >&2; exit 2; }
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
xvfb_log=
cleanup() {
    local result=$?
    if [ -n "$xvfb_log" ]; then
        # Reopening /dev/stderr can fail after runuser for a root-owned pipe.
        # Reading our own file and writing the inherited descriptor does not.
        cat "$xvfb_log" >&2 || true
        rm -f -- "$xvfb_log" || true
    fi
    rmdir "$XDG_RUNTIME_DIR" 2>/dev/null || true
    return "$result"
}
trap cleanup EXIT
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
case "$backend" in
    xvfb)
        # The observer connects before/during Qt startup. A last-client reset
        # can briefly close the listener between the observer and the GUI.
        xvfb_log=$(mktemp "$XDG_RUNTIME_DIR/xvfb.XXXXXX.log")
        dbus-run-session -- xvfb-run -a -e "$xvfb_log" -s '-screen 0 1600x1000x24 -noreset' \
            python3 "$root/port/ci/gui-smoke.py" "$@"
        ;;
    xwayland)
        # xwfb-run creates a headless Wayland compositor and its Xwayland.
        # There is deliberately no Xvfb fallback for EL10.
        dbus-run-session -- xwfb-run -c mutter -s '\-geometry' -s 1600x1000 -- \
            python3 "$root/port/ci/gui-smoke.py" "$@"
        ;;
    *) echo 'unknown headless backend' >&2; exit 2;;
esac
