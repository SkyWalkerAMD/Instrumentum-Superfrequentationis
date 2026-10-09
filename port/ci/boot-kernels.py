#!/usr/bin/env python3
"""Boot actual target kernels under QEMU TCG; never load modules on the host.

The tiny newc initramfs follows the Linux early-userspace buffer format. It
contains only static BusyBox, our probe and the corresponding unsigned module.
This is not a Secure Boot or physical motherboard acceptance test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def initramfs(busybox, probe, module):
    archive = bytearray()
    inode = 0

    def add(name, mode, data=b'', major=0, minor=0):
        nonlocal inode
        inode += 1
        encoded = name.encode() + b'\0'
        fields = (inode, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(encoded), 0)
        archive.extend(('070701' + ''.join('{:08x}'.format(n) for n in fields)).encode())
        archive.extend(encoded)
        archive.extend(b'\0' * (-len(archive) % 4))
        archive.extend(data)
        archive.extend(b'\0' * (-len(archive) % 4))

    for name in ('bin', 'proc', 'sys', 'dev'):
        add(name, stat.S_IFDIR | 0o755)
    add('dev/console', stat.S_IFCHR | 0o600, major=5, minor=1)
    add('bin/busybox', stat.S_IFREG | 0o755, busybox.read_bytes())
    add('probe', stat.S_IFREG | 0o755, probe.read_bytes())
    add('octool_hwio.ko', stat.S_IFREG | 0o600, module.read_bytes())
    # A failing PID 1 must print a marker and power off instead of timing out.
    add('init', stat.S_IFREG | 0o755, b'''#!/bin/busybox sh
export PATH=/bin
fail() { echo OCTOOL_GUEST_FAILED; /bin/busybox dmesg; /bin/busybox poweroff -f; }
/bin/busybox mount -t proc proc /proc || fail
/bin/busybox mount -t sysfs sysfs /sys || fail
/bin/busybox mount -t devtmpfs devtmpfs /dev || fail
/bin/busybox insmod /octool_hwio.ko || fail
/probe || fail
/bin/busybox rmmod octool_hwio || fail
test ! -e /dev/mydev && test ! -d /sys/module/octool_hwio || fail
echo OCTOOL_GUEST_OK
/bin/busybox poweroff -f
''')
    add('TRAILER!!!', 0)
    return bytes(archive)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--inputs', required=True, type=Path)
    parser.add_argument('--modules', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    workspace = ROOT / 'build/guest-boot'
    workspace.mkdir(parents=True, exist_ok=True)
    probe = workspace / 'probe'
    subprocess.run(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-pthread',
                    str(ROOT / 'port/ci/guest-probe.c'), str(ROOT / 'port/hal/octool_hwio.c'),
                    '-o', str(probe)], check=True)
    busybox = Path(shutil.which('busybox'))
    qemu = shutil.which('qemu-system-x86_64')
    assert qemu
    images = sorted(args.inputs.glob('*/vmlinuz'))
    assert images, 'no target kernels downloaded'
    assert len(images) == len(list(args.modules.glob('*/octool_hwio.ko'))), 'missing target image'
    reports = []
    for kernel in images:
        release = kernel.parent.name
        assert re.fullmatch(r'[A-Za-z0-9_.+\-]+', release), release
        module = args.modules / release / 'octool_hwio.ko'
        ramdisk = workspace / (release + '.cpio')
        ramdisk.write_bytes(initramfs(busybox, probe, module))
        command = [qemu, '-machine', 'q35', '-accel', 'tcg', '-cpu', 'max', '-smp', '2', '-m', '512',
                   '-nodefaults', '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
                   '-kernel', str(kernel), '-initrd', str(ramdisk), '-append',
                   'console=ttyS0 rdinit=/init panic=1 oops=panic loglevel=5']
        with (args.output / (release + '.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)
        console = (args.output / (release + '.log')).read_text(errors='replace')
        assert 'OCTOOL_GUEST_OK' in console and 'OCTOOL_GUEST_FAILED' not in console, console[-6000:]
        assert not re.search(r'Kernel panic|BUG:|Oops:', console), console[-6000:]
        match = re.search(r'^OCTOOL_GUEST_PROBE=(\{[^\r\n]+\})', console, re.M)
        assert match, console[-6000:]
        report = json.loads(match.group(1))
        assert report['kernel'] == release, report
        report.update({'load_unload': True, 'emulator': 'QEMU TCG', 'secure_boot_tested': False,
                       'kernel_sha256': hashlib.sha256(kernel.read_bytes()).hexdigest(),
                       'module_sha256': hashlib.sha256(module.read_bytes()).hexdigest()})
        reports.append(report)
        (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')
        print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
