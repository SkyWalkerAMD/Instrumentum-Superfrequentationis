#!/usr/bin/env python3
"""Choose an actually bootable EL kernel pair in a disposable CI container.

AppStream kernel-devel and BaseOS kernel-core can be published at different
times. Choose the newest common version instead of testing mismatched images,
or silently skipping the boot gate. This never runs on an installed user OS.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def command(args):
    return subprocess.check_output(args, text=True).splitlines()


def versions(package):
    values = command(['dnf', '-q', 'repoquery', '--available', '--arch=x86_64',
                      '--qf', '%{version}-%{release}.%{arch}', package])
    return {x.strip() for x in values if re.fullmatch(r'[0-9][A-Za-z0-9_.+\-]*\.x86_64', x.strip())}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert os.geteuid() == 0 and os.environ.get('OCTOOL_DISPOSABLE_CONTAINER') == '1'
    headers, images = versions('kernel-devel'), versions('kernel-core')
    common = headers & images
    assert common, 'no matching kernel-devel/kernel-core versions in the enabled official repositories'
    # These are conventional numeric kernel-release names, not generic RPM EVRs.
    ordered = subprocess.check_output(['sort', '-V'], input='\n'.join(common)+'\n', text=True).splitlines()
    selected = ordered[-1]
    installed = command(['rpm', '-q', 'kernel-devel', '--qf', '%{VERSION}-%{RELEASE}.%{ARCH}\n'])
    subprocess.run(['dnf', 'install', '-y', 'kernel-devel-'+selected], check=True)
    # Install the replacement first. Remove only the other explicitly named
    # header packages in this fresh CI container; keep all build dependencies.
    current = command(['rpm', '-q', 'kernel-devel', '--qf', '%{VERSION}-%{RELEASE}.%{ARCH}\n'])
    other = ['kernel-devel-'+x for x in current if x != selected]
    if other:
        subprocess.run(['dnf', 'remove', '-y', '--setopt=clean_requirements_on_remove=False', *other], check=True)
    tree = Path('/usr/src/kernels')/selected
    assert (tree/'Makefile').is_file() and (tree/'Module.symvers').is_file()
    directory = Path('/lib/modules')/selected
    directory.mkdir(parents=True, exist_ok=True)
    link = directory/'build'
    if link.is_symlink() or link.exists():
        assert link.resolve() == tree.resolve(), link
    else:
        link.symlink_to(tree, target_is_directory=True)
    report = {'selected': selected, 'initial_headers': installed,
              'headers_without_image': sorted(headers-images), 'selection': 'newest common kernel-devel/kernel-core',
              'scope': 'disposable CI container only'}
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
