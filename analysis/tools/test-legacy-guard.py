#!/usr/bin/env python3
"""Compile and natively run pinned wrapper bytes against a synthetic backend.

No full original ELF, kernel module, device node, or hardware is used. Python
supervises each child; the external timeout distinguishes an unchanged old
busy loop from the guard's own error exit. Standard library only, Python 3.8+.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'analysis/fixtures/legacy-mmio-wrappers.json'
FIXTURE_SHA = '4f96aa65f8f03f73e67a277b372e281c433226e5b2403fd26a5464078623d961'
CASES = {
    'success': None,
    'delayed_success': None,
    'no_completion': ('completion-timeout', 110),
    'encoded_enomem': ('driver-error', 12),
    'write_error': ('write-error', 5),
    'short_write': ('short-write', 5),
    'write_error_but_done': ('write-error', 5),
    'encoded_einval': ('driver-error', 22),
    'malformed_done': ('invalid-completion', 71),
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture():
    assert sha(FIXTURE) == FIXTURE_SHA, 'unexpected public instruction fixture'
    data = json.loads(FIXTURE.read_text(encoding='utf-8'))
    for f in data['functions']:
        assert hashlib.sha256(bytes.fromhex(f['code_hex'])).hexdigest() == f['code_sha256']
    return data


def prepare(directory, data):
    directory.mkdir(parents=True, exist_ok=True)
    functions = data['functions']
    header = ['/* Generated from pinned public fixture ' + FIXTURE_SHA + '. */']
    for i, f in enumerate(functions):
        values = ','.join('0x%02x' % b for b in bytes.fromhex(f['code_hex']))
        header.append('static const unsigned char legacy_code_%d[] = {%s};' % (i, values))
    header.append('static const struct legacy_function legacy_functions[] = {')
    for i, f in enumerate(functions):
        header.append('    {0x%x, 0x%x, sizeof(legacy_code_%d), %d, legacy_code_%d},' %
                      (f['address'], f['write_call'] + 5, i, f['opcode'], i))
    header.extend(['};', '#define LEGACY_FUNCTION_COUNT 8'])
    for key, macro in [('kernel_fd', 'FD'), ('kernel_address', 'MAILBOX'), ('user_request', 'REQUEST')]:
        header.append('#define LEGACY_%s 0x%xUL' % (macro, data['objects'][key]))
    (directory / 'legacy-mailbox-layout.h').write_text('\n'.join(header) + '\n', encoding='ascii')

    # This is a new tiny test ELF, not a modified OCTool executable. Every
    # original function byte is retained at its original virtual offset. Only
    # the write destination is a test trampoline to the ordinary versioned PLT.
    assembly = ['.section .legacy_wrappers,"ax",@progbits']
    start = functions[0]['address']
    for i, f in enumerate(functions):
        assembly.extend(['.org %d' % (f['address'] - start), '.globl legacy_%d' % i,
                         '.type legacy_%d,@function' % i, 'legacy_%d:' % i,
                         '.byte ' + ','.join('0x%02x' % b for b in bytes.fromhex(f['code_hex'])),
                         '.size legacy_%d,.-legacy_%d' % (i, i)])
    assembly.extend(['.section .legacy_write,"ax",@progbits',
                     '.symver original_write_import,write@GLIBC_2.2.5',
                     'jmp original_write_import@PLT',
                     '.section .legacy_state,"aw",@nobits',
                     '.globl legacy_fd, legacy_mailbox, legacy_request',
                     'legacy_fd: .zero 8', 'legacy_mailbox: .zero 8',
                     'legacy_request: .zero 96', '.section .note.GNU-stack,"",@progbits'])
    (directory / 'wrappers.S').write_text('\n'.join(assembly) + '\n', encoding='ascii')
    (directory / 'backend.map').write_text('GLIBC_2.2.5 { global: write; };\n', encoding='ascii')


def section_bytes(path, name):
    raw = path.read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    off = struct.unpack_from('<Q', raw, 40)[0]
    stride, count, strings = struct.unpack_from('<HHH', raw, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, off + i * stride) for i in range(count)]
    s = headers[strings]
    names = raw[s[4]:s[4] + s[5]]
    for h in headers:
        if names[h[0]:].split(b'\0', 1)[0].decode() == name:
            return h[3], raw[h[4]:h[4] + h[5]]
    raise AssertionError('missing ELF section ' + name)


def verify_bodies(binary, data):
    elf = binary.read_bytes()
    elf_type = struct.unpack_from('<H', elf, 16)[0]
    phoff = struct.unpack_from('<Q', elf, 32)[0]
    stride, count = struct.unpack_from('<HH', elf, 54)
    stack = []
    for i in range(count):
        p = struct.unpack_from('<IIQQQQQQ', elf, phoff + i * stride)
        if p[0] == 1:  # PT_LOAD
            assert (p[1] & 3) != 3, 'test ELF must not contain a writable executable segment'
            if elf_type == 2:  # ET_EXEC: fixed addresses, unlike a relocated PIE
                assert p[3] >= 0x10000, 'test ELF would request a null/low-page mapping'
        if p[0] == 0x6474e551:  # PT_GNU_STACK
            stack.append(p[1])
    assert stack == [6], 'test ELF must declare a non-executable stack'
    start, raw = section_bytes(binary, '.legacy_wrappers')
    for f in data['functions']:
        expected = bytes.fromhex(f['code_hex'])
        off = f['address'] - start
        assert raw[off:off + len(expected)] == expected, (binary, f['symbol'])


def build(directory, data, external_guard):
    cc = os.environ.get('CC', 'gcc')
    flags = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    guard = external_guard or directory / 'octool-mailbox-guard.so'
    if external_guard is None:
        subprocess.check_call(flags + ['-fPIC', '-shared', '-I' + str(directory),
            str(ROOT / 'port/legacy/mailbox-guard.c'), '-Wl,-z,relro,-z,now,-z,noexecstack',
            '-ldl', '-lpthread', '-o', str(guard)])
    subprocess.check_call(flags + ['-fPIC', '-shared', str(ROOT / 'analysis/tests/legacy-guard-backend.c'),
        '-Wl,--version-script=' + str(directory / 'backend.map'), '-lpthread', '-ldl',
        '-Wl,-z,noexecstack', '-o', str(directory / 'libfake-mailbox.so')])
    for kind, mode in [('pie', '-pie'), ('exec', '-no-pie')]:
        binary = directory / ('caller-' + kind)
        # binutils 2.30's default ET_EXEC text starts at 0x400000. Placing
        # original lower-address sections before it otherwise creates a LOAD
        # at address zero. Choose a nonzero header/text segment BELOW all of
        # our pinned sections; do not change mmap_min_addr or the old bytes.
        layout = ['-Wl,-z,max-page-size=0x1000']
        if kind == 'exec':
            layout += ['-Wl,-Ttext-segment=0x10000']
        subprocess.check_call(flags + layout + ['-fPIE', mode,
            str(ROOT / 'analysis/tests/legacy-guard-harness.c'), str(directory / 'wrappers.S'),
            '-Wl,--section-start=.legacy_wrappers=0x%x' % data['functions'][0]['address'],
            '-Wl,--section-start=.legacy_write=0x%x' % data['functions'][0]['write_plt'],
            '-Wl,--section-start=.legacy_state=0x%x' % data['objects']['kernel_fd'],
            '-L' + str(directory), '-lfake-mailbox', '-Wl,-rpath,$ORIGIN',
            '-Wl,-z,relro,-z,now,-z,noexecstack', '-o', str(binary)])
        verify_bodies(binary, data)
    versions = subprocess.check_output(['readelf', '--version-info', str(guard)], text=True)
    requirements = sorted(set(re.findall(r'GLIBC_([0-9.]+)', versions)),
                          key=lambda v: tuple(map(int, v.split('.'))))
    artifacts = ['caller-pie', 'caller-exec', 'libfake-mailbox.so']
    manifest = {
        'fixture_sha256': FIXTURE_SHA, 'guard_sha256': sha(guard),
        'guard_glibc_versions': requirements,
        'compiler': subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
        'artifacts': {name: sha(directory / name) for name in artifacts},
        'sources': {name: sha(ROOT / name) for name in (
            'port/legacy/mailbox-guard.c', 'analysis/tests/legacy-guard-backend.c',
            'analysis/tests/legacy-guard-harness.c', 'analysis/tools/test-legacy-guard.py')},
    }
    (directory / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return guard


def child(command, guard, timeout):
    env = dict(os.environ)
    for name in ('LD_PRELOAD', 'LD_AUDIT', 'LD_LIBRARY_PATH'):
        env.pop(name, None)
    if guard:
        env['LD_PRELOAD'] = str(guard)
    started = time.monotonic()
    try:
        p = subprocess.run(command, env=env, capture_output=True, timeout=timeout)
        status, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as exc:
        status, out, err = 'external-timeout', exc.stdout or b'', exc.stderr or b''
    return {'exit': status, 'seconds': round(time.monotonic() - started, 6),
            'stdout': out.decode('utf-8'), 'stderr': err.decode('utf-8')}


def expected_request(f):
    words = [0xa0a0000000000000 + i for i in range(12)]
    words[0:3] = [f['opcode'], 71, 0x12345000]
    if f['write']:
        words[3] = 0x1122334455667788
        if f['width'] in (1, 2):
            words[3] &= (1 << (8 * f['width'])) - 1
    return struct.pack('<12Q', *words).hex()


def check(row, f, case, guarded):
    events = [json.loads(line) for line in row['stdout'].splitlines()]
    backend = [e for e in events if e['event'] == 'backend']
    assert len(backend) == 1, row
    assert backend[0] == {'event': 'backend', 'fd': 600, 'size': 96,
                          'before': '0000000000000000', 'request': expected_request(f)}, row
    expected = CASES[case]
    returns = expected is None or (not guarded and case == 'write_error_but_done')
    if returns:
        assert row['exit'] == 0 and row['stderr'] == '', row
        answer, = [e for e in events if e['event'] == 'returned']
        assert answer['done'] == '0000000000000001', row
        assert answer['result'] == 'fedcba9876543210', row
        if not f['write']:
            assert answer['value'] == 'fedcba9876543210', row
    elif guarded:
        assert row['exit'] == 74, row
        error = json.loads(row['stderr'])
        assert error['event'] == 'octool-mailbox-guard', row
        assert (error['reason'], error['errno']) == expected, row
        assert error['opcode'] == f['opcode'] and error['fd'] == 600, row
        assert len(events) == 1, row  # No old-function return or invented reading.
    else:
        assert row['exit'] == 'external-timeout' and not row['stderr'], row
        assert len(events) == 1, row


def run(directory, guard, data, output, target):
    assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
    build_info = json.loads((directory / 'build.json').read_text(encoding='utf-8'))
    assert build_info['fixture_sha256'] == FIXTURE_SHA
    assert sha(guard) == build_info['guard_sha256']
    for name, digest in build_info['artifacts'].items():
        assert sha(directory / name) == digest, name
    for kind in ('pie', 'exec'):
        verify_bodies(directory / ('caller-' + kind), data)
    report = {'scope': 'native original wrapper bytes + synthetic backend; no full GUI/hardware',
              'target': target, 'platform': platform.platform(), 'python': platform.python_version(),
              'libc': platform.libc_ver(), 'uid': os.getuid(), 'fixture_sha256': FIXTURE_SHA,
              'build': build_info, 'observations': [], 'passed': False}
    if Path('/etc/os-release').exists():
        report['os_release'] = Path('/etc/os-release').read_text()
    loader = '/lib64/ld-linux-x86-64.so.2'
    assert Path(loader).exists()
    try:
        modes = [('unguarded-pie', [str(directory / 'caller-pie')], False),
                 ('unguarded-exec', [str(directory / 'caller-exec')], False),
                 ('guarded-pie', [str(directory / 'caller-pie')], True),
                 ('guarded-exec', [str(directory / 'caller-exec')], True),
                 ('guarded-loader', [loader, str(directory / 'caller-pie')], True)]
        for mode, prefix, guarded in modes:
            for i, f in enumerate(data['functions']):
                cases = ('success', 'delayed_success') if mode == 'unguarded-exec' else CASES
                for case in cases:
                    row = child(prefix + [str(i), case], guard if guarded else None,
                                4 if guarded else 0.5)
                    row.update(mode=mode, function=f['symbol'], case=case)
                    report['observations'].append(row)
                    check(row, f, case, guarded)
            if guarded:
                row = child(prefix + ['foreign', 'write_error'], guard, 4)
                row.update(mode=mode, function='unrelated write caller', case='forward-error')
                report['observations'].append(row)
                assert row['exit'] == 0 and row['stderr'] == '', row
                assert json.loads(row['stdout'].splitlines()[-1]) == {
                    'event': 'foreign', 'wrote': -1, 'errno': 5}, row
            print(mode + ' passed', flush=True)
        report['passed'] = True
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'target': target, 'passed': True, 'observations': len(report['observations']),
                      'guard_sha256': sha(guard)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--guard', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--run-only', action='store_true')
    parser.add_argument('--target', default='local-linux')
    parser.add_argument('--max-glibc')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = fixture()
    directory = args.build_dir.resolve()
    guard = args.guard.resolve() if args.guard else None
    if not args.run_only:
        prepare(directory, data)
        if args.prepare_only:
            return
        guard = build(directory, data, guard)
    guard = guard or directory / 'octool-mailbox-guard.so'
    if args.max_glibc:
        info = json.loads((directory / 'build.json').read_text())
        limit = tuple(map(int, args.max_glibc.split('.')))
        assert all(tuple(map(int, v.split('.'))) <= limit for v in info['guard_glibc_versions']), info
    if not args.build_only:
        run(directory, guard, data, args.output or directory / 'results.json', args.target)


if __name__ == '__main__':
    main()
