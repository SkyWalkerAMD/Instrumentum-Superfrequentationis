#!/usr/bin/env python3
"""Add bounded static FP decoding to an isolated, pinned Ghidra installation.

No firmware bytes are changed. Only CONST.S has numeric semantics here. The
approximation/refinement instructions remain named, opaque p-code operations.
This is not an Xtensa emulator and does not model coprocessor exceptions.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

QEMU_REV = 'd73149283205645625bfa9fe029cfb68e4c4a9ae'
SOURCE = f'https://github.com/qemu/qemu/blob/{QEMU_REV}/target/xtensa/'
# name, opcode t-field, destination also read, updates FP status flags
UNARY = (
    ('div0', 7, False, False), ('recip0', 8, False, True),
    ('sqrt0', 9, False, False), ('rsqrt0', 10, False, True),
    ('nexp01', 11, False, False), ('mksadj', 12, False, True),
    ('mkdadj', 13, True, True), ('addexp', 14, True, False),
    ('addexpm', 15, True, False),
)


def definitions():
    lines = [
        '# OCTool static research extension; see SmuFpPatch.py.',
        '# Exact CONST.S; all approximation/refinement results stay opaque.',
    ]
    for immediate, bits in enumerate((0, 0x3f800000, 0x40000000, 0x3f000000)):
        # fs aliases the same encoded bits as op_s. Constrain the token alias,
        # not the displayed operand (which SLEIGH treats as an operand symbol).
        lines.append(f':const.s fr, op_s is op2=15 & op1=10 & fr & op_s & fs={immediate} & op_t=3 & op0=0 {{ fr = 0x{bits:08x}; }}')
    # Immediates 4..15 are reserved, deliberately left undecodable.
    for name, opcode, read_dest, flags in UNARY:
        op = 'octool_' + name + '_s'
        lines.append(f'define pcodeop {op};')
        args = 'fr, fs' if read_dest else 'fs'
        body = ''
        if flags:
            lines.append(f'define pcodeop {op}_flags;')
            body = f'local nextFlags:4 = {op}_flags(FSR, FCR, {args}); '
        lines.append(f':{name}.s fr, fs is op2=15 & op1=10 & fr & fs & op_t={opcode} & op0=0 {{ '
                     + body + f'fr = {op}({args}); '
                     + ('FSR = nextFlags; ' if flags else '') + '}')
    for name, opcode, flags in (('maddn', 6, False), ('divn', 7, True)):
        op = 'octool_' + name + '_s'
        lines.append(f'define pcodeop {op};')
        body = ''
        if flags:
            lines.append(f'define pcodeop {op}_flags;')
            body = f'local nextFlags:4 = {op}_flags(FSR, FCR, fr, fs, ft); '
        lines.append(f':{name}.s fr, fs, ft is op2={opcode} & op1=10 & fr & fs & ft & op0=0 {{ '
                     + body + f'fr = {op}(fr, fs, ft' + (', FCR' if flags else '') + '); '
                     + ('FSR = nextFlags; ' if flags else '') + '}')
    return '\n'.join(lines) + '\n'


def conversions(source):
    # Upstream ROUND/TRUNC multiply a float by integer scale bits, FLOOR/CEIL
    # retain float result bits in an integer register, and UTRUNC's overflow
    # check shifts by 16. Do not let these stock semantics invent data values.
    # Keep the ISA conversion opaque (including rounding, saturation and flags).
    for name, opcode in (('round', 8), ('trunc', 9), ('floor', 10), ('ceil', 11), ('utrunc', 14)):
        op = 'octool_' + name + '_s'
        replacement = f'''define pcodeop {op};
define pcodeop {op}_flags;
:{name}.s ar, fs, op_t is op2={opcode} & op1=10 & ar & fs & op_t & op0=0 {{
    local nextFlags:4 = {op}_flags(FSR, FCR, fs, op_t:4);
    ar = {op}(fs, op_t:4, FCR);
    FSR = nextFlags;
}}'''
        source, count = re.subn(r':'+name+r'\.s [^\n]+\{[^}]+\}', lambda _: replacement, source)
        if count != 1:
            raise SystemExit('Unexpected stock conversion constructor: ' + name)
    return source


def postincrement(source):
    """Select the DE233-style FP address-update encoding for Granite research.

    These encodings alias the older FPU's LSIU/SSIU/LSXU/SSXU. The pinned
    configuration-specific ISA names them LSIP/SSIP/LSXP/SSXP. Never silently
    apply this processor configuration to an unrelated firmware study.
    """
    for old, new, store, indexed in (
            ('lsiu', 'lsip', False, False), ('ssiu', 'ssip', True, False),
            ('lsxu', 'lsxp', False, True), ('ssxu', 'ssxp', True, True)):
        def replace(match):
            header = match.group(0).split('{', 1)[0].replace(':'+old+' ', ':'+new+' ')
            fp = 'fr' if indexed else 'ft'
            increment = 'at' if indexed else 'ri8_i8_sh2'
            access = f'*:4 as = {fp};' if store else f'{fp} = *:4 as;'
            return header + '{\n    local next:4 = as + ' + increment + ';\n    ' + access + '\n    as = next;\n}'
        source, count = re.subn(r':'+old+r' [^\n]+\{[^}]+\}', replace, source)
        if count != 1:
            raise SystemExit('Unexpected stock update constructor: ' + old)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ghidra', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--granite-postincrement', action='store_true',
                        help='Select pinned DE233-style FP postincrement encodings for Granite Ridge')
    args = parser.parse_args()
    install = args.ghidra.resolve()
    props = (install / 'Ghidra/application.properties').read_text()
    if 'application.version=12.1.4\n' not in props:
        raise SystemExit('Requires the hash-pinned Ghidra 12.1.4 installation')
    language = install / 'Ghidra/Processors/Xtensa/data/languages'
    spec = language / 'xtensa_le.slaspec'
    original = spec.read_bytes()
    expected = {
        'xtensa_le.slaspec': 'e480837a5e3004401c9904ee486f9cf42b24cd13eefa47a7f141e995be6e1959',
        'xtensaInstructions.sinc': 'd7fcfdc28dd216ba02e5b202c1c4fd61d35885c7b525e6ded7c3ab2dfa40196d',
    }
    for filename, digest in expected.items():
        if hashlib.sha256((language / filename).read_bytes()).hexdigest() != digest:
            raise SystemExit('Unexpected upstream Xtensa source: ' + filename)
    marker = b'@include "cust.sinc"\n'
    if original.count(marker) != 1 or b'octool-fp.sinc' in original:
        raise SystemExit('Unexpected/already patched Xtensa specification')
    extension = language / 'octool-fp.sinc'
    if extension.exists():
        raise SystemExit('Refusing to replace an existing extension')
    content = definitions().encode()
    stock_instructions = language / 'xtensaInstructions.sinc'
    patched = conversions(stock_instructions.read_text())
    if args.granite_postincrement:
        patched = postincrement(patched)
    patched_instructions = patched.encode()
    extension.write_bytes(content)
    spec.write_bytes(original.replace(marker, marker + b'@include "octool-fp.sinc"\n'))
    stock_instructions.write_bytes(patched_instructions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({
        'schema': 1, 'hardware_execution': False, 'firmware_modified': False,
        'ghidra_version': '12.1.4', 'qemu_revision': QEMU_REV,
        'encoding_source': SOURCE + 'core-de233_fpu/xtensa-modules.c.inc',
        'constant_semantics_source': SOURCE + 'translate.c',
        'reference_sha256': {
            'xtensa-modules.c.inc': '70717c4060f356ad038def74241c4de76cbd57758edd19616ad449222a04d0a7',
            'translate.c': 'f06ea79f7c3332fb8910d1e5829978324cf1e04843161bbadb47b6fbb256f810',
        },
        'original_spec_sha256': hashlib.sha256(original).hexdigest(),
        'patched_spec_sha256': hashlib.sha256(spec.read_bytes()).hexdigest(),
        'extension_sha256': hashlib.sha256(content).hexdigest(),
        'original_instructions_sha256': expected['xtensaInstructions.sinc'],
        'patched_instructions_sha256': hashlib.sha256(patched_instructions).hexdigest(),
        'address_update_profile': 'granite-de233-postincrement' if args.granite_postincrement else 'stock',
        'exact_operations': ['const.s'] + (['lsip','ssip','lsxp','ssxp'] if args.granite_postincrement else []),
        'opaque_operations': [n + '.s' for n, *_ in UNARY] +
                             ['maddn.s', 'divn.s', 'round.s', 'trunc.s', 'floor.s', 'ceil.s', 'utrunc.s'],
        'limitations': ['Reserved CONST.S immediates remain undecodable',
                       'Approximation/refinement numeric results and status flags are opaque',
                       'Coprocessor exceptions are not modeled; static research only'],
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
