#!/usr/bin/env python3
"""Characterize original cache-ratio side effects using synthetic MSR calls."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('uncore_shared', Path(__file__).with_name('legacy-intel-ratio.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
WRITER = '_Z14Wr_Ratio_cachei'
HELPER = '_Z9get_32bithhhh'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-uncore.json'
FIXTURE_SHA = 'ee0cb4a9a4168c166d7875cf0f895d67d9de8d93ad98528f11d7a9f06e433193'


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('original uncore fixture identity mismatch')
    result = json.loads(data)
    if result['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture source mismatch')
    return result


class UncoreMachine(shared.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.trace = []

    def prepare(self, symbol):
        self.reg('rdi', self.inputs['ratio'] & 0xffffffff)

    def external(self, name):
        self.calls.append(name)
        if name == 'usleep@plt':
            assert self.reg('rdi') == 1000
            self.ret(0)
            return
        address = self.reg('rdi')
        assert address in (0x150, 0x620), hex(address)
        if name == '_Z5Wrmsrjjj':
            value = (self.reg('rdx') & 0xffffffff) << 32 | (self.reg('rsi') & 0xffffffff)
            self.trace.append(dict(write=True, msr=address, value=value))
            self.ret(self.inputs.get('result', 1))
        elif name == '_Z5RdmsrjPjS_':
            value = self.inputs['mailbox'] if address == 0x150 else self.inputs['uncore']
            if address == 0x150 and self.inputs.get('busy'):
                value |= 1 << 63
            self.put32(self.reg('rsi'), value)
            self.put32(self.reg('rdx'), value >> 32)
            self.trace.append(dict(write=False, msr=address, value=value))
            self.ret(self.inputs.get('result', 1))
        else:
            raise ValueError('unexpected boundary: ' + name)


def investigate(fixture):
    rows = []
    for ratio, word, result in itertools.product((0, 1, 45, 127, 128, 255, 256, -1),
                                                (0, 0xffffffffffffffff, 0x9876543210fe882d), (0, 1)):
        inputs = dict(ratio=ratio, uncore=word, mailbox=0x012abcde, result=result)
        m = UncoreMachine(fixture, inputs)
        out = m.run(WRITER)
        byte = ratio & 255
        assert out['outcome'] == 'returned'
        assert m.trace == [dict(write=True, msr=0x150, value=0x8000021000000000),
                           dict(write=False, msr=0x150, value=0x012abcde),
                           dict(write=False, msr=0x150, value=0x012abcde),
                           dict(write=True, msr=0x150, value=0x80000211012abc00 | byte),
                           dict(write=False, msr=0x620, value=word),
                           dict(write=True, msr=0x620, value=(word & ~0xffff) | byte << 8 | byte)]
        rows.append(dict(inputs=inputs, outcome=out['outcome'], trace=m.trace))
    m = UncoreMachine(fixture, dict(ratio=45, uncore=0, mailbox=0, busy=True))
    out = m.run(WRITER, instruction_limit=1000, allow_instruction_bound=True)
    assert out['outcome'] == 'instruction-limit' and all(t['msr'] == 0x150 for t in m.trace)
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
                busy_loop=dict(outcome=out['outcome'], instructions=out['steps']),
                scope='Complete original Wr_Ratio_cache and get_32bit instructions; synthetic MSR/delay boundaries; platform flags zero. No hardware.',
                findings=['Original setter changes cache OC mailbox ratio before MSR 620.',
                          'Both low bytes of MSR 620 become uint8(input); bits 7 and 15 are overwritten too.',
                          'Original ignores wrapper returns, spins indefinitely on busy, and does not read back settings.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path)
    p.add_argument('--export-fixture', type=Path)
    p.add_argument('--static-output', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys((WRITER, HELPER))
        fixture, proof = shared.extract(args.binary)
        proof['scope'] = 'Original cache-ratio writer and byte helper. Runtime experiment keeps platform flags zero.'
        if args.export_fixture:
            args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(shared.encoded(result))
    print(str(len(result['scenarios'])) + ' original cache-ratio scenarios plus bounded busy-loop experiment')


if __name__ == '__main__':
    main()
