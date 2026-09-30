#!/bin/bash
# Diagnostic runtime for the unchanged, source-lost Ubuntu 22.04 executable.
# Run inside a disposable Ubuntu 22.04 container; no host libraries are changed.
set -euo pipefail
[ "${OCTOOL_DISPOSABLE_CONTAINER:-}" = 1 ]
. /etc/os-release
[ "$ID" = ubuntu ] && [ "$VERSION_ID" = 22.04 ]
[ "$(uname -m)" = x86_64 ]
input=${1:?original ELF required}
output=${2:?empty output directory required}
test ! -e "$output/octool"
printf '%s  %s\n' 44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10 "$input" | sha256sum -c -
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends python3 binutils libc6 libstdc++6 \
    libx11-6 libx11-xcb1 libxcb1 libxcb-glx0 libxcb-icccm4 libxcb-image0 \
    libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 \
    libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb-xinput0 libsm6 libice6 \
    libxcomposite1 libxkbcommon0 libxkbcommon-x11-0 libgl1 libegl1 libdrm2 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 libfontconfig1 \
    libfreetype6 libpng16-16 libjpeg-turbo8 zlib1g libdbus-1-3 libglib2.0-0 \
    libpcre2-16-0 libudev1 libicu70 libhwloc15
mkdir -p "$output/lib"
cp "$input" "$output/octool"
chmod 755 "$output/octool"
# Explicit --list resolves DT_NEEDED without running main/constructors.
/lib64/ld-linux-x86-64.so.2 --list "$output/octool" > "$output/loader-build.txt"
python3 - "$output" <<'PY'
import hashlib, json, re, shutil, subprocess, sys
from pathlib import Path
out = Path(sys.argv[1])
paths = set(re.findall(r'(?:=>\s+|^\s*)(/\S+)\s+\(', (out/'loader-build.txt').read_text(), re.M))
assert len(paths) > 45, 'incomplete dependency closure'
paths.add('/lib64/ld-linux-x86-64.so.2')
for name in ('libnss_files.so.2', 'libnss_dns.so.2', 'libresolv.so.2',
             'libpthread.so.0', 'libdl.so.2', 'librt.so.1'):
    p = Path('/lib/x86_64-linux-gnu') / name
    if p.exists():
        paths.add(str(p))
records = []
for name in sorted(paths):
    p = Path(name)
    content = p.read_bytes()  # Dereference links, including ld-linux.
    dest = out/'lib'/p.name
    if dest.exists():
        assert dest.read_bytes() == content, 'conflicting SONAME: ' + p.name
    dest.write_bytes(content)
    dest.chmod(0o755)
    records.append({'source': name, 'file': 'lib/'+p.name, 'size': len(content),
                    'sha256': hashlib.sha256(content).hexdigest()})
# The legacy application uses dirname(/proc/self/exe). Keep its matching
# loader beside the executable, not in lib/ or in a different directory.
shutil.copy2(out/'lib/ld-linux-x86-64.so.2', out/'ld-linux-x86-64.so.2')
gconv = Path('/usr/lib/x86_64-linux-gnu/gconv')
if gconv.exists():
    shutil.copytree(gconv, out/'lib/gconv')
# EL8/9 display-only installations may have fonts but no /etc/fonts at all.
# Materialize Ubuntu's config links and resolve them inside this application.
shutil.copytree('/etc/fonts', out/'etc/fonts', symlinks=False)
(out/'packages.tsv').write_bytes(subprocess.check_output(
    ['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\n']))
manifest = {'original_sha256': hashlib.sha256((out/'octool').read_bytes()).hexdigest(),
            'builder': 'ubuntu:22.04', 'files': records,
            'scope': 'Diagnostic runtime; not hardware or full feature validation'}
(out/'runtime.json').write_text(json.dumps(manifest, indent=2)+'\n')
PY
cp "$(dirname "$0")/run.sh" "$output/run.sh"
chmod 755 "$output/run.sh"
env -u LD_PRELOAD -u LD_AUDIT -u LD_LIBRARY_PATH \
    "$output/ld-linux-x86-64.so.2" --inhibit-cache --library-path "$output/lib" \
    --list "$output/octool" > "$output/loader-private.txt"
readelf -W --version-info "$output/octool" > "$output/elf-versions.txt"
