#!/usr/bin/env python3
"""Characterize original turbo-table byte packing, using synthetic MSR boundaries."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


shared = module('turbo_shared', 'legacy-intel-ratio.py')
WRITERS = {'_Z5Wr1ADiiiiiiii': 0x1ad, '_Z5Wr1AEiiiiiiii': 0x1ae,
           '_Z5Wr650iiiiiiii': 0x650, '_Z5Wr651iiiiiiii': 0x651}
HELPER = '_Z9get_32bithhhh'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-turbo.json'
FIXTURE_SHA = '6dfd2f93dce1f56e7de82ef44196120df027eac78462d50b86f3c46d8565a91b'


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('original turbo fixture identity mismatch')
    value = json.loads(data)
    if value['schema'] != 1 or value['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture source mismatch')
    return value


class TurboMachine(shared.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.writes = []

    def prepare(self, symbol):
        # SysV arguments 7 and 8 follow the return address. Leave room inside
        # the already mapped synthetic stack instead of writing above it.
        stack = self.reg('rsp') - 32
        self.reg('rsp', stack)
        self.put64(stack, shared.base.STOP)
        values = self.inputs['values']
        for name, value in zip(('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9'), values):
            self.reg(name, value & 0xffffffff)
        for index in (6, 7):
            self.put64(stack + (index - 5) * 8, values[index] & 0xffffffff)

    def external(self, name):
        if name != '_Z5Wrmsrjjj':
            raise ValueError('unexpected external boundary: ' + name)
        self.calls.append(name)
        self.writes.append(dict(msr=self.reg('rdi'), low=self.reg('rsi') & 0xffffffff,
                                high=self.reg('rdx') & 0xffffffff))
        self.ret(self.inputs['write_result'])


def investigate(fixture):
    rows = []
    values = (0, 1, 85, 255, 256, -1, 2147483647, -2147483648)
    patterns = [list(values)]
    for index in range(8):
        for value in values:
            pattern = list(range(60, 52, -1)); pattern[index] = value; patterns.append(pattern)
    for symbol, msr in WRITERS.items():
        for pattern in patterns:
            for write_result in (0, 1):
                inputs = dict(values=pattern, write_result=write_result)
                machine = TurboMachine(fixture, inputs)
                result = machine.run(symbol, instruction_limit=500)
                word = sum((value & 255) << (8 * index) for index, value in enumerate(pattern))
                assert result['outcome'] == 'returned' and result['return_al'] == write_result
                assert machine.writes == [dict(msr=msr, low=word & 0xffffffff, high=word >> 32)]
                assert machine.calls == ['_Z5Wrmsrjjj']
                rows.append(dict(symbol=symbol, inputs=inputs, writes=machine.writes, outcome=result['outcome']))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
                scope='Four complete original writers plus original get_32bit helper; synthetic MSR call only. Readers inspected statically, not emulated.',
                findings=['Eight parameters are truncated to bytes, first parameter in bits 7:0.',
                          'Writers submit one complete 64-bit table without a prior read, validation or readback.',
                          'The return value of the MSR wrapper is forwarded; caller error handling is outside this experiment.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys(tuple(WRITERS) + (HELPER,))
        fixture, proof = shared.extract(args.binary)
        if args.export_fixture:
            args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output:
            audit = module('turbo_audit', 'elf-runtime-audit.py')
            names = tuple(WRITERS) + (HELPER, '_Z5Rd1ADv', '_Z5Rd1AEv', '_Z5Rd650v', '_Z5Rd651v')
            proof['functions'] = audit.inspect(args.binary, re.compile('^(?:' + '|'.join(map(re.escape, names)) + ')$'), True)['selected_functions']
            proof['scope'] = 'Four complete table writers and original byte-packing helper emulated; four table readers are static evidence only.'
            args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(shared.encoded(result))
    print(str(len(result['scenarios'])) + ' original Intel turbo table scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
