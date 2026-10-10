#!/usr/bin/env python3
"""Characterize original target/mode routines without executing hardware I/O."""
import argparse
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


shared = module('voltage_shared', 'legacy-intel-ratio.py')
ADAPTIVE = '_Z21Wr_Voltage_Adapt_z390i'
CACHE_OVERRIDE = '_Z28wr_voltage_manual_only_cachei'
COMBINE = '_Z9get_32bithhhh'
FUNCTIONS = (ADAPTIVE, CACHE_OVERRIDE, COMBINE)
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-voltage.json'
FIXTURE_SHA = '72f5a01e7b203f8b5098fe8425eb185f63455148495ad10c89cc1c9f03b7f3af'
STEP_ADDRESS = 0x1620af0
STEP_BYTES = bytes.fromhex('47414bb26940ef3f')
LEGACY_STEP = struct.unpack('<d', STEP_BYTES)[0]  # 0.9766129, not exactly 1000/1024


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != shared.base.SOURCE_SHA:
        raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data))
    objects = {s['st_value']: s for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}

    def read(address, size):
        for segment in elf.iter_segments():
            relative = address - segment['p_vaddr']
            if segment['p_type'] != 'PT_LOAD' or relative < 0:
                continue
            if relative + size <= segment['p_filesz']:
                return data[segment['p_offset'] + relative:segment['p_offset'] + relative + size]
            if segment['p_filesz'] <= relative and relative + size <= segment['p_memsz']:
                return bytes(size)
        raise ValueError('unmapped original data')

    audit = module('voltage_audit', 'elf-runtime-audit.py')
    # The core manual setter also changes ratio and chooses a CPU mask. Its
    # instructions are inspected, but that CPU-mapping path is not emulated.
    selected = FUNCTIONS + ('_Z9wrvoltageiii',)
    report = audit.inspect(path, re.compile('^(?:' + '|'.join(map(re.escape, selected)) + ')$'), True)
    fixture = dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, functions=[], objects={}, literals=[], constants=[])
    literals, constants = {}, {}
    dec = Cs(CS_ARCH_X86, CS_MODE_64); dec.detail = True
    for fn in report['selected_functions']:
        matches = [s for s in fn['symbols'] if s in FUNCTIONS]
        if not matches:
            continue
        code = read(fn['address'], fn['size'])
        fixture['functions'].append(dict(symbol=matches[0], address=fn['address'], size=len(code), code_hex=code.hex(),
            code_sha256=hashlib.sha256(code).hexdigest(), calls=fn['calls']))
        literals.update({row['address']: row['text'] for row in fn['literals']})
        for instruction in dec.disasm(code, fn['address']):
            for op in instruction.operands:
                if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP:
                    continue
                address = instruction.address + instruction.size + op.mem.disp
                if address in objects:
                    obj = objects[address]
                    fixture['objects'][obj.name] = dict(address=address, size=obj['st_size'], source_initial_hex=read(address, obj['st_size']).hex())
                elif address == STEP_ADDRESS and op.size == 8 and instruction.mnemonic == 'divsd':
                    raw = read(address, 8)
                    if raw != STEP_BYTES:
                        raise ValueError('unexpected original voltage step')
                    constants[address] = raw.hex()
                elif address not in literals:
                    raise ValueError('unclassified original RIP data: ' + hex(address))
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    fixture['constants'] = [dict(address=a, hex=v) for a, v in sorted(constants.items())]
    if len(fixture['functions']) != 3 or len(constants) != 1:
        raise ValueError('incomplete original extraction')
    return fixture, dict(source_elf_sha256=shared.base.SOURCE_SHA, functions=report['selected_functions'],
        scope='Adaptive and cache override plus original byte-combiner are emulated; core manual setter is static only. NVL/GNR flags zero.')


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original voltage fixture identity mismatch')
    result = json.loads(raw)
    if result['schema'] != 1 or result['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return result


class VoltageMachine(shared.RatioMachine):
    def __init__(self, fixture, inputs):
        if inputs.get('nvl') or inputs.get('gnr'):
            raise ValueError('server paths are outside this harness')
        super().__init__(fixture, inputs)
        for row in fixture['constants']:
            raw = bytes.fromhex(row['hex'])
            if row['address'] != STEP_ADDRESS or raw != STEP_BYTES:
                raise ValueError('unexpected voltage literal')
            self.map(row['address'], len(raw)); self.uc.mem_write(row['address'], raw)

    def prepare(self, symbol):
        self.reg('rdi', self.inputs.get('millivolts', 1000) & shared.U32)


def investigate(fixture):
    rows = []
    for symbol in (ADAPTIVE, CACHE_OVERRIDE):
        cases = [dict(millivolts=mv, data=data) for mv, data in itertools.product(
            (0, 1, 999, 1000, 1250, 2000, 2001, 4096, -1), (0, shared.U32, 0xf3512345, 0x012abcde))]
        cases += [dict(millivolts=1250, data=0xf3512345, status=status, read_result=0, write_result=0)
                  for status in (1, 3, 0xfe, 0xff)]
        cases.append(dict(millivolts=1250, data=0xf3512345, busy=True))
        for inputs in cases:
            m = VoltageMachine(fixture, inputs)
            result = m.run(symbol, instruction_limit=3000, allow_instruction_bound=bool(inputs.get('busy')))
            if inputs.get('busy'):
                assert result['outcome'] == 'instruction-limit' and len(m.writes) == 1
            else:
                assert result['outcome'] == 'returned'
                code = int(inputs['millivolts'] / LEGACY_STEP)
                if symbol == ADAPTIVE:
                    value = (inputs['data'] & 0xffe000ff) | ((code & 4095) << 8)
                    assert m.writes == [dict(low=0, high=0x80000010), dict(low=value, high=0x80000011),
                                        dict(low=0, high=0x80000210), dict(low=value, high=0x80000211)]
                    assert len(m.reads) == 3
                else:
                    # Original byte combiner clears bits 31:24 and takes all
                    # eight bits of the target's high byte, including offset.
                    value = (inputs['data'] & 255) | ((code & 65535) << 8) | (1 << 20)
                    assert m.writes == [dict(low=0, high=0x80000210), dict(low=value, high=0x80000211)]
                    assert len(m.reads) == 1
            rows.append(dict(symbol=symbol, inputs=inputs, writes=m.writes, reads=len(m.reads), outcome=result['outcome']))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
        scope='Original complete client routines with original floating constant and byte combiner; synthetic MSR and sleep only.',
        findings=['Adaptive changes both core and cache, preserving offset and ratio.',
                  'Cache override reconstructs the word; ordinary positive targets clear the offset.',
                  'Original conversion truncates mV / 0.9766129; the original literal differs from the documented 1000/1024 step. Out-of-range input is accepted.',
                  'Wrapper failures and firmware statuses do not prevent subsequent writes; busy loops have no internal bound.',
                  'Neither setter verifies full readback. No hardware or OS code was executed.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        fixture, proof = extract(args.binary)
        if args.export_fixture:
            args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(shared.encoded(result))
    print(str(len(result['scenarios'])) + ' original Intel voltage scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
