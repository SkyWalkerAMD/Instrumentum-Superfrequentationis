#!/usr/bin/env python3
"""Read the original mailbox ELF/KO evidence; never load or execute a module."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import zipfile

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

GUI_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
MODULES = {
    'peter_kernel.ko': '4538764d874c39641106f8e3b3d97f0e6a6b02f1d27ba538e79dea1472b4a026',
    'peter_kernel_new.ko': '405b5a59b1a80a21e246f9cae90c112a232574978097a0aeaaafb8f71581322f',
    'peter_kernel_old.ko': '955be8f274f831d77ae259cad45a19ad663f3c62de93c0b565c3d35cc2c8d975',
}
GUI_FUNCTIONS = r'^(_Z\d+(Read_MMIO\d*_kernel|Write_MMIO\d*_kernel|initilize_kernel_driver|virt_to_phys_user|pagemap_get_entry|mem_chunk2|read_mmio_peter_lib)\w*)$'
MODULE_FUNCTIONS = re.compile(r'^(open|read|write|release|mmap|vm_open|vm_close|vm_fault|parse_user_request|mem_(read|write)(8|16|32|64))$')
LAYOUTS = {'file': {'private_data'}, 'vm_area_struct': {'vm_ops', 'vm_private_data'},
           'vm_fault': {'page'}}


def direct_callers(data, target):
    """Find relative call/jmp instructions inside declared function extents.

    Byte candidates are only a filter; report after decoding from the function
    start. Indirect branches and pointer aliases are outside this inventory.
    """
    elf = ELFFile(io.BytesIO(data))
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    hits = []
    for symbol in elf.get_section_by_name('.symtab').iter_symbols():
        index = symbol['st_shndx']
        if symbol['st_info']['type'] != 'STT_FUNC' or not symbol['st_size'] or not isinstance(index, int):
            continue
        section = elf.get_section(index)
        if section['sh_type'] != 'SHT_PROGBITS' or not section['sh_flags'] & 4:
            continue
        start, size = symbol['st_value'], symbol['st_size']
        off = section['sh_offset'] + start - section['sh_addr']
        code = data[off:off + size]
        if not any(byte in (0xe8, 0xe9) and n + 5 <= len(code)
                   and start + n + 5 + int.from_bytes(code[n + 1:n + 5], 'little', signed=True) == target
                   for n, byte in enumerate(code)):
            continue
        instructions = list(decoder.disasm_lite(code, start))
        if sum(i[1] for i in instructions) != size:
            raise ValueError('incomplete candidate decode: ' + symbol.name)
        for n, (pc, length, op, args) in enumerate(instructions):
            if op in ('call', 'jmp') and args == hex(target):
                hits.append({'symbol': symbol.name, 'address': start, 'site': pc,
                             'context': [{'address': a, 'size': z, 'mnemonic': m, 'operands': v}
                                         for a, z, m, v in instructions[max(0, n - 8):n + 4]]})
    return {'target': target, 'scope': 'direct call/jmp in declared STT_FUNC only; no indirect reachability inference',
            'sites': sorted(hits, key=lambda r: (r['site'], r['symbol']))}


def module_evidence(data, pattern=MODULE_FUNCTIONS, layouts=LAYOUTS):
    elf = ELFFile(io.BytesIO(data))
    if elf['e_type'] != 'ET_REL' or elf['e_machine'] != 'EM_X86_64' or not elf.little_endian:
        raise ValueError('expected relocatable little-endian x86-64 ELF')
    symbols = elf.get_section_by_name('.symtab')
    if symbols is None:
        raise ValueError('symbol table required')
    relocations = {}
    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection):
            continue
        table = elf.get_section(section['sh_link'])
        for reloc in section.iter_relocations():
            symbol = table.get_symbol(reloc['r_info_sym'])
            name = symbol.name
            if not name and isinstance(symbol['st_shndx'], int):
                name = elf.get_section(symbol['st_shndx']).name
            relocations.setdefault(section['sh_info'], []).append({
                'offset': reloc['r_offset'], 'type': reloc['r_info_type'], 'symbol': name,
                'addend': reloc['r_addend'] if reloc.is_RELA() else None})
    rows = []
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    for symbol in symbols.iter_symbols():
        if symbol['st_info']['type'] != 'STT_FUNC' or not pattern.search(symbol.name):
            continue
        index = symbol['st_shndx']
        if not isinstance(index, int) or not symbol['st_size']:
            raise ValueError('selected function lacks a section or declared extent')
        section = elf.get_section(index)
        start, size = symbol['st_value'], symbol['st_size']
        if section['sh_type'] != 'SHT_PROGBITS' or not section['sh_flags'] & 4 or start + size > section['sh_size']:
            raise ValueError('function outside executable section')
        code = section.data()[start:start + size]
        if len(code) != size:
            raise ValueError('truncated function')
        instructions = []
        for pc, length, mnemonic, operands in decoder.disasm_lite(code, start):
            instructions.append({'offset': pc, 'size': length, 'mnemonic': mnemonic,
                                 'operands': operands, 'relocations': [r for r in relocations.get(index, [])
                                                                        if pc <= r['offset'] < pc + length]})
        if sum(i['size'] for i in instructions) != size:
            raise ValueError('incomplete decode: ' + symbol.name)
        rows.append({'symbol': symbol.name, 'section': section.name, 'offset': start, 'size': size,
                     'code_sha256': hashlib.sha256(code).hexdigest(), 'instructions': instructions})
    fields = []
    if elf.has_dwarf_info():
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                attr = die.attributes.get('DW_AT_name')
                name = attr.value.decode('utf-8', 'replace') if attr and isinstance(attr.value, bytes) else ''
                if die.tag != 'DW_TAG_structure_type' or name not in layouts:
                    continue
                for member in die.iter_children():
                    attr = member.attributes.get('DW_AT_name')
                    field = attr.value.decode('utf-8', 'replace') if attr and isinstance(attr.value, bytes) else ''
                    loc = member.attributes.get('DW_AT_data_member_location')
                    if field in layouts[name] and loc and isinstance(loc.value, int):
                        fields.append({'struct': name, 'member': field, 'offset': loc.value,
                                       'dwarf_die_offset': member.offset})
    modinfo = elf.get_section_by_name('.modinfo')
    versions = [line.decode('utf-8', 'replace') for line in modinfo.data().split(b'\0')
                if line.startswith(b'vermagic=')] if modinfo else []
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'vermagic': versions,
            'address_convention': 'ET_REL offsets are section-relative; relocation operands are unapplied',
            'layout_fields': fields, 'functions': sorted(rows, key=lambda r: (r['section'], r['offset'], r['symbol']))}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--module-zip', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.binary.read_bytes()).hexdigest() != GUI_SHA:
        parser.error('GUI hash differs from the documented sample')
    spec = importlib.util.spec_from_file_location('elf_runtime_audit', Path(__file__).with_name('elf-runtime-audit.py'))
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    gui = audit.inspect(args.binary, re.compile(GUI_FUNCTIONS), True, None,
                        re.compile(r'^(MY_KMOD_LOADED|kernel_fd|kernel_address|user_request)$'))
    # Keep this evidence focused; full dynamic-import inventory is in the
    # preceding runtime report. Function instructions retain call labels.
    gui.pop('imports', None)
    result = {'schema': 1, 'scope': 'static original GUI and three module mailbox paths; no execution',
              'gui': gui, 'mem_chunk2_callers': direct_callers(args.binary.read_bytes(), 0x36ea40), 'modules': {}}
    with zipfile.ZipFile(args.module_zip) as archive:
        for name, digest in MODULES.items():
            matches = [n for n in archive.namelist() if Path(n).name == name]
            if len(matches) != 1:
                parser.error('expected exactly one ' + name)
            data = archive.read(matches[0])
            if hashlib.sha256(data).hexdigest() != digest:
                parser.error('module hash mismatch: ' + name)
            result['modules'][name] = module_evidence(data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    print('{} GUI functions; {} pinned modules; static evidence written'.format(len(gui['selected_functions']), len(result['modules'])))


if __name__ == '__main__':
    main()
