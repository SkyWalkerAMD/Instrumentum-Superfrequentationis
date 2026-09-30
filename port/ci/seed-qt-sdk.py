#!/usr/bin/env python3
"""Optionally reuse the project's already validated, immutable EL8 SDK artifact.

Recipe mismatch or an expired/unavailable artifact falls back to a source build.
An archive hash mismatch is an error. No GUI result is restored or reused.
"""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    seed = json.loads((ROOT / 'port/ci/qt-sdk-seed.json').read_text())
    for name, expected in seed['recipe'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            print('SDK recipe changed; build Qt from source')
            return
    download = ROOT / 'build/sdk-seed'
    result = subprocess.run(['gh', 'run', 'download', str(seed['run_id']),
                             '--repo', 'SkyWalkerAMD/Instrumentum-Superfrequentationis',
                             '--name', seed['artifact'], '--dir', str(download)])
    if result.returncode:
        print('Optional SDK seed unavailable; build Qt from source')
        return
    archive = download / 'qt-sdk.tar.gz'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != seed['archive_sha256']:
        raise RuntimeError('SDK seed SHA-256 mismatch')
    destination = ROOT / 'build/baseline'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(archive, destination / 'qt-sdk.tar.gz')
    (destination / 'qt-sdk.sha256').write_text(digest + '  qt-sdk.tar.gz\n', encoding='ascii')
    print('Validated SDK seed restored; GUI will compile from current source')


if __name__ == '__main__':
    main()
