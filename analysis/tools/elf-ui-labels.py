#!/usr/bin/env python3
"""Inventory literal text references in selected x86-64 ELF UI functions.

This reads code as data. References are evidence, not reconstructed widget
bindings, register semantics, or proof that a particular branch was executed.
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


def inspect(path, pattern):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if elf.elfclass != 64 or not elf.little_endian or elf['e_machine'] != 'EM_X86_64':
        raise ValueError('expected little-endian x86-64 ELF')
    symbols = elf.get_section_by_name('.symtab')
    if symbols is None:
        raise ValueError('symbol table required')
    segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']

    def file_offset(address):
        for segment in segments:
            relative = address - segment['p_vaddr']
            if 0 <= relative < segment['p_filesz']:
                return segment['p_offset'] + relative
        return None

    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    functions = []
    for symbol in symbols.iter_symbols():
        if symbol['st_info']['type'] != 'STT_FUNC' or not pattern.search(symbol.name):
            continue
        address, size = symbol['st_value'], symbol['st_size']
        start = file_offset(address)
        if start is None or not size:
            continue
        code = data[start:start + size]
        references = []
        seen = set()
        for instruction in decoder.disasm(code, address):
            # Address-taking instructions can reference string literals.
            # SIMD loads of packed character arrays are not C-string pointers.
            if instruction.mnemonic != 'lea':
                continue
            for operand in instruction.operands:
                if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                    continue
                target = instruction.address + instruction.size + operand.mem.disp
                offset = file_offset(target)
                if offset is None or target in seen:
                    continue
                end = data.find(b'\0', offset, offset + 1024)
                if end < 0:
                    continue
                try:
                    value = data[offset:end].decode('utf-8')
                except UnicodeDecodeError:
                    continue
                if len(value) < 3 or not all(c.isprintable() or c in '\n\t\r' for c in value):
                    continue
                if not any(c.isalpha() for c in value):
                    continue
                seen.add(target)
                references.append({'instruction_address': instruction.address,
                                   'literal_address': target, 'literal_file_offset': offset,
                                   'text': value})
        functions.append({'symbol': symbol.name, 'address': address, 'size': size,
                          'code_sha256': hashlib.sha256(code).hexdigest(), 'references': references})
    return {'binary': path.name, 'sha256': hashlib.sha256(data).hexdigest(),
            'pattern': pattern.pattern, 'executed': False,
            'scope': 'Literal references only; no inferred widget bindings or register semantics',
            'functions': sorted(functions, key=lambda row: row['address'])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--pattern', required=True, help='regular expression for ELF function symbols')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.binary, re.compile(args.pattern))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, indent=2, ensure_ascii=False) + '\n').encode())
    print('{} functions, {} literal references'.format(
        len(result['functions']), sum(len(f['references']) for f in result['functions'])))


if __name__ == '__main__':
    main()
