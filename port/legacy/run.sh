#!/bin/bash
# Experimental unprivileged desktop smoke only; never install system links.
set -euo pipefail
[ "$(id -u)" != 0 ] || { echo 'legacy diagnostic must run unprivileged' >&2; exit 2; }
root=$(cd "$(dirname "$0")" && pwd)
for name in LD_PRELOAD LD_AUDIT LD_LIBRARY_PATH; do
    [ -z "${!name:-}" ] || { echo "unset $name before using the private runtime" >&2; exit 2; }
done
export QT_QPA_PLATFORM=xcb QT_XCB_GL_INTEGRATION=none QT_OPENGL=software
export QT_PLUGIN_PATH= QT_QPA_PLATFORM_PLUGIN_PATH=
export GCONV_PATH="$root/lib/gconv"
export FONTCONFIG_PATH="$root/etc/fonts" FONTCONFIG_FILE="$root/etc/fonts/fonts.conf"
# --library-path affects this loader invocation only. Shell children retain
# their native interpreter and libraries; no global LD_LIBRARY_PATH is set.
exec "$root/ld-linux-x86-64.so.2" --inhibit-cache --library-path "$root/lib" "$root/octool" "$@"
