#!/usr/bin/env python3
"""Characterize complete BCLK-aware voltage helpers with synthetic MSR calls."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('bclk_shared', Path(__file__).with_name('legacy-intel-ratio.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
READER = '_Z10bclk_awarev'
WRITER = '_Z13wr_bclk_awareb'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-bclk-voltage.json'
FIXTURE_SHA = 'df51fe524418ac686f64ed506d221dc5d9f573de0307d1a56ba712c5a4764f57'


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original BCLK voltage fixture identity mismatch')
    value = json.loads(raw)
    if value['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture source mismatch')
    return value


class BclkMachine(shared.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.trace = []
        self.command = 0
        self.reads = {0x14: 0, 0x15: 0}

    def prepare(self, symbol):
        if symbol == WRITER:
            self.reg('rdi', self.inputs['enabled'])

    def external(self, name):
        self.calls.append(name)
        if name == '_Z5Wrmsrjjj':
            assert self.reg('rdi') == 0x150
            low, high = self.reg('rsi') & 0xffffffff, self.reg('rdx') & 0xffffffff
            assert high in (0x80000014, 0x80000015)
            self.command = high & 255
            self.trace.append(dict(write=True, msr=0x150, low=low, high=high))
            self.ret(self.inputs.get('write_result', 1))
        elif name == '_Z5RdmsrjPjS_':
            assert self.reg('rdi') == 0x150 and self.command
            self.reads[self.command] += 1
            if self.inputs.get('supply_data', True):
                busy = (self.command == self.inputs.get('busy_command') or
                        self.reads[self.command] <= self.inputs.get('busy_reads', 0))
                raw = self.inputs['raw']
                # Original performs an extra query read after observing completion.
                if self.command == 0x14 and self.reads[0x14] > self.inputs.get('busy_reads', 0) + 1:
                    raw = self.inputs.get('extra_read_raw', raw)
                self.put32(self.reg('rsi'), raw)
                self.put32(self.reg('rdx'), (0x80000000 if busy else 0) | self.inputs.get('status', 0))
            self.trace.append(dict(write=False, msr=0x150, command=self.command))
            self.ret(self.inputs.get('read_result', 1))
        else:
            raise ValueError('unexpected boundary: ' + name)


def investigate(fixture):
    rows = []

    def case(symbol, inputs):
        machine = BclkMachine(fixture, inputs)
        bounded = bool(inputs.get('busy_command')) or not inputs.get('supply_data', True)
        result = machine.run(symbol, instruction_limit=1500, allow_instruction_bound=bounded)
        writes = [r for r in machine.trace if r['write']]
        expected = [dict(write=True, msr=0x150, low=0, high=0x80000014)]
        raw = inputs.get('extra_read_raw', inputs['raw'])
        submits = symbol == WRITER and inputs.get('busy_command') != 0x14 and inputs.get('supply_data', True)
        if submits:
            expected.append(dict(write=True, msr=0x150, low=(raw & ~4) | ((inputs['enabled'] & 255) << 2), high=0x80000015))
        assert writes == expected
        assert result['outcome'] == ('instruction-limit' if bounded else 'returned')
        if not bounded:
            assert machine.reads[0x14] == inputs.get('busy_reads', 0) + 2
            assert machine.reads[0x15] == (inputs.get('busy_reads', 0) + 1 if symbol == WRITER else 0)
            if symbol == READER:
                assert machine.reg('rax') == ((raw >> 2) & 1)
        rows.append(dict(symbol=symbol, inputs=inputs, writes=writes, reads=machine.reads,
                         outcome=result['outcome'], returned_bit=machine.reg('rax') if symbol == READER and not bounded else None))

    patterns = [0, 0xffffffff, 0xa5c32110] + [1 << bit for bit in range(32)] + [0xffffffff ^ (1 << bit) for bit in range(32)]
    for raw, busy in itertools.product(patterns, (0, 2)):
        case(READER, dict(raw=raw, busy_reads=busy))
        for enabled in (0, 1):
            case(WRITER, dict(raw=raw, busy_reads=busy, enabled=enabled))
    for symbol, status in itertools.product((READER, WRITER), range(256)):
        case(symbol, dict(raw=0xa5c32110, enabled=1, status=status, read_result=0, write_result=0))
    for symbol in (READER, WRITER):
        case(symbol, dict(raw=0, extra_read_raw=0xffffffff, enabled=0))
        case(symbol, dict(raw=0, enabled=1, busy_command=0x14))
        case(symbol, dict(raw=0, enabled=1, supply_data=False, read_result=0))
    case(WRITER, dict(raw=0, enabled=1, busy_command=0x15))
    # Noncanonical bool values are ABI-invalid; characterize, never permit them in the new API.
    for enabled in (2, 127, 255, 256, 0xffffffff):
        case(WRITER, dict(raw=0, enabled=enabled))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
                scope='Two complete original functions; allowlisted instructions and synthetic MSR boundaries only; no hardware.',
                findings=['Query 14 / set 15 on MSR 150. Bit 2 is positive enable; canonical bool preserves all other bits.',
                          'Original performs an extra query read, ignores wrapper and firmware errors, and can poll forever.',
                          'Original setter polls completion but does not independently query and compare readback.',
                          'A noncanonical bool can modify adjacent bits. Public input must be exactly 0 or 1.',
                          'Processor applicability and voltage effects require separate evidence.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys((READER, WRITER))
        fixture, proof = shared.extract(args.binary)
        proof['scope'] = 'Complete original BCLK-aware voltage functions, all bytes and direct calls.'
        if args.export_fixture: args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output: args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    report = investigate(fixture)
    args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios'])) + ' original BCLK voltage scenarios; synthetic MSR only')


if __name__ == '__main__': main()
