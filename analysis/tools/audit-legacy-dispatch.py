#!/usr/bin/env python3
"""Extract platform selection evidence from the pinned ELF without running it.

Function names describe the original program, not verified CPU support. Only
small, explicitly listed routines are exported as an emulator fixture. The
complete original executable is never an output of this tool.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re

from elftools.elf.elffile import ELFFile

SHA256 = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
EMULATED = (
    '_Z12check_if_amdv', '_Z8isit_nvlv', '_Z8isit_arlv', '_Z8isit_adlv',
    '_Z8isit_rplv', '_Z8isit_rklv', '_Z8isit_sprv', '_Z11isit_gnr_spv',
    '_Z10is_it_asusv', '_ZN10proc_classC2Ev', '_Z13is_intel_hedtv',
    '_Z9get_oc_okRbS_S_', '_ZN10MainWindow27on_actionControls_triggeredEv',
)
HELPERS = (
    '_Z7getmoboB5cxx11v', '_Z13getmobo_brandB5cxx11v', '_Z15test_dmi_get_mbi',
    '_Z13dmi_decode_mbP10dmi_headerti', '_Z17dmi_table_decode2Phjttji',
    '_Z16GetPciDeviceListv', '_Z23GetPciDeviceList_pcilibPhS_S_PtS0_Pj',
    '_Z22FindPciDeviceById_realtth', '_Z24FindPciDeviceById_pcilibtt',
    '_Z8checkvrmv', '_Z12rd_oc_enablev', '_Z32Rd_CAPID0_B_HOSTBRIDGE_CFG_Tunerv',
    '_Z14get_oc_supportv', '_Z30get_overclocking_optin_supportv',
    '_ZN10MainWindowC2EP7QWidget',
)
OBJECTS = ('GLOBAL_IS_NVL', 'GLOBAL_IS_GNR_SP', 'IS_ARL_GLOBAL',
           '_ZN10QArrayData11shared_nullE')


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def investigate(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('input differs from the documented original ELF')
    spec = importlib.util.spec_from_file_location('runtime_audit',
                                                 Path(__file__).with_name('elf-runtime-audit.py'))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    selection = re.compile('^(' + '|'.join(map(re.escape, EMULATED + HELPERS))
                           + r'|_ZN10MainWindow\d+on_\w+Ev)$')
    result = audit.inspect(path, selection, True)
    functions = result.pop('selected_functions')
    result.pop('imports')
    result['selected_functions'] = [f for f in functions if any(
        name in EMULATED + HELPERS for name in f['symbols'])]
    slots = [f for f in functions if any(re.fullmatch(r'_ZN10MainWindow\d+on_\w+Ev', n)
                                        for n in f['symbols'])]
    result['mainwindow_slots'] = [{k: v for k, v in f.items() if k != 'instructions'} for f in slots]
    result['slot_scope'] = ('Direct call/jump and literal inventory only. Does not resolve Qt connections, '
                            'virtual calls, other window slots, indirect branches or hardware semantics.')
    result['method'] = 'Full original SHA; declared function sizes; PLT/GOT resolution; Capstone x86-64'
    elf = ELFFile(io.BytesIO(data))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    chosen = {name: next(f for f in result['selected_functions'] if name in f['symbols']) for name in EMULATED}
    fixture = {'schema': 1, 'source_elf_sha256': SHA256,
               'scope': 'Unchanged platform/Controls decision routines only; external calls synthetic; no GUI/hardware execution',
               'functions': [], 'objects': {}, 'literals': []}
    literals = {}
    for name in EMULATED:
        fn = chosen[name]
        symbol = symbols[name]
        section = elf.get_section(symbol['st_shndx'])
        offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr']
        code = data[offset:offset + symbol['st_size']]
        if hashlib.sha256(code).hexdigest() != fn['code_sha256']:
            raise ValueError('function extraction disagreement: ' + name)
        fixture['functions'].append({'symbol': name, 'address': fn['address'], 'size': len(code),
                                      'code_sha256': fn['code_sha256'], 'code_hex': code.hex(),
                                      'calls': fn['calls']})
        for literal in fn['literals']:
            literals[literal['address']] = literal['text']
    fixture['literals'] = [{'address': va, 'text': text} for va, text in sorted(literals.items())]
    for name in OBJECTS:
        symbol = symbols[name]
        fixture['objects'][name] = {'address': symbol['st_value'], 'size': symbol['st_size'],
                                     'emulator_initialization': 'synthetic; original contents not exported'}
    result['fixture_sha256'] = hashlib.sha256(encoded(fixture)).hexdigest()
    result['fixture_function_count'] = len(EMULATED)
    result['fixture_code_bytes'] = sum(f['size'] for f in fixture['functions'])
    return result, fixture


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--export-fixture', type=Path)
    args = parser.parse_args()
    result, fixture = investigate(args.binary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded(result))
    if args.export_fixture:
        args.export_fixture.parent.mkdir(parents=True, exist_ok=True)
        args.export_fixture.write_bytes(encoded(fixture))
    print('{} detailed functions, {} MainWindow slots, {} fixture bytes, SHA {}'.format(
        len(result['selected_functions']), len(result['mainwindow_slots']), result['fixture_code_bytes'],
        result['fixture_sha256']))


if __name__ == '__main__':
    main()
