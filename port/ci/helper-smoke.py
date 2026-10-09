#!/usr/bin/env python3
"""Installed helper/policy and actual pkexec client in a fresh runtime image.

Root authorization and no-agent denial are automatic. A human session's password
dialog and real hardware register access remain hardware acceptance items.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import time
import xml.etree.ElementTree as ET

HELPER = Path('/opt/octool/bin/octool-hwio-helper')
POLICY = Path('/usr/share/polkit-1/actions/com.octool.hwio.policy')


def run(args, **kwargs):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=30, **kwargs)


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
            children.append(subprocess.Popen(['dbus-daemon', '--system', '--nofork', '--nopidfile'],
                                             stdout=log, stderr=subprocess.STDOUT))
        for _ in range(100):
            if Path('/run/dbus/system_bus_socket').exists(): break
            time.sleep(0.05)
        daemon = next(x for x in (Path('/usr/lib/polkit-1/polkitd'), Path('/usr/libexec/polkit-1/polkitd')) if x.is_file())
        log = args.output.with_suffix('.polkit.log').open('w'); logs.append(log)
        children.append(subprocess.Popen([str(daemon), '--no-debug'], stdout=log, stderr=subprocess.STDOUT))
        for _ in range(100):
            policy_check = run(['pkaction', '--action-id', 'com.octool.hwio', '--verbose'])
            if policy_check.returncode == 0 and 'auth_admin' in policy_check.stdout: break
            time.sleep(0.05)
        assert policy_check.returncode == 0 and 'auth_admin' in policy_check.stdout, policy_check.stderr
        args.output.with_suffix('.policy.txt').write_text(policy_check.stdout)
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
        report = {'policy': 'auth_admin', 'helper_mode': '0755', 'root_pkexec': json.loads(authorized.stdout),
                  'no_agent_denial': json.loads(denied.stdout), 'direct_unprivileged_exit': direct.returncode,
                  'unconnected_exit': unconnected.returncode, 'headless_diagnostics': info,
                  'restricted_cpu': selected, 'helper_exited_after_disconnect': True,
                  'interactive_password_dialog_tested': False, 'register_access_tested': False}
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try: child.wait(timeout=5)
                except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
        for log in logs: log.close()


if __name__ == '__main__':
    main()
