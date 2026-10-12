#!/usr/bin/env python3
"""Characterize original TVB disable bits with synthetic MSR/vector boundaries."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('tvb_ratio_extractor', Path(__file__).with_name('legacy-intel-ratio.py'))
ratio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ratio)
base = ratio.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-tvb.json'
FIXTURE_SHA = 'ece8bff3f59a5b284e15fd63af6e93729093418b75169a357265b8dfeffa998a'
CLIP = '_Z12set_tvb_clipb'
VOLT = '_Z16disable_volt_optb'
READ = '_Z13Rd_Misc_Ratiov'
FUNCTIONS = (CLIP, VOLT, READ)


def extract(path):
    previous = ratio.FUNCTIONS
    try:
        ratio.FUNCTIONS = FUNCTIONS
        fixture, proof = ratio.extract(path)
    finally:
        ratio.FUNCTIONS = previous
    proof['scope'] = 'Complete original TVB clipping setter, voltage optimization disable setter and miscellaneous turbo query; no original process launch.'
    return fixture, proof


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original TVB fixture identity mismatch')
    fixture = json.loads(raw)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return fixture


class TvbMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.writes, self.reads, self.bits = [], [], []

    def prepare(self, symbol):
        if symbol != READ:
            self.reg('rdi', self.inputs.get('argument', 0))

    def external(self, name):
        self.calls.append(name)
        if name == '_Z5Wrmsrjjj':
            if self.reg('rdi') != 0x150:
                raise ValueError('unexpected MSR')
            self.writes.append(dict(low=self.reg('rsi') & 0xffffffff, high=self.reg('rdx') & 0xffffffff))
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            if self.reg('rdi') != 0x150 or not self.writes:
                raise ValueError('unexpected MSR query')
            high = self.inputs.get('status', 0)
            if self.inputs.get('busy') or (self.inputs.get('write_busy') and len(self.writes) == 2):
                high |= 0x80000000
            data = self.inputs.get('data', 0xa5c32110)
            self.put32(self.reg('rsi'), data); self.put32(self.reg('rdx'), high)
            self.reads.append(dict(low=data, high=high))
            self.ret(self.inputs.get('read_result', 1))
        elif name == 'usleep@plt':
            if self.reg('rdi') != 1000:
                raise ValueError('unexpected delay')
            self.ret(0)
        elif name == '_ZNSt6vectorIbSaIbEE13_M_insert_auxESt13_Bit_iteratorb.isra.0':
            self.bits.append(bool(self.reg('rcx'))); self.ret(0)
        elif name == '_ZNSt6vectorIbSaIbEE9push_backEb':
            self.bits.append(bool(self.reg('rsi'))); self.ret(0)
        else:
            raise ValueError('unrecognized boundary: ' + name)


def investigate(fixture):
    rows = []

    def case(symbol, inputs):
        machine = TvbMachine(fixture, inputs)
        bounded = bool((inputs.get('busy') and symbol != READ) or (inputs.get('write_busy') and symbol == CLIP))
        result = machine.run(symbol, instruction_limit=3000, allow_instruction_bound=bounded)
        assert result['outcome'] == ('instruction-limit' if bounded else 'returned')
        expected = [dict(low=0, high=0x80000018)]
        if symbol == READ:
            assert machine.bits == [bool(inputs['data'] & (1 << bit)) for bit in (1, 2, 3, 4)]
            assert len(machine.reads) == 1  # Original ignores the busy bit.
        elif not inputs.get('busy'):
            mask = 4 if symbol == CLIP else 8
            disabled = not inputs['argument'] if symbol == CLIP else bool(inputs['argument'])
            expected.append(dict(low=(inputs['data'] & ~mask) | (mask if disabled else 0), high=0x80000019))
            if not inputs.get('write_busy'):
                assert len(machine.reads) == (3 if symbol == CLIP else 2)
        assert machine.writes == expected
        rows.append(dict(symbol=symbol, inputs=inputs, writes=machine.writes, reads=len(machine.reads),
                         bits=machine.bits, outcome=result['outcome'], instructions=result['steps']))

    for symbol in FUNCTIONS:
        for data, argument in itertools.product((0, 0xffffffff, 4, 8, 12, 0xa5c32110), (0,) if symbol == READ else (0, 1)):
            case(symbol, dict(data=data, argument=argument))
        for status in (1, 3, 5, 0xff):
            case(symbol, dict(data=0xa5c32110, argument=1, status=status, read_result=0, write_result=0))
        case(symbol, dict(data=0xa5c32110, argument=1, busy=True))
        if symbol != READ:
            case(symbol, dict(data=0xa5c32110, argument=1, write_busy=True))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, scenarios=rows,
                scope='Original instructions with synthetic MSR/vector/delay boundaries only; no syscalls or hardware.',
                findings=['Clipping enabled clears bit 2; voltage optimization disabled sets bit 3; other data bits are preserved.',
                          'Original query returns disable bits 1,2,3,4 without checking busy or status.',
                          'Original setters ignore firmware/wrapper failures; clipping polls without a bound, voltage setter does not wait for completion; neither queries readback.'])


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
            args.export_fixture.write_bytes(ratio.encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(ratio.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(ratio.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(ratio.encoded(result))
    print(str(len(result['scenarios'])) + ' original Intel TVB scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
