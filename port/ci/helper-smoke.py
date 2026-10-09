#!/usr/bin/env python3
"""Installed helper/policy and actual pkexec client in a fresh runtime image.

Root authorization and no-agent denial are automatic. A human session's password
dialog and real hardware register access remain hardware acceptance items.
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import re
import stat
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

HELPER = Path('/opt/octool/bin/octool-hwio-helper')
POLICY = Path('/usr/share/polkit-1/actions/com.octool.hwio.policy')


def run(args, **kwargs):
    kwargs.setdefault('timeout', 30)
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          **kwargs)


def wait_for_name(name):
    # Query only the bus itself: do not trigger service auto-activation while
    # the explicitly started daemon is still registering its name.
    deadline = time.monotonic() + 20
    while True:
        result = run(['dbus-send', '--system', '--type=method_call', '--print-reply', '--reply-timeout=1000',
                      '--dest=org.freedesktop.DBus', '/org/freedesktop/DBus',
                      'org.freedesktop.DBus.NameHasOwner', 'string:' + name], timeout=3)
        if result.returncode == 0 and re.search(r'^\s*boolean true\s*$', result.stdout, re.MULTILINE):
            return
        assert time.monotonic() < deadline, (name, result)
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert os.geteuid() == 0 and os.environ.get('OCTOOL_DISPOSABLE_CONTAINER') == '1'
    metadata = HELPER.stat()
    assert metadata.st_uid == 0 and stat.S_IMODE(metadata.st_mode) == 0o755
    policy = ET.parse(POLICY).getroot()
    actions = policy.findall('action')
    assert len(actions) == 1 and actions[0].get('id') == 'com.octool.hwio'
    action = actions[0]
    assert [action.findtext('defaults/' + x) for x in ('allow_any', 'allow_inactive', 'allow_active')] == ['auth_admin'] * 3
    assert {x.get('key'): x.text for x in action.findall('annotate')} == {'org.freedesktop.policykit.exec.path': str(HELPER)}
    children = []
    logs = []
    try:
        # Container only; never change the host bus, policies or services.
        Path('/run/dbus').mkdir(exist_ok=True)
        subprocess.run(['dbus-uuidgen', '--ensure'], check=True)
        if not Path('/run/dbus/system_bus_socket').exists():
            log = args.output.with_suffix('.dbus.log').open('w'); logs.append(log)
            # Match systemd's bus-service startup environment. Older EL8 NSS
            # group enumeration otherwise connects to this not-yet-serving
            # bus while dbus-daemon is dropping privileges, deadlocking itself.
            bus_env = dict(os.environ, SYSTEMD_NSS_BYPASS_BUS='1')
            children.append(subprocess.Popen(['dbus-daemon', '--system', '--nofork', '--nopidfile',
                                              '--print-address=1', '--print-pid=1'],
                                             stdout=log, stderr=subprocess.STDOUT, env=bus_env))
        wait_for_name('org.freedesktop.DBus')
        service = configparser.ConfigParser(interpolation=None)
        service.read('/usr/share/dbus-1/system-services/org.freedesktop.PolicyKit1.service')
        daemon_command = shlex.split(service['D-BUS Service']['Exec'])
        daemon = Path(daemon_command[0])
        assert daemon.is_absolute() and daemon.name == 'polkitd' and daemon.is_file(), daemon_command
        daemon_metadata = daemon.stat()
        assert daemon_metadata.st_uid == 0 and not daemon_metadata.st_mode & 0o022
        # Keep startup diagnostics in CI; production continues to use the
        # distribution's installed service unchanged.
        daemon_command = [arg for arg in daemon_command if arg != '--no-debug']
        args.output.with_suffix('.service.json').write_text(json.dumps(daemon_command) + '\n')
        log = args.output.with_suffix('.polkit.log').open('w'); logs.append(log)
        children.append(subprocess.Popen(daemon_command, stdout=log, stderr=subprocess.STDOUT))
        wait_for_name('org.freedesktop.PolicyKit1')
        policy_version = run(['pkaction', '--version'])
        assert policy_version.returncode == 0, policy_version
        # Upstream 0.105 leaves ret=1 after a successful action enumeration.
        # Accept that historical exit code only for this exact version, and
        # still require every loaded authorization value and the helper path.
        accepted_exits = (0, 1) if policy_version.stdout.strip() == 'pkaction version 0.105' else (0,)
        expected_policy = [r'^com\.octool\.hwio:$'] + [
            r'^\s*implicit ' + context + r':\s+auth_admin\s*$'
            for context in ('any', 'inactive', 'active')]
        expected_policy.append(r'^\s*annotation:\s+org\.freedesktop\.policykit\.exec\.path\s+->\s+'
                               + re.escape(str(HELPER)) + r'\s*$')
        for _ in range(100):
            policy_check = run(['pkaction', '--action-id', 'com.octool.hwio', '--verbose'])
            policy_loaded = policy_check.returncode in accepted_exits and all(
                re.search(pattern, policy_check.stdout, re.MULTILINE) for pattern in expected_policy)
            if policy_loaded: break
            time.sleep(0.05)
        args.output.with_suffix('.policy.txt').write_text(policy_check.stdout)
        assert policy_loaded, (policy_version, policy_check)
        direct = run(['runuser', '-u', 'octool-smoke', '--', str(HELPER)])
        assert direct.returncode == 77, direct
        unconnected = run([str(HELPER)])
        assert unconnected.returncode == 64, unconnected
        authorized = run(['/out/helper-client'])
        assert authorized.returncode == 0, authorized
        denied = run(['runuser', '-u', 'octool-smoke', '--', '/out/helper-client', '--expect-denied'])
        assert denied.returncode == 0, denied
        # No DISPLAY, no GUI initialization, no device or privileged requests.
        env = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
        diagnostic = run(['runuser', '-u', 'octool-smoke', '--', '/usr/bin/octool', '--diagnose'], env=env)
        assert diagnostic.returncode == 0, diagnostic
        info = json.loads(diagnostic.stdout)
        assert info['helper_installed'] and info['affinity_error'] == 0
        assert info['allowed_cpus'] == sorted(os.sched_getaffinity(0))
        assert info['register_access_tested'] is False
        selected = max(info['allowed_cpus'])
        limited = run(['taskset', '-c', str(selected), 'runuser', '-u', 'octool-smoke', '--',
                       '/usr/bin/octool', '--diagnose'], env=env)
        assert limited.returncode == 0, limited
        assert json.loads(limited.stdout)['allowed_cpus'] == [selected]
        # All clients have closed their private stream; no privileged helper
        # should remain. Examine exact executable paths, never kill by name.
        for _ in range(100):
            remaining = []
            for entry in Path('/proc').glob('[0-9]*/exe'):
                try:
                    if entry.resolve() == HELPER: remaining.append(str(entry))
                except (FileNotFoundError, PermissionError): pass
            if not remaining: break
            time.sleep(0.05)
        assert not remaining, remaining
        report = {'policy': 'auth_admin', 'helper_mode': '0755', 'polkit_service_command': daemon_command,
                  'bus_startup_nss_direct_lookup': True,
                  'pkaction_version': policy_version.stdout.strip(), 'pkaction_exit': policy_check.returncode,
                  'root_pkexec': json.loads(authorized.stdout),
                  'no_agent_denial': json.loads(denied.stdout), 'direct_unprivileged_exit': direct.returncode,
                  'unconnected_exit': unconnected.returncode, 'headless_diagnostics': info,
                  'restricted_cpu': selected, 'helper_exited_after_disconnect': True,
                  'interactive_password_dialog_tested': False, 'register_access_tested': False}
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    except Exception:
        processes = []
        for child in children:
            state = {'pid': child.pid, 'exit': child.poll(), 'args': child.args}
            for field in ('status', 'wchan'):
                path = Path('/proc') / str(child.pid) / field
                try: state[field] = path.read_text()
                except OSError as error: state[field] = str(error)
            processes.append(state)
        args.output.with_suffix('.failure.json').write_text(json.dumps(processes, indent=2) + '\n')
        for log in logs:
            log.flush()
            print(Path(log.name).read_text(), flush=True)
        print(json.dumps(processes, indent=2), flush=True)
        raise
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try: child.wait(timeout=5)
                except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
        for log in logs: log.close()


if __name__ == '__main__':
    main()
