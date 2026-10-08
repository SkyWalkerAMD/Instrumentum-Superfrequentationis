#!/usr/bin/env python3
"""Read x86-64 ELF imports and direct call sites; never execute the sample.

PLT entries are resolved through their RIP-relative GOT relocation, not an
assumed PLT order/stride. Indirect calls, callbacks and runtime reachability
are deliberately not inferred from this static inventory.
"""
import argparse
import hashlib
import io
import json
import re
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


DEFAULT_FUNCTIONS = (
    r"^(main|_start|_ZN10MainWindowC[12]EP7QWidget|"
    r"_Z23initilize_kernel_driverv|_Z9load_kmodPKc|_Z5RdmsrjPjS_|"
    r"_Z5Wrmsrjjj|_Z7RdmsrTxjPjS_m|_Z7WrmsrTxjjjm|"
    r"_Z15enable_all_ioplv|_Z10is_it_asusv|_Z7getmoboB5cxx11v|"
    r"_Z14en_ec_decodingv|_Z20set_process_affinityi|_Z8isit_adlv|"
    r"_Z12check_if_amdv|_Z15test_dmi_get_mbi|_Z13getmobo_brandB5cxx11v|"
    r"_Z17ReadPciConfigWordjj)$"
)


def assembly_spans(elf, data, pattern, names, plt, imports):
    """Explicitly requested zero-sized labels, bounded by section/next symbol.

    These are byte spans, NOT inferred function extents or reachability. Keep
    them separate from the declared STT_FUNC inventory and its counts.
    """
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    groups = {}
    for symbol in symbols:
        if not pattern.search(symbol.name):
            continue
        index = symbol['st_shndx']
        if not isinstance(index, int):
            raise ValueError('assembly label is not section-defined: ' + symbol.name)
        section = elf.get_section(index)
        if (symbol['st_size'] or symbol['st_info']['type'] != 'STT_NOTYPE'
                or section['sh_type'] != 'SHT_PROGBITS' or not section['sh_flags'] & 4):
            raise ValueError('expected zero-sized NOTYPE label in executable section: ' + symbol.name)
        groups.setdefault((index, symbol['st_value']), []).append(symbol.name)
    rows = []
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    for (index, start), aliases in sorted(groups.items()):
        section = elf.get_section(index)
        limit = section['sh_addr'] + section['sh_size']
        if not section['sh_addr'] <= start < limit:
            raise ValueError('assembly label outside section: ' + aliases[0])
        following = [s['st_value'] for s in symbols if s['st_shndx'] == index
                     and start < s['st_value'] < limit]
        end = min(following) if following else limit
        begin = section['sh_offset'] + start - section['sh_addr']
        code = data[begin:begin + end - start]
        if len(code) != end - start:
            raise ValueError('truncated assembly span: ' + aliases[0])
        row = {'symbols': sorted(aliases), 'address': start, 'end': end,
               'section': section.name, 'declared_size': 0,
               'extent_rule': 'next symbol in same section, otherwise section end; not a function boundary',
               'boundary_symbols': names.get(end, []) if following else [],
               'code_sha256': hashlib.sha256(code).hexdigest(), 'instructions': []}
        decoded = 0
        for pc, size, mnemonic, operands in decoder.disasm_lite(code, start):
            decoded += size
            instruction = {'address': pc, 'size': size, 'mnemonic': mnemonic, 'operands': operands}
            if mnemonic in ('call', 'jmp') and operands.startswith('0x'):
                target = int(operands, 16)
                imported = plt.get(target)
                instruction['target_symbols'] = ([imports[imported]['symbol'] + '@plt']
                                                  if imported is not None else names.get(target, []))
            row['instructions'].append(instruction)
        row['decoded_bytes'] = decoded
        row['complete_decode'] = decoded == len(code)
        rows.append(row)
    return rows


