#!/usr/bin/env python3
"""EL8 local-account fixture for a disposable container without systemd PID 1."""
import argparse
import json
import os
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert os.geteuid() == 0 and os.environ.get('OCTOOL_DISPOSABLE_CONTAINER') == '1'
    release = Path('/etc/os-release').read_text()
    assert re.search(r'^ID="?rocky"?$', release, re.MULTILINE)
    assert re.search(r'^VERSION_ID="?8\.', release, re.MULTILINE)
    pid1 = Path('/proc/1/comm').read_text().strip()
    assert pid1 != 'systemd', 'Do not change NSS on a booted system'
    path = Path('/etc/nsswitch.conf')
    before = path.read_text()
    lines = []
    changed = []
    for line in before.splitlines(keepends=True):
        match = re.match(r'^(\s*(passwd|group|initgroups)\s*:)([^#\n]*)(.*)$', line.rstrip('\n'))
        if match and 'systemd' in match[3].split():
            # This known EL8 image has plain providers, not bracketed actions.
            # Refuse an unfamiliar policy instead of re-associating its rules.
            assert '[' not in match[3] and ']' not in match[3], line
            providers = [word for word in match[3].split() if word != 'systemd']
            assert 'files' in providers, line
            line = match[1] + ' ' + ' '.join(providers) + match[4] + '\n'
            changed.append(match[2])
        lines.append(line)
    after = ''.join(lines)
    path.write_text(after)
    args.output.write_text(json.dumps({'scope': 'disposable EL8 container only; local accounts',
                                      'pid1': pid1, 'removed_provider': 'systemd',
                                      'changed_databases': changed, 'before': before, 'after': after}, indent=2) + '\n')


if __name__ == '__main__':
    main()
