#!/usr/bin/env python3
"""Observe the unchanged GUI without privileges, device stubs or input events."""
import hashlib
import json
import os
import re
import signal
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path


def capture(window, output):
    ppm = output.with_suffix('.ppm')
    subprocess.check_call(['/usr/local/bin/capture-window', window, str(ppm)])
    data = ppm.read_bytes().split(b'\n', 3)
    assert data[0] == b'P6' and data[2] == b'255'
    width, height = map(int, data[1].split())
    assert len(data[3]) == width * height * 3
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind+payload) & 0xffffffff)
    scanlines = b''.join(b'\0' + data[3][y*width*3:(y+1)*width*3] for y in range(height))
    output.write_bytes(b'\x89PNG\r\n\x1a\n' +
                       chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                       chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))
    ppm.unlink()


def main():
    if os.geteuid() == 0:
        raise RuntimeError('unprivileged smoke required')
    runtime, out = Path('/opt/legacy'), Path('/out')
    expected = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
    assert hashlib.sha256((runtime/'octool').read_bytes()).hexdigest() == expected
    target = sys.argv[1]
    if '--session' not in sys.argv:
        Path(os.environ['XDG_RUNTIME_DIR']).mkdir(mode=0o700, exist_ok=True)
        display = (['xwfb-run', '-c', 'mutter', '-s', r'\-geometry', '-s', '1600x1000', '--']
                   if target == 'el10' else ['xvfb-run', '-a', '-s', '-screen 0 1600x1000x24'])
        return subprocess.call(['dbus-run-session', '--'] + display +
                               [sys.executable, __file__, target, '--session'])
    result = {'target': target, 'execution_mode': 'native', 'original_sha256': expected,
              'os_release': Path('/etc/os-release').read_text(),
              'host_kernel': os.uname().release, 'uid': os.geteuid(),
              'display': 'Mutter/Xwayland' if target == 'el10' else 'Xvfb',
              'hardware_devices_passed_to_container': False,
              'hardware_register_tests_run': False, 'status': 'pending'}
    result['cpuid'] = json.loads(subprocess.check_output(['/usr/local/bin/cpu-id'], universal_newlines=True))
    result['sysfs_identity'] = {}
    for path in ('/sys/bus/pci/devices/0000:00:00.0/vendor',
                 '/sys/bus/pci/devices/0000:00:00.0/device',
                 '/sys/devices/virtual/dmi/id/board_vendor',
                 '/sys/devices/virtual/dmi/id/board_name'):
        try:
            value = {'value': Path(path).read_text().strip()}
        except OSError as error:
            value = {'errno': error.errno, 'error': error.strerror}
        result['sysfs_identity'][path] = value
    native = subprocess.run(['ldd', str(runtime/'octool')], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, universal_newlines=True)
    (out/'native-loader.txt').write_text(native.stdout)
    result['native_loader_returncode'] = native.returncode
    # glibc's ldd may return zero while printing unresolved versions/SONAMEs.
    result['native_loader_problems'] = [line.strip() for line in native.stdout.splitlines() if 'not found' in line]
    result['native_packages'] = subprocess.check_output(['rpm', '-q', 'glibc', 'libstdc++'], universal_newlines=True).splitlines()
    loader = [str(runtime/'ld-linux-x86-64.so.2'), '--inhibit-cache',
              '--library-path', str(runtime/'lib'), '--list', str(runtime/'octool')]
    trace = subprocess.run(loader, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    (out/'loader.txt').write_text(trace.stdout)
    result['loader_returncode'] = trace.returncode
    assert trace.returncode == 0, trace.stdout
    external = [line for line in trace.stdout.splitlines() if '=> /' in line and '=> /opt/legacy/' not in line]
    assert not external, 'host dependencies mixed with private libc: '+repr(external)
    with (out/'gui.log').open('w') as log:
        proc = subprocess.Popen([str(runtime/'run.sh')], stdout=log, stderr=subprocess.STDOUT,
                                start_new_session=True)
        seen, windows, stable = None, [], False
        try:
            started = time.monotonic()
            while time.monotonic() - started < 40:
                if proc.poll() is not None:
                    result['status'], result['exit_code'] = 'exited', proc.returncode
                    break
                lines = subprocess.check_output(['/usr/local/bin/window-probe', str(proc.pid)],
                                                universal_newlines=True).splitlines()
                windows = [{'id': line.split('\t', 1)[0],
                            'title': bytes.fromhex(line.split('\t', 1)[1]).decode('utf-8')}
                           for line in lines]
                if windows:
                    if seen is None:
                        seen = time.monotonic()
                    if time.monotonic() - seen >= 5:
                        stable = True
                        result['status'] = ('main-window' if any(w['title'] == 'Work Tool' for w in windows)
                                            else 'dialog-only')
                        break
                else:
                    seen = None
                time.sleep(0.2)
            else:
                result['status'] = 'no-window'
            result['windows'], result['pid'] = windows, proc.pid
            for number, window in enumerate(windows):
                capture(window['id'], out/'window-{}.png'.format(number))
            if proc.poll() is None:
                maps = Path('/proc/{}/maps'.format(proc.pid)).read_text()
                (out/'maps.txt').write_text(maps)
                result['proc_self_exe'] = os.readlink('/proc/{}/exe'.format(proc.pid))
                result['foreign_libraries'] = sorted(set(
                    p for p in re.findall(r'(/\S+)', maps)
                    if '.so' in p and not p.startswith('/opt/legacy/')))
                if result['foreign_libraries']:
                    raise RuntimeError('runtime loaded host libraries: '+repr(result['foreign_libraries']))
                wrong = subprocess.check_output(['/usr/local/bin/window-probe', str(os.getpid())],
                                                universal_newlines=True)
                assert not wrong.strip(), 'window ownership negative control failed'
            print(json.dumps(result, indent=2))
            # An unsupported-hardware dialog demonstrates Qt/loader success,
            # but must NOT make this GUI acceptance gate green.
            return 0 if stable and result['status'] == 'main-window' else 1
        finally:
            (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()


if __name__ == '__main__':
    sys.exit(main())
