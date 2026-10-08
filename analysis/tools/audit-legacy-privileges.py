#!/usr/bin/env python3
"""Reproduce the original OCTool startup/privilege evidence without execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re


EXPECTED_SHA256 = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FUNCTIONS = (
    r'^(_Z23initilize_kernel_driverv|_Z9load_kmodPKc|_Z5RdmsrjPjS_|_Z5Wrmsrjjj|'
    r'_Z7RdmsrTxjPjS_m|_Z7WrmsrTxjjjm|_Z15enable_all_ioplv|_Z20set_process_affinityi|'
    r'_Z16set_all_affinityv|_Z14en_ec_decodingv|_ZN10MainWindowC2EP7QWidget|'
    r'_ZN3SIOC2Ev|_ZN3SIO8enter_efEv|_Z9Read_MMIOm|_Z16Read_MMIO_kernelm|'
    r'_Z17libpci_initializev|_Z17WriteIoPortByteExth|_Z19read_mmio_peter_libmm|'
    r'_Z10mem_chunk2mmPKc|_ZN6MB_SMBC1Ev)$'
)
ASSEMBLY = (r'^(my_iopl|ReadIoPort(Byte|Word)|WriteIoPort(Byte|Word)|'
            r'call_(get_(uid|gid|ppid|pgrp|pgid)|set_(uid|gid|pgid)|sys_(chown|mmap)))$')
OBJECTS = r'^MY_KMOD_LOADED$'
IMPORTS = {'iopl', 'open', 'read', 'write', 'lseek', 'mmap', 'syscall',
           'sched_setaffinity', 'sched_getaffinity'}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.binary.read_bytes()).hexdigest() != EXPECTED_SHA256:
        parser.error('input differs from the documented original ELF')
    spec = importlib.util.spec_from_file_location('elf_runtime_audit',
                                                 Path(__file__).with_name('elf-runtime-audit.py'))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    result = audit.inspect(args.binary, re.compile(FUNCTIONS), True,
                           re.compile(ASSEMBLY), re.compile(OBJECTS))
    result['total_imports'] = len(result['imports'])
    result['imports'] = [row for row in result['imports'] if row['symbol'] in IMPORTS]
    for row in result['imports']:
        row['site_count'] = len(row['sites'])
        row['sites'] = row['sites'][:1]
    result['options'] = {'functions': FUNCTIONS, 'instructions': True,
                         'assembly_symbols': ASSEMBLY, 'objects': OBJECTS,
                         'imports_filter': 'startup syscall imports, first site only'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print('{} functions, {} assembly spans, {} object references'.format(
        len(result['selected_functions']), len(result['assembly_spans']),
        sum(len(o['references']) for o in result['selected_objects'])))


if __name__ == '__main__':
    main()
