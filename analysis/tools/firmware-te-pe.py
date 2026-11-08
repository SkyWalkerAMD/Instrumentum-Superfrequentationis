#!/usr/bin/env python3
"""Strict IA32 TE -> analysis-only PE32 wrapper; never load or execute firmware.

Only the two pinned Granite images are used by the pipeline. No relocation,
rebasing or instruction patching is performed. Original TE bytes remain the
identity for emulation; REA evidence identifies the separately hashed wrapper.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


GRANITE_TE_MODULES = {
    'core-status-producer': dict(
        guid='F3FDA9D8-4EE5-44D8-8E49-AFAA77B5FC51', bytes=33584,
        sha256='7464250e2d6f8e3268af5da403997d57c86485ef86b25a23bc2b4af6abc65f1f',
        image_base=0x09ac6280, entry_rva=0x3c0,
        analysis_sha256='ecf94dafb435b1058bf5351960b18910674170bf066e148a9bc73616232c0992',
        procedure_rvas=(0x3c0, 0x409, 0x66d, 0x997, 0x4789, 0x4917, 0x492a, 0x4a21)),
    'core-status-consumer': dict(
        guid='C8F6D366-DDF1-4960-9D57-82F2655A1294', bytes=20736,
        sha256='c76d2ac61c511c113406297113d999f9cf7f1827c3b4413029907d5f7966ecb6',
        image_base=0x09b07988, entry_rva=0x399,
        analysis_sha256='c821f23f2ac30abfb9a4b6a93ab2920b9b15a19cab8be836e1409c77b1c635a9',
        procedure_rvas=(0x399, 0x1f7b, 0x434a)),
}
PE_OFFSET = 0x80
OPTIONAL_SIZE = 224
PE_SECTIONS = PE_OFFSET + 24 + OPTIONAL_SIZE
ALIGNMENT = 0x20


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value):
    return (value + ALIGNMENT - 1) & -ALIGNMENT


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse_te(body):
    require(len(body) >= 40, 'Truncated TE header')
    signature, machine, count, subsystem, stripped, entry, code, base = struct.unpack_from('<HHBBHIIQ', body)
    require(signature == 0x5a56 and machine == 0x14c and subsystem == 11,
            'Expected IA32 EFI boot-service TE image')
    require(1 <= count <= 16 and stripped >= 40, 'Invalid TE count/stripped size')
    require(40 + count * 40 <= len(body), 'Truncated TE section table')
    require(0 < base <= 0xffffffff, 'TE ImageBase does not fit PE32')
    delta = stripped - 40
    sections = []
    raw_ranges, virtual_ranges = [], []
    for index in range(count):
        fields = struct.unpack_from('<8s6I2HI', body, 40 + index * 40)
        name, virtual_size, rva, size, raw, reloc, lines, reloc_count, line_count, flags = fields
        offset = raw - delta
        require(size > 0 and offset >= 40 + count * 40 and offset <= len(body) - size,
                'Invalid TE section file extent')
        require(rva > 0 and rva + max(virtual_size, size) + base <= 0x100000000,
                'Invalid TE section virtual extent')
        require(raw == rva and rva % ALIGNMENT == 0 and size % ALIGNMENT == 0,
                'Unsupported TE section layout; no normalization is allowed')
        require(not (reloc or lines or reloc_count or line_count), 'Unsupported section auxiliary tables')
        raw_ranges.append((offset, offset + size))
        virtual_ranges.append((rva, rva + max(virtual_size, size)))
        sections.append(dict(name=name, virtual_size=virtual_size, rva=rva, raw_size=size,
            te_offset=offset, pe_offset=raw, flags=flags, body=body[offset:offset + size]))
    for ranges in (raw_ranges, virtual_ranges):
        ordered = sorted(ranges)
        require(all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:])), 'Overlapping TE sections')
    require(max(end for _, end in raw_ranges) == len(body), 'Unexpected TE overlay/trailing data')
    require(any(s['rva'] <= entry < s['rva'] + s['raw_size'] and s['flags'] & 0x20000000
                for s in sections), 'TE entry is not backed by executable section bytes')
    directories = (struct.unpack_from('<II', body, 24), struct.unpack_from('<II', body, 32))
    for rva, size in directories:
        require((rva == 0 and size == 0) or (size > 0 and any(
            s['rva'] <= rva and rva + size <= s['rva'] + s['raw_size'] for s in sections)),
            'TE data directory is not backed by section bytes')
    return dict(machine=machine, subsystem=subsystem, stripped_size=stripped, delta=delta,
                entry_rva=entry, base_of_code=code, image_base=base,
                sections=sections, directories=directories)


def parse_pe(body):
    require(len(body) >= PE_SECTIONS and body[:2] == b'MZ', 'Invalid analysis DOS header')
    require(struct.unpack_from('<I', body, 0x3c)[0] == PE_OFFSET and
            body[PE_OFFSET:PE_OFFSET + 4] == b'PE\0\0', 'Invalid analysis PE header')
    machine, count, _, _, _, optional_size, flags = struct.unpack_from('<HHIIIHH', body, PE_OFFSET + 4)
    optional = PE_OFFSET + 24
    require(machine == 0x14c and optional_size == OPTIONAL_SIZE and
            struct.unpack_from('<H', body, optional)[0] == 0x10b, 'Expected analysis PE32')
    require(1 <= count <= 16 and PE_SECTIONS + count * 40 <= len(body), 'Invalid PE section table')
    base = struct.unpack_from('<I', body, optional + 28)[0]
    entry = struct.unpack_from('<I', body, optional + 16)[0]
    sections = []
    for index in range(count):
        name, virtual_size, rva, size, raw, _, _, _, _, section_flags = struct.unpack_from(
            '<8s6I2HI', body, PE_SECTIONS + index * 40)
        require(raw >= PE_SECTIONS + count * 40 and raw <= len(body) - size, 'Invalid PE section extent')
        sections.append(dict(name=name, virtual_size=virtual_size, rva=rva, raw_size=size,
            pe_offset=raw, flags=section_flags, body=body[raw:raw + size]))
    return dict(image_base=base, entry_rva=entry, flags=flags, sections=sections,
        base_of_code=struct.unpack_from('<I', body, optional + 20)[0],
        subsystem=struct.unpack_from('<H', body, optional + 68)[0],
        directories=tuple(struct.unpack_from('<II', body, optional + 96 + i * 8) for i in (5, 6)))


def rva_to_offset(image, rva, source):
    require(source in ('te', 'pe'), 'Unknown input format')
    matches = [s for s in image['sections'] if s['rva'] <= rva < s['rva'] + s['raw_size']]
    require(len(matches) == 1, 'RVA does not select exactly one file-backed section')
    section = matches[0]
    return section[source + '_offset'] + rva - section['rva']


def verify_equivalent(te_bytes, pe_bytes):
    te, pe = parse_te(te_bytes), parse_pe(pe_bytes)
    require(te['image_base'] == pe['image_base'] and te['entry_rva'] == pe['entry_rva'],
            'Analysis wrapper changed ImageBase or entry RVA')
    for field in ('base_of_code', 'subsystem', 'directories'):
        require(te[field] == pe[field], 'Analysis wrapper changed TE field ' + field)
    require(len(te['sections']) == len(pe['sections']), 'Analysis section count differs')
    require(pe['flags'] == 0x0100, 'Analysis wrapper must not set executable-image characteristic')
    report = []
    for original, wrapped in zip(te['sections'], pe['sections']):
        for field in ('name', 'virtual_size', 'rva', 'raw_size', 'flags', 'body'):
            require(original[field] == wrapped[field], 'Analysis wrapper changed section ' + field)
        report.append(dict(name=original['name'].rstrip(b'\0').decode('ascii'),
            rva=hex(original['rva']), virtual_address=hex(te['image_base'] + original['rva']),
            virtual_size=original['virtual_size'], raw_bytes=original['raw_size'],
            original_te_file_offset=original['te_offset'], analysis_pe_file_offset=wrapped['pe_offset'],
            sha256=digest(original['body']), bytes_equal=True, rva_equal=True,
            virtual_address_equal=True))
    return dict(schema=1, original_format='IA32 TE', analysis_format='PE32',
        original_sha256=digest(te_bytes), analysis_sha256=digest(pe_bytes),
        original_bytes=len(te_bytes), analysis_bytes=len(pe_bytes),
        image_base=hex(te['image_base']), entry_rva=hex(te['entry_rva']),
        entry_virtual_address=hex(te['image_base'] + te['entry_rva']),
        te_stripped_delta=te['delta'], sections=report, analysis_only=True,
        section_bytes_patched=False, relocations_applied=False, hardware_execution=False,
        executable_image_characteristic=False,
        synthesized_headers='DOS/COFF/PE32; no original PE header recovery is claimed')


def wrap_te(body):
    te = parse_te(body)
    sections = te['sections']
    header_size = align(PE_SECTIONS + len(sections) * 40)
    require(header_size <= min(s['pe_offset'] for s in sections), 'Analysis headers overlap section bytes')
    out = bytearray(max(s['pe_offset'] + s['raw_size'] for s in sections))
    out[:2] = b'MZ'
    struct.pack_into('<I', out, 0x3c, PE_OFFSET)
    out[PE_OFFSET:PE_OFFSET + 4] = b'PE\0\0'
    # Deliberately omit IMAGE_FILE_EXECUTABLE_IMAGE. This is an analysis artifact,
    # not a runnable/restored PE. Section execute flags remain original evidence.
    struct.pack_into('<HHIIIHH', out, PE_OFFSET + 4, 0x14c, len(sections), 0, 0, 0, OPTIONAL_SIZE, 0x0100)
    optional = PE_OFFSET + 24
    struct.pack_into('<H', out, optional, 0x10b)
    struct.pack_into('<III', out, optional + 4,
        sum(s['raw_size'] for s in sections if s['flags'] & 0x20),
        sum(s['raw_size'] for s in sections if s['flags'] & 0x40), 0)
    struct.pack_into('<IIIIII', out, optional + 16, te['entry_rva'], te['base_of_code'],
        min((s['rva'] for s in sections if s['flags'] & 0x40), default=0),
        te['image_base'], ALIGNMENT, ALIGNMENT)
    struct.pack_into('<II', out, optional + 56,
        align(max(s['rva'] + max(s['virtual_size'], s['raw_size']) for s in sections)), header_size)
    struct.pack_into('<H', out, optional + 68, te['subsystem'])
    struct.pack_into('<I', out, optional + 92, 16)
    for pe_index, values in zip((5, 6), te['directories']):
        struct.pack_into('<II', out, optional + 96 + pe_index * 8, *values)
    for index, s in enumerate(sections):
        struct.pack_into('<8s6I2HI', out, PE_SECTIONS + index * 40,
            s['name'], s['virtual_size'], s['rva'], s['raw_size'], s['pe_offset'], 0, 0, 0, 0, s['flags'])
        out[s['pe_offset']:s['pe_offset'] + s['raw_size']] = s['body']
    wrapped = bytes(out)
    return wrapped, verify_equivalent(body, wrapped)


def pinned_wrapper(name, body):
    profile = GRANITE_TE_MODULES[name]
    require(len(body) == profile['bytes'] and digest(body) == profile['sha256'], 'Unexpected original TE identity')
    wrapped, report = wrap_te(body)
    require(digest(wrapped) == profile['analysis_sha256'], 'Unexpected derived PE identity')
    te, pe = parse_te(body), parse_pe(wrapped)
    require(te['image_base'] == profile['image_base'] and te['entry_rva'] == profile['entry_rva'],
            'Unexpected pinned TE address identity')
    report['procedures'] = [dict(rva=hex(rva), virtual_address=hex(te['image_base'] + rva),
        original_te_file_offset=hex(rva_to_offset(te, rva, 'te')),
        analysis_pe_file_offset=hex(rva_to_offset(pe, rva, 'pe')))
        for rva in profile['procedure_rvas']]
    report['module'] = name
    return wrapped, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', choices=tuple(GRANITE_TE_MODULES), required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(len({args.input.resolve(), args.output.resolve(), args.report.resolve()}) == 3,
            'Original input, wrapper and report must remain separate')
    wrapped, report = pinned_wrapper(args.module, args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(wrapped)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Verified analysis-only PE wrapper; original section bytes and addresses preserved')


if __name__ == '__main__':
    main()
