#!/usr/bin/env python3
"""crccheck.py - would a prebuilt .ko pass the modversions check on kernel X?

For every symbol a module imports, compare the CRC recorded in the module's
__versions section (plus __version_ext_* on 6.14+ extended modversions, and the
variable-length layout Ubuntu 23.04's 6.2 kernels used) with the CRC exported by
the kernel's Module.symvers. One mismatch or missing symbol means the kernel
refuses the module ("disagrees about version of symbol ...").

usage:
  crccheck.py [-s PATH_OR_GLOB ...] module.ko [module.ko ...]

-s defaults to the Module.symvers of every kernel whose headers are installed:
  /lib/modules/*/build/Module.symvers, /usr/src/kernels/*/Module.symvers,
  /usr/src/linux-headers-*/Module.symvers
needs: pyelftools (pip install pyelftools)
"""
import argparse
import glob
import os
import struct
import sys

from elftools.elf.elffile import ELFFile


def module_versions(path):
    with open(path, 'rb') as f:
        e = ELFFile(f)
        out = {}
        s = e.get_section_by_name('__versions')
        if s is not None:
            d = s.data()
            if len(d) % 64 == 0 and d[4:8] == b'\0\0\0\0':
                # classic layout: unsigned long crc; char name[56]
                for i in range(0, len(d), 64):
                    crc = struct.unpack_from('<Q', d, i)[0]
                    name = d[i + 8:i + 64].split(b'\0')[0].decode()
                    out[name] = crc & 0xffffffff
            else:
                # Ubuntu 6.2 SAUCE variable-length layout: u32 next, u32 crc, name
                i = 0
                while i + 8 <= len(d):
                    nxt, crc = struct.unpack_from('<II', d, i)
                    if nxt == 0:
                        break
                    out[d[i + 8:i + nxt].split(b'\0')[0].decode()] = crc
                    i += nxt
        ec = e.get_section_by_name('__version_ext_crcs')
        en = e.get_section_by_name('__version_ext_names')
        if ec is not None and en is not None:
            crcs = [struct.unpack_from('<I', ec.data(), i)[0] for i in range(0, ec.data_size, 4)]
            names = [n.decode() for n in en.data().split(b'\0') if n]
            out.update(zip(names, crcs))
        mi = e.get_section_by_name('.modinfo').data().split(b'\0')
        vermagic = next((x.decode().split('=', 1)[1] for x in mi if x.startswith(b'vermagic=')), '?')
        return out, vermagic.strip()


def symvers(path):
    m = {}
    with open(path) as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            if len(p) >= 2:
                m[p[1]] = int(p[0], 16)
    return m


def label(path):
    d = os.path.dirname(os.path.realpath(path))
    return os.path.basename(d) or d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-s', '--symvers', action='append', default=[],
                    help='Module.symvers file or glob (repeatable)')
    ap.add_argument('modules', nargs='+')
    a = ap.parse_args()
    pats = a.symvers or ['/lib/modules/*/build/Module.symvers',
                         '/usr/src/kernels/*/Module.symvers',
                         '/usr/src/linux-headers-*/Module.symvers']
    files = sorted({os.path.realpath(p) for pat in pats for p in glob.glob(pat, recursive=True)})
    if not files:
        sys.exit('no Module.symvers found; install kernel headers or pass -s')
    tables = [(label(p), symvers(p)) for p in files]
    for mp in a.modules:
        vers, vm = module_versions(mp)
        print(f"\n### {os.path.basename(mp)}  vermagic='{vm}'  imported-with-CRC={len(vers)}")
        if not vers:
            print('  no __versions section: modversions check does not apply')
            continue
        for name, sv in tables:
            missing = [n for n in vers if n not in sv]
            bad = [n for n in vers if n in sv and sv[n] != vers[n]]
            ok = len(vers) - len(missing) - len(bad)
            verdict = 'LOADABLE (all CRCs match)' if not missing and not bad else 'REJECTED'
            extra = ''
            if missing:
                extra += '  missing: ' + ' '.join(missing)
            if bad:
                extra += '  mismatch: ' + ' '.join(bad[:8]) + (' ...' if len(bad) > 8 else '')
            print(f"  {name:32s} ok={ok:2d} crc-mismatch={len(bad):2d} missing={len(missing):2d}  {verdict}{extra}")


if __name__ == '__main__':
    main()
