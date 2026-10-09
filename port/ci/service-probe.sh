#!/bin/bash
# Disposable cloud diagnostic; no OCTool registers or host services are used.
set -euo pipefail
dnf install -y --setopt=install_weak_deps=False dbus-daemon dbus-tools strace polkit shadow-utils util-linux
cat /etc/nsswitch.conf > /out/nsswitch.conf
getent passwd dbus > /out/dbus-user.txt
getent group dbus > /out/dbus-group.txt
cat /proc/self/limits > /out/limits.txt
cat /usr/share/dbus-1/system.conf > /out/system.conf
/usr/libexec/platform-python /src/port/ci/container-nss.py --output /out/runtime-nss.json
mkdir -p /run/dbus
dbus-uuidgen --ensure
useradd --create-home octool-smoke
SYSTEMD_NSS_BYPASS_BUS=1 timeout 120 strace -ff -tt -o /out/dbus.trace dbus-daemon --system --nofork --nopidfile \
    --print-address=1 --print-pid=1 > /out/dbus.log 2>&1 &
probe_pid=$!
trap 'kill "$probe_pid" 2>/dev/null || true; wait "$probe_pid" 2>/dev/null || true' EXIT
result=0
/usr/libexec/platform-python - <<'PY' || result=$?
import configparser
import json
from pathlib import Path
import re
import shlex
import subprocess
import time


def run(command):
    result = subprocess.run(command, universal_newlines=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=10)
    with Path('/out/client.log').open('a') as log:
        log.write(json.dumps({'command': command, 'exit': result.returncode,
                              'stdout': result.stdout, 'stderr': result.stderr}) + '\n')
    return result


def wait_for_name(name):
    deadline = time.monotonic() + 20
    while True:
        result = run(['dbus-send', '--system', '--type=method_call', '--print-reply',
                      '--reply-timeout=1000', '--dest=org.freedesktop.DBus',
                      '/org/freedesktop/DBus', 'org.freedesktop.DBus.NameHasOwner', 'string:' + name])
        if result.returncode == 0 and re.search(r'^\s*boolean true\s*$', result.stdout, re.MULTILINE):
            return
        assert time.monotonic() < deadline, (name, result)
        time.sleep(0.05)


wait_for_name('org.freedesktop.DBus')
service = configparser.ConfigParser(interpolation=None)
service.read('/usr/share/dbus-1/system-services/org.freedesktop.PolicyKit1.service')
command = [arg for arg in shlex.split(service['D-BUS Service']['Exec']) if arg != '--no-debug']
daemon = Path(command[0])
assert daemon.is_absolute() and daemon.name == 'polkitd' and daemon.is_file()
with Path('/out/polkit.log').open('w') as log:
    child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        wait_for_name('org.freedesktop.PolicyKit1')
        policy = run(['pkaction', '--action-id', 'org.freedesktop.policykit.exec', '--verbose'])
        assert policy.returncode == 0 and 'org.freedesktop.policykit.exec:' in policy.stdout, policy
        # Never trace setuid pkexec: ptrace changes its privilege semantics.
        authorized = run(['/usr/bin/pkexec', '--disable-internal-agent', '/usr/bin/true'])
        assert authorized.returncode == 0, authorized
        denied = run(['runuser', '-u', 'octool-smoke', '--', '/usr/bin/pkexec',
                      '--disable-internal-agent', '/usr/bin/true'])
        assert denied.returncode == 127, denied
        Path('/out/service-probe.json').write_text(json.dumps({
            'system_bus_ready': True, 'polkit_ready': True, 'root_pkexec_exit': authorized.returncode,
            'unprivileged_no_agent_exit': denied.returncode, 'polkit_command': command,
            'scope': 'disposable EL8 local accounts; harmless /usr/bin/true, no OCTool hardware access'
        }, indent=2) + '\n')
    finally:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)
PY
cat /out/dbus.log /out/client.log /out/polkit.log
for trace in /out/dbus.trace*; do
    tail -n 70 "$trace"
done
exit "$result"
