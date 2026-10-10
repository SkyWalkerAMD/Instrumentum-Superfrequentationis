#!/usr/bin/env python3
"""Execute original client OC ratio functions with synthetic MSR boundaries only."""
import argparse
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import re
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


base = module('ratio_base', 'emulate-legacy-dispatch.py')
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-ratio.json'
FIXTURE_SHA = '75364fa078d5cee1ebcdd8f0bcef830d3bc0c1ef4049039a0846bd6bf777cbc2'
FUNCTIONS = {'_Z14Wr_150maxratioi': (True, 0), '_Z20Wr_150max_ring_ratioi': (True, 2),
             '_Z14Rd_150maxratiov': (False, 0), '_Z20Rd_150max_ring_ratiov': (False, 2)}
U32 = 0xffffffff


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
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

    audit = module('ratio_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(?:' + '|'.join(map(re.escape, FUNCTIONS)) + ')$'), True)
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, functions=[], objects={}, literals=[])
    literals = {}
    dec = Cs(CS_ARCH_X86, CS_MODE_64); dec.detail = True
    for fn in report['selected_functions']:
        symbol = next(s for s in fn['symbols'] if s in FUNCTIONS)
        code = read(fn['address'], fn['size'])
        fixture['functions'].append(dict(symbol=symbol, address=fn['address'], size=len(code), code_hex=code.hex(),
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
                elif address not in literals:
                    raise ValueError('unclassified original RIP data')
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    return fixture, dict(source_elf_sha256=base.SOURCE_SHA, functions=report['selected_functions'],
                         scope='Complete function bytes; executed paths explicitly force NVL/GNR flags to zero.')


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original ratio fixture identity mismatch')
    result = json.loads(raw)
    if result['schema'] != 1 or result['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return result


class RatioMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.writes, self.reads = [], []

    def prepare(self, symbol):
        self.reg('rdi', self.inputs.get('ratio', 59) & U32)

    def external(self, name):
        self.calls.append(name)
        if name == '_Z5Wrmsrjjj':
            if self.reg('rdi') != 0x150:
                raise ValueError('unexpected MSR')
            self.writes.append(dict(low=self.reg('rsi') & U32, high=self.reg('rdx') & U32))
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            if self.reg('rdi') != 0x150 or not self.writes:
                raise ValueError('unexpected MSR query')
            high = self.inputs.get('status', 0)
            if self.inputs.get('busy'):
                high |= 0x80000000
            data = self.inputs.get('data', 0xf3512345)
            self.put32(self.reg('rsi'), data); self.put32(self.reg('rdx'), high)
            self.reads.append(dict(low=data, high=high))
            self.ret(self.inputs.get('read_result', 1))
        elif name == 'usleep@plt':
            if self.reg('rdi') != 1000:
                raise ValueError('unexpected sleep')
            self.ret(0)
        else:
            raise ValueError('unrecognized boundary: ' + name)


def investigate(fixture):
    rows = []

    def case(symbol, inputs):
        m = RatioMachine(fixture, inputs)
        result = m.run(symbol, instruction_limit=3000, allow_instruction_bound=bool(inputs.get('busy')))
        write, domain = FUNCTIONS[symbol]
        assert result['outcome'] == ('instruction-limit' if inputs.get('busy') else 'returned')
        expected = [dict(low=0, high=0x80000010 | domain << 8)]
        if write and not inputs.get('busy'):
            expected.append(dict(low=(inputs['data'] & 0xffffff00) | (inputs['ratio'] & 255), high=0x80000011 | domain << 8))
            # The original client setter returns immediately after submission.
            assert len(m.reads) == 1
        if not write and not inputs.get('busy'):
            assert m.reg('rax') == inputs['data'] & 255
        assert m.writes == expected
        rows.append(dict(symbol=symbol, inputs=inputs, writes=m.writes, reads=len(m.reads), outcome=result['outcome'], instructions=result['steps']))

    for symbol in FUNCTIONS:
        write, _ = FUNCTIONS[symbol]
        ratios = (0, 1, 59, 85, 86, 255, 256, 257, -1, -2147483648) if write else (0,)
        for ratio, data in itertools.product(ratios, (0, U32, 0xf3512345, 0x012abcde)):
            case(symbol, dict(ratio=ratio, data=data))
        for status in (1, 3, 0xfe, 0xff):
            case(symbol, dict(ratio=59, data=0xf3512345, status=status, read_result=0, write_result=0))
        case(symbol, dict(ratio=59, data=0xf3512345, busy=True))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, scenarios=rows,
                scope='Original client ratio instructions; MSR wrappers and delay are synthetic. No NVL/GNR paths, syscalls or hardware.',
                findings=['Only data bits 7:0 are replaced; original input truncates to uint8.',
                          'Original client setters ignore status and wrapper errors, have an unbounded busy loop and no post-write verification.'])


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
            args.export_fixture.write_bytes(encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(encoded(proof))
        print('fixture SHA ' + hashlib.sha256(encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(str(len(result['scenarios'])) + ' original Intel client ratio scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