def inspect(path, pattern, include_instructions=False, assembly_pattern=None, object_pattern=None):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 64 or not elf.little_endian or elf['e_machine'] != 'EM_X86_64':
        raise ValueError('expected little-endian x86-64 ELF')
    symtab = elf.get_section_by_name('.symtab')
    dynsym = elf.get_section_by_name('.dynsym')
    if symtab is None or dynsym is None:
        raise ValueError('both symbol tables required')
    loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']

    def offset(va):
        for segment in loads:
            relative = va - segment['p_vaddr']
            if 0 <= relative < segment['p_filesz']:
                return segment['p_offset'] + relative
        return None

    def literal(va):
        start = offset(va)
        if start is None:
            return None
        end = data.find(b'\0', start, start + 1024)
        if end < 0:
            return None
        try:
            value = data[start:end].decode('utf-8')
        except UnicodeDecodeError:
            return None
        if len(value) >= 3 and all(c.isprintable() or c in '\n\t\r' for c in value):
            return value
        return None

    versions = {}
    verneed = elf.get_section_by_name('.gnu.version_r')
    if verneed:
        for need, auxiliary in verneed.iter_versions():
            for aux in auxiliary:
                versions[aux['vna_other']] = (need.name, aux.name)
    versym = elf.get_section_by_name('.gnu.version')
    imports = {}
    for index, symbol in enumerate(dynsym.iter_symbols()):
        if not symbol.name or symbol['st_shndx'] != 'SHN_UNDEF':
            continue
        verindex = versym.get_symbol(index)['ndx'] if versym else None
        provider, version = versions.get(verindex & 0x7fff if isinstance(verindex, int) else verindex,
                                         (None, None))
        imports[index] = {'symbol': symbol.name, 'provider': provider, 'version': version,
                          'kind': symbol['st_info']['type'], 'sites': []}

    got = {}
    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection):
            continue
        linked = elf.get_section(section['sh_link'])
        if linked.name != '.dynsym':
            continue
        for rel in section.iter_relocations():
            if rel['r_info_sym'] in imports:
                got[rel['r_offset']] = rel['r_info_sym']

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    plt = {}
    for section in elf.iter_sections():
        if not section.name.startswith('.plt') or not section['sh_flags'] & 4:
            continue
        entry = None
        for ins in decoder.disasm(section.data(), section['sh_addr']):
            if ins.mnemonic == 'endbr64':
                entry = ins.address
            if ins.mnemonic in ('jmp', 'bnd jmp'):
                for op in ins.operands:
                    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                        relocation = ins.address + ins.size + op.mem.disp
                        if relocation in got:
                            plt[ins.address] = got[relocation]
                            if entry is not None and ins.address - entry <= 8:
                                plt[entry] = got[relocation]
                entry = None

    functions = {}
    names = {}
    objects = {}
    for symbol in symtab.iter_symbols():
        if symbol['st_value']:
            names.setdefault(symbol['st_value'], []).append(symbol.name)
        if symbol['st_info']['type'] == 'STT_FUNC' and symbol['st_size'] and offset(symbol['st_value']) is not None:
            key = (symbol['st_value'], symbol['st_size'])
            functions.setdefault(key, []).append(symbol.name)
        if object_pattern and object_pattern.search(symbol.name):
            if symbol['st_info']['type'] != 'STT_OBJECT' or not isinstance(symbol['st_shndx'], int):
                raise ValueError('expected section-defined object: ' + symbol.name)
            section = elf.get_section(symbol['st_shndx'])
            relative = symbol['st_value'] - section['sh_addr']
            size = symbol['st_size']
            if not 0 <= relative <= relative + size <= section['sh_size']:
                raise ValueError('object outside section: ' + symbol.name)
            objects[symbol['st_value']] = {
                'symbol': symbol.name, 'address': symbol['st_value'], 'size': size,
                'section': section.name, 'zero_initialized': section['sh_type'] == 'SHT_NOBITS',
                'initial_bytes_hex': section.data()[relative:relative + min(size, 64)].hex(),
                'references': []}
    selected, function_count, undecoded = [], 0, []
    # disasm_lite avoids allocating detailed instruction objects for millions
    # of arithmetic instructions. Decode details only for PC-relative operands.
    lite = Cs(CS_ARCH_X86, CS_MODE_64)
    for (address, size), aliases in sorted(functions.items()):
        start = offset(address)
        code = data[start:start + size]
        chosen = any(pattern.search(name) for name in aliases)
        row = {'symbols': sorted(aliases), 'address': address, 'size': size,
               'code_sha256': hashlib.sha256(code).hexdigest(), 'calls': [], 'literals': []}
        if chosen and include_instructions:
            row['instructions'] = []
        decoded = 0
        for pc, length, mnemonic, operands in lite.disasm_lite(code, address):
            decoded += length
            if chosen and include_instructions:
                row['instructions'].append({'address': pc, 'size': length,
                                            'mnemonic': mnemonic, 'operands': operands})
            imported = None
            if mnemonic in ('call', 'jmp', 'bnd jmp') and operands.startswith('0x'):
                target = int(operands, 16)
                imported = plt.get(target)
                if chosen:
                    row['calls'].append({'instruction': pc, 'kind': mnemonic, 'target': target,
                                         'symbols': ([imports[imported]['symbol'] + '@plt']
                                                     if imported is not None else names.get(target, []))})
            if 'rip' in operands:
                ins = next(decoder.disasm(code[pc-address:pc-address+length], pc))
                for op in ins.operands:
                    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                        target = pc + length + op.mem.disp
                        imported = got.get(target, imported)
                        if target in objects:
                            objects[target]['references'].append({
                                'function': aliases[0], 'function_address': address,
                                'instruction': pc, 'mnemonic': mnemonic, 'operands': operands})
                        if chosen and mnemonic == 'lea':
                            text = literal(target)
                            if text is not None:
                                row['literals'].append({'instruction': pc, 'address': target, 'text': text})
            if imported is not None:
                imports[imported]['sites'].append({'function': aliases[0], 'function_address': address,
                                                   'instruction': pc, 'kind': mnemonic})
        function_count += 1
        if decoded != size:
            undecoded.append({'address': address, 'size': size, 'decoded': decoded, 'symbols': aliases})
        if chosen:
            selected.append(row)

    dynamic = elf.get_section_by_name('.dynamic')
    needed, flags = [], []
    for tag in dynamic.iter_tags():
        if tag.entry.d_tag == 'DT_NEEDED':
            needed.append(tag.needed)
        if tag.entry.d_tag in ('DT_FLAGS', 'DT_FLAGS_1', 'DT_BIND_NOW'):
            flags.append({'tag': tag.entry.d_tag, 'value': tag.entry.d_val})
    return {'binary': path.name, 'sha256': hashlib.sha256(data).hexdigest(), 'executed': False,
            'entry': elf['e_entry'], 'needed': sorted(needed), 'dynamic_flags': flags,
            'gnu_stack_flags': [s['p_flags'] for s in elf.iter_segments() if s['p_type'] == 'PT_GNU_STACK'],
            'scope': 'Static direct calls / GOT data references only; no runtime reachability claim',
            'functions_scanned': function_count, 'incomplete_decodes': undecoded,
            'imports': sorted(imports.values(), key=lambda x: (x['provider'] or '', x['symbol'])),
            'selected_functions': selected,
            'selected_objects': sorted(objects.values(), key=lambda x: x['address']),
            'object_reference_scope': 'Exact RIP-relative object base references in declared functions only; no alias analysis',
            'assembly_spans': (assembly_spans(elf, data, assembly_pattern, names, plt, imports)
                               if assembly_pattern else [])}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--functions', default=DEFAULT_FUNCTIONS)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--el8-focus', action='store_true', help='keep ABI-floor and startup/hardware imports')
    parser.add_argument('--site-limit', type=int, default=0, help='limit examples per import; 0 keeps all')
    parser.add_argument('--instructions', action='store_true', help='include decoded instructions of selected functions')
    parser.add_argument('--assembly-symbols', help='regex selecting zero-sized NOTYPE labels; report bounded spans separately')
    parser.add_argument('--objects', help='regex selecting objects and exact RIP-relative base references')
    args = parser.parse_args()
    if args.site_limit < 0:
        parser.error('--site-limit cannot be negative')
    result = inspect(args.binary, re.compile(args.functions), args.instructions,
                     re.compile(args.assembly_symbols) if args.assembly_symbols else None,
                     re.compile(args.objects) if args.objects else None)
    result['total_imports'] = len(result['imports'])
    if args.el8_focus:
        def relevant(row):
            version = row['version'] or ''
            for prefix, floor in (('GLIBC_', (2, 28)), ('GLIBCXX_', (3, 4, 25))):
                if version.startswith(prefix):
                    if tuple(map(int, version[len(prefix):].split('.'))) > floor:
                        return True
            return bool(re.search(r'(_70$|^jpeg_|^hwloc_|^(iopl|open|open64|read|write|lseek|mmap|'
                                  r'readlink|chdir|syscall|system|sched_setaffinity)$)', row['symbol']))
        result['imports'] = [row for row in result['imports'] if relevant(row)]
    for row in result['imports']:
        row['site_count'] = len(row['sites'])
        if args.site_limit:
            row['sites'] = row['sites'][:args.site_limit]
    result['options'] = {'el8_focus': args.el8_focus, 'site_limit': args.site_limit,
                         'functions': args.functions, 'instructions': args.instructions,
                         'assembly_symbols': args.assembly_symbols, 'objects': args.objects}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode())
    print('{} imports, {} functions, {} selected; {} partial decodes'.format(
        len(result['imports']), result['functions_scanned'], len(result['selected_functions']),
        len(result['incomplete_decodes'])))


if __name__ == '__main__':
    main()
