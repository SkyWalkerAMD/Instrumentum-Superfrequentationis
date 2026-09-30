#!/usr/bin/env python3
"""Read Qt5 metadata in x64 reference executables without loading their code.

Requires pefile and pyelftools. Output is recovery evidence, not recovered C++.
Qt layout reference: qtbase/src/tools/moc/generator.cpp (Qt 5.15.18).
"""
import argparse
import hashlib
import io
import json
import re
import struct
from pathlib import Path

import pefile
from elftools.elf.elffile import ELFFile


def strings_at(data, start):
    ref, length, allocation, padding, offset = struct.unpack_from('<iIIIq', data, start)
    if ref != -1 or allocation or padding or offset < 24 or offset % 24 or offset > 240000:
        return None
    strings = []
    for index in range(offset // 24):
        location = start + index * 24
        ref, length, allocation, padding, relative = struct.unpack_from('<iIIIq', data, location)
        position = location + relative
        if (ref != -1 or allocation or padding or length > 65535 or
                position < start + offset or position + length >= len(data) or
                data[position + length] != 0):
            return None
        try:
            strings.append(data[position:position + length].decode('utf-8'))
        except UnicodeDecodeError:
            return None
    if not re.fullmatch(r'[A-Za-z_]\w*(?:::\w+)*', strings[0]):
        return None
    return {'class': strings[0], 'file_offset': start, 'strings': strings}


def inspect(path):
    data = path.read_bytes()
    found = []
    if data.startswith(b'\x7fELF'):
        elf = ELFFile(io.BytesIO(data))
        if elf.elfclass != 64 or not elf.little_endian:
            raise ValueError('only little-endian ELF64 is supported')
        symbols = elf.get_section_by_name('.symtab')
        if symbols is None:
            raise ValueError('ELF symbol table is required for this recovery method')
        for symbol in symbols.iter_symbols():
            if 'qt_meta_stringdata_' not in symbol.name:
                continue
            section = elf.get_section(symbol['st_shndx'])
            offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr']
            entry = strings_at(data, offset)
            if entry:
                entry['symbol'] = symbol.name
                found.append(entry)
    elif data.startswith(b'MZ'):
        pe = pefile.PE(data=data)
        if pe.FILE_HEADER.Machine != 0x8664:
            raise ValueError('only x64 PE is supported')
        candidates = {}
        for match in re.finditer(b'\xff\xff\xff\xff', data):
            offset = match.start()
            if offset % 8:
                continue
            try:
                entry = strings_at(data, offset)
                if entry:
                    va = pe.OPTIONAL_HEADER.ImageBase + pe.get_rva_from_offset(offset)
                    candidates[va] = entry
            except (struct.error, IndexError, pefile.PEFormatError):
                continue
        # A QByteArrayData-shaped string alone is not sufficient. Require a
        # relocated QMetaObject.stringdata pointer followed by a valid data
        # pointer whose metadata descriptor has the expected Qt revision/name.
        seen = set()
        for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
            for reloc in block.entries:
                if reloc.type != 10:  # IMAGE_REL_BASED_DIR64
                    continue
                try:
                    offset = pe.get_offset_from_rva(reloc.rva)
                    string_va, meta_va = struct.unpack_from('<QQ', data, offset)
                    if string_va not in candidates or string_va in seen:
                        continue
                    meta_offset = pe.get_offset_from_rva(meta_va - pe.OPTIONAL_HEADER.ImageBase)
                    header = struct.unpack_from('<14I', data, meta_offset)
                    if header[0] not in (7, 8) or header[1] != 0:
                        continue
                    entry = dict(candidates[string_va], meta_offset=meta_offset,
                                 revision=header[0], method_count=header[4])
                    found.append(entry)
                    seen.add(string_va)
                except (struct.error, IndexError, pefile.PEFormatError):
                    continue
    else:
        raise ValueError('not an ELF/PE executable')
    found.sort(key=lambda entry: entry['class'])
    return {'file': path.name, 'sha256': hashlib.sha256(data).hexdigest(),
            'class_count': len(found), 'classes': found,
            'executed': False, 'scope': 'Qt metadata; not C++ source or hardware semantics'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.binary)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes((json.dumps(result, indent=2, ensure_ascii=False) + '\n').encode())
    print('{}: {} metadata classes'.format(args.binary.name, result['class_count']))


if __name__ == '__main__':
    main()
