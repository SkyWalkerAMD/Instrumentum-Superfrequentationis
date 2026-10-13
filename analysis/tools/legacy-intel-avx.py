#!/usr/bin/env python3
"""Characterize three complete original AVX helpers with synthetic MSR/vector calls."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('avx_shared', Path(__file__).with_name('legacy-intel-ratio.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
WRITERS = ('_Z13set_avxoffsetj', '_Z17set_avxoffset_390j')
READER = '_Z12rd_avxoffsetv'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-avx.json'
FIXTURE_SHA = 'a3a53341a43bead5574b401c9b1f42f4cee9d087c25add744edc90f44f155b34'


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original AVX fixture identity mismatch')
    fixture = json.loads(raw)
    if fixture['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture source mismatch')
    return fixture


class AvxMachine(shared.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.trace = []
        self.reads = 0

    def prepare(self, symbol):
        if symbol in WRITERS:
            self.reg('rdi', self.inputs['bins'])

    def external(self, name):
        self.calls.append(name)
        if name == '_Z5Wrmsrjjj':
            assert self.reg('rdi') == 0x150
            word = (self.reg('rdx') & 0xffffffff) << 32 | (self.reg('rsi') & 0xffffffff)
            self.trace.append(dict(write=True, msr=0x150, value=word))
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            assert self.reg('rdi') == 0x150
            self.reads += 1
            if self.inputs.get('supply_data', True):
                busy = self.inputs.get('busy', False) or self.reads <= self.inputs.get('busy_reads', 0)
                self.put32(self.reg('rsi'), self.inputs['raw'])
                self.put32(self.reg('rdx'), (0x80000000 if busy else 0) | self.inputs.get('status', 0))
            self.trace.append(dict(write=False, msr=0x150))
            self.ret(self.inputs.get('read_result', 1))
        elif name.startswith('_ZNSt6vectorIiSaIiEE17_M_realloc_insert'):
            obj = self.reg('rdi')
            begin, end, capacity = (self.u64(obj+n*8) for n in range(3))
            assert self.reg('rsi') == end and end == capacity
            previous = bytes(self.uc.mem_read(begin, end-begin)) if begin else b''
            value = bytes(self.uc.mem_read(self.reg('rdx'), 4))
            data = self.alloc(len(previous)+4)
            self.uc.mem_write(data, previous+value)
            for n, address in enumerate((data, data+len(previous)+4, data+len(previous)+4)):
                self.put64(obj+n*8, address)
            self.ret()
        else:
            raise ValueError('unexpected boundary: '+name)

    def result(self):
        begin, end = self.u64(self.output), self.u64(self.output+8)
        assert self.reg('rax') == self.output and end-begin == 8
        return list(struct.unpack('<2I', self.uc.mem_read(begin, 8)))


def investigate(fixture):
    rows = []
    for symbol, bins, status in itertools.product(WRITERS, list(range(65))+[0xffffffff], (0, 1)):
        inputs = dict(bins=bins, write_result=status)
        m = AvxMachine(fixture, inputs); out = m.run(symbol)
        word = (bins & 31) << 5
        if symbol == WRITERS[1]: word |= (bins & 31) << 10
        assert out['outcome'] == 'returned'
        assert m.trace == [dict(write=True, msr=0x150, value=(0x8000001b << 32)|word)]
        rows.append(dict(symbol=symbol, inputs=inputs, trace=m.trace, outcome=out['outcome']))
    patterns = [0, 0xffffffff, 0xa5c32110]+[(a << 5)|(b << 10) for a in range(32) for b in (0, 7, 31)]
    for raw, busy_reads in itertools.product(patterns, (0, 2)):
        inputs = dict(raw=raw, busy_reads=busy_reads)
        m = AvxMachine(fixture, inputs); out = m.run(READER)
        assert out['outcome'] == 'returned' and m.result() == [(raw>>5)&31, (raw>>10)&31]
        assert m.reads == busy_reads+2 and m.trace[0] == dict(write=True, msr=0x150, value=0x8000001a << 32)
        assert not any(x['write'] for x in m.trace[1:])
        rows.append(dict(symbol=READER, inputs=inputs, trace=m.trace, result=m.result(), outcome=out['outcome']))
    for status in range(256):
        inputs = dict(raw=0xa5c32110, status=status, read_result=0, write_result=0)
        m = AvxMachine(fixture, inputs); out = m.run(READER)
        assert out['outcome'] == 'returned' and m.result() == [8, 8]
        rows.append(dict(symbol=READER, inputs=inputs, result=m.result(), outcome=out['outcome']))
    for inputs in (dict(raw=0, busy=True), dict(raw=0, supply_data=False, read_result=0)):
        m = AvxMachine(fixture, inputs); out = m.run(READER, instruction_limit=1000, allow_instruction_bound=True)
        assert out['outcome'] == 'instruction-limit' and m.reads > 100
        rows.append(dict(symbol=READER, inputs=inputs, outcome=out['outcome'], reads=m.reads))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
                scope='Three complete original functions; only allowlisted instructions and synthetic MSR/vector boundaries; no hardware.',
                findings=['Query 1A returns AVX2 bits 9:5 and a second offset in bits 14:10.',
                          'Both setters send command 1B, truncate unsigned input to five bits, and clear unrelated bits.',
                          'The _390 setter copies the same value into both fields; the ordinary setter only fills bits 9:5.',
                          'Original reader ignores wrapper and firmware errors and can poll forever. Setters do not query or verify.',
                          'Platform validity and physical frequency effects are outside synthetic characterization.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys(WRITERS+(READER,))
        fixture, proof = shared.extract(args.binary)
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA '+hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    report = investigate(fixture)
    args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios']))+' original AVX scenarios; synthetic MSR only')


if __name__ == '__main__': main()
