#!/usr/bin/env python3
"""Characterize original TVB table selectors and bytes, using synthetic I/O only."""
import argparse
import hashlib
import importlib.util
import itertools
import json
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location('table_ratio', Path(__file__).with_name('legacy-intel-ratio.py'))
ratio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ratio)
base = ratio.base
READ = '_Z9Rd_octvb2i'
WRITE = '_Z13Wr_octvb_vec2St6vectorIS_IiSaIiEESaIS1_EEi'
PER_READ = '_Z9Rd_octvb3i'
PER_WRITE = '_Z13Wr_octvb_vec3St6vectorIiSaIiEEi'
FUNCTIONS = (READ, WRITE, PER_READ, PER_WRITE)
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-tvb-table.json'
FIXTURE_SHA = 'e02a3564a45d672333be561a79ff1e5cffbf508b7ebeeddd6e537e8942f7a7b7'


def extract(path):
    previous = ratio.FUNCTIONS
    try:
        ratio.FUNCTIONS = FUNCTIONS
        fixture, proof = ratio.extract(path)
    finally:
        ratio.FUNCTIONS = previous
    proof['scope'] = 'Complete original table and per-core reader/writer functions. All I/O, allocation and C++ containers are synthetic.'
    return fixture, proof


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('TVB table fixture hash mismatch')
    result = json.loads(raw)
    if result['schema'] != 1 or result['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('TVB table source mismatch')
    return result


class TableMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.writes, self.reads = [], []

    def ints(self, obj):
        start, end = self.u64(obj), self.u64(obj+8)
        if not 0 <= end-start <= 1024 or (end-start) % 4:
            raise ValueError('invalid vector size')
        return list(struct.unpack('<{}I'.format((end-start)//4), self.uc.mem_read(start, end-start))) if end > start else []

    def put_ints(self, obj, values):
        start = self.alloc(4*len(values))
        if values:
            self.uc.mem_write(start, struct.pack('<{}I'.format(len(values)), *(x & 0xffffffff for x in values)))
        for offset, value in ((0, start), (8, start+4*len(values)), (16, start+4*len(values))):
            self.put64(obj+offset, value)

    def rows(self, obj):
        return [self.ints(at) for at in range(self.u64(obj), self.u64(obj+8), 24)]

    def prepare(self, symbol):
        self.reg('rsi', self.inputs.get('target', 0))
        if symbol == WRITE:
            self.vector(self.output, [[x & 0xffffffff for x in row] for row in self.inputs['rows']])
        elif symbol == PER_WRITE:
            self.put_ints(self.output, self.inputs['rows'][0])

    def external(self, name):
        self.calls.append(name)
        if name in ('_ZN10proc_classC1Ev', '_ZN10proc_classC2Ev'):
            self.put32(self.reg('rdi')+0x2c, self.inputs.get('count', 8)); self.ret()
        elif name == '_Z5Wrmsrjjj':
            assert self.reg('rdi') == 0x150
            self.writes.append(dict(low=self.reg('rsi') & 0xffffffff, high=self.reg('rdx') & 0xffffffff))
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            assert self.reg('rdi') == 0x150 and self.writes
            data = self.inputs.get('data', 0x02644601)
            high = self.inputs.get('status', 0) | (0x80000000 if self.inputs.get('busy') else 0)
            self.put32(self.reg('rsi'), data); self.put32(self.reg('rdx'), high)
            self.reads.append(dict(low=data, high=high)); self.ret(self.inputs.get('read_result', 1))
        elif name.startswith('_ZNSt6vectorIiSaIiEE17_M_realloc_insert'):
            obj = self.reg('rdi')
            assert self.reg('rsi') == self.u64(obj+8)
            value = struct.unpack('<I', self.uc.mem_read(self.reg('rdx'), 4))[0]
            self.put_ints(obj, self.ints(obj)+[value]); self.ret()
        elif name.startswith('_ZNSt6vectorIS_IiSaIiEESaIS1_EE17_M_realloc_insert'):
            obj = self.reg('rdi')
            assert self.reg('rsi') == self.u64(obj+8)
            self.vector(obj, self.rows(obj)+[self.ints(self.reg('rdx'))]); self.ret()
        elif name == '_ZdlPvm@plt':
            assert self.reg('rdi') in self.allocations
            self.ret()
        elif name == 'usleep@plt':
            assert self.reg('rdi') == 3000
            self.ret()
        else:
            raise ValueError('unexpected boundary: '+name)


def investigate(fixture):
    cases = []

    def run(symbol, inputs):
        m = TableMachine(fixture, inputs)
        bounded = inputs.get('busy', False) and symbol != PER_WRITE
        out = m.run(symbol, instruction_limit=10000, allow_instruction_bound=bounded)
        assert out['outcome'] == ('instruction-limit' if bounded else 'returned')
        if symbol in (READ, PER_READ):
            count = inputs.get('count', 8) if symbol == READ else 1
            expected = [dict(low=0, high=0x80000024 | ((inputs.get('target', 0) & 255) << 16) |
                (n << 8 if symbol == READ else 0)) for n in range(1, count+1)]
            assert m.writes == (expected[:1] if bounded else expected)
            if not bounded:
                data = inputs.get('data', 0x02644601)
                rows = m.rows(m.output) if symbol == READ else [m.ints(m.output)]
                assert rows == [[n, data >> 24, (data >> 8) & 255, data & 255, (data >> 16) & 255]
                                for n in range(1, count+1)]
        else:
            rows = inputs['rows'][:8] if symbol == WRITE else inputs['rows'][:1]
            expected = []
            for row in rows:
                data = ((row[1] & 255) << 24) | ((min(row[2], 115) & 255) << 8) | (row[3] & 255) | ((row[4] & 255) << 16)
                high = 0x80000025 | ((inputs.get('target', 0) & 255) << 16) | ((row[0] & 255) << 8 if symbol == WRITE else 0)
                expected.extend([dict(low=data, high=high)] * (2 if symbol == WRITE else 1))
            assert m.writes == (expected[:1] if bounded else expected)
            if symbol == PER_WRITE:
                assert not m.reads
        cases.append(dict(symbol=symbol, inputs=inputs, writes=m.writes, read_count=len(m.reads),
                          outcome=out['outcome'], instructions=out['steps']))

    for symbol in (READ, PER_READ):
        for target, data in itertools.product((0, 1, 255, 256), (0, 0xffffffff, 0x02644601, 0x81fe7f23)):
            run(symbol, dict(target=target, data=data))
        for status in (1, 2, 5, 128, 255):
            run(symbol, dict(status=status, write_result=0, read_result=0))
        run(symbol, dict(busy=True))
    for symbol in (WRITE, PER_WRITE):
        for target, temperature in itertools.product((0, 1, 255, 256), (-1, 70, 115, 116, 256)):
            run(symbol, dict(target=target, rows=[[n, 0x82, temperature, 0x41, 0x64] for n in range(1, 10)]))
        run(symbol, dict(status=255, write_result=0, read_result=0, rows=[[n, 2, 70, 1, 100] for n in range(1, 9)]))
        run(symbol, dict(busy=True, rows=[[n, 2, 70, 1, 100] for n in range(1, 9)]))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, scenarios=cases,
        scope='Isolated original instructions; synthetic MSR, proc_class count and C++ vector boundaries; no hardware.',
        findings=['Table selector uses interface bits 15:8; separate per-core target uses 23:16. Neither is an OS CPU ID.',
                  'Reader vector order: selector, byte3, byte1, byte0, byte2. Original UI maps these to B bins, A temperature, A bins, B temperature.',
                  'Table setter repeats each of the first eight writes twice; per-core setter does not wait for completion.',
                  'Original setters clamp only signed temperature A to 115; all fields and target selectors truncate to eight bits.',
                  'Busy polling is unbounded; firmware and wrapper errors are ignored by the original functions.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path); p.add_argument('--export-fixture', type=Path)
    p.add_argument('--static-output', type=Path); p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.binary:
        fixture, proof = extract(args.binary)
        if args.export_fixture: args.export_fixture.write_bytes(ratio.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(ratio.encoded(proof))
        print('fixture SHA '+hashlib.sha256(ratio.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture); args.output.write_bytes(ratio.encoded(result))
    print(str(len(result['scenarios']))+' original Intel TVB table scenarios; synthetic I/O only')


if __name__ == '__main__': main()
