#!/bin/bash
# Disposable cloud diagnostic; no OCTool registers or host services are used.
set -euo pipefail
dnf install -y --setopt=install_weak_deps=False dbus-daemon dbus-tools strace
cat /etc/nsswitch.conf > /out/nsswitch.conf
getent passwd dbus > /out/dbus-user.txt
getent group dbus > /out/dbus-group.txt
cat /proc/self/limits > /out/limits.txt
cat /usr/share/dbus-1/system.conf > /out/system.conf
mkdir -p /run/dbus
dbus-uuidgen --ensure
SYSTEMD_NSS_BYPASS_BUS=1 timeout 45 strace -ff -tt -o /out/dbus.trace dbus-daemon --system --nofork --nopidfile \
    --print-address=1 --print-pid=1 > /out/dbus.log 2>&1 &
probe_pid=$!
trap 'kill "$probe_pid" 2>/dev/null || true; wait "$probe_pid" 2>/dev/null || true' EXIT
sleep 1
result=0
timeout 10 strace -ff -tt -o /out/client.trace dbus-send --system --type=method_call \
    --print-reply --reply-timeout=2000 --dest=org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus.NameHasOwner string:org.freedesktop.DBus > /out/client.log 2>&1 || result=$?
cat /out/dbus.log /out/client.log
for trace in /out/dbus.trace* /out/client.trace*; do
    tail -n 70 "$trace"
done
exit "$result"
