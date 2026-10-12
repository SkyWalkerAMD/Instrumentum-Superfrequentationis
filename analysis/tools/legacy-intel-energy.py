#!/usr/bin/env python3
"""Execute original MSR 1FC energy helpers at synthetic MSR boundaries."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('energy_shared', Path(__file__).with_name('legacy-intel-ratio.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
WRITERS = {'_Z12EE_TURBO_DISb': 19, '_Z7RTH_DISb': 20}
READER = '_Z18Rd_POWER_CTL_Tunerv'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-energy.json'
FIXTURE_SHA = '72eda316b8b03f3fb435ff60da6eacaef27d6f5b2595c7cd49b3595b8049293c'


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('original energy fixture identity mismatch')
    result = json.loads(data)
    if result['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture source mismatch')
    return result


class EnergyMachine(shared.base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.trace = []

    def prepare(self, symbol):
        self.reg('rdi', self.inputs.get('disabled', 0))

    def external(self, name):
        self.calls.append(name)
        assert self.reg('rdi') == 0x1fc
        if name == '_Z5RdmsrjPjS_':
            # Error tests explicitly distinguish a failed wrapper leaving the
            # outputs untouched from one which supplies a word despite error.
            if self.inputs.get('supply_data', True):
                self.put32(self.reg('rsi'), self.inputs['raw'])
                self.put32(self.reg('rdx'), self.inputs['raw'] >> 32)
            self.trace.append(dict(write=False, msr=0x1fc))
            self.ret(self.inputs.get('read_result', 1))
        elif name == '_Z5Wrmsrjjj':
            word = (self.reg('rdx') & 0xffffffff) << 32 | (self.reg('rsi') & 0xffffffff)
            self.trace.append(dict(write=True, msr=0x1fc, value=word))
            self.ret(self.inputs.get('write_result', 1))
        else:
            raise ValueError('unexpected boundary: ' + name)


def investigate(fixture):
    rows = []
    patterns = [0, (1 << 64)-1, 0xa5c3211098600243] + [1 << bit for bit in range(64)]
    for symbol, bit in WRITERS.items():
        for raw, disabled in itertools.product(patterns, (0, 1)):
            inputs = dict(raw=raw, disabled=disabled)
            machine = EnergyMachine(fixture, inputs)
            out = machine.run(symbol)
            assert out['outcome'] == 'returned'
            expected = (raw & ~(1 << bit)) | disabled << bit
            assert machine.trace == [dict(write=False, msr=0x1fc), dict(write=True, msr=0x1fc, value=expected)]
            rows.append(dict(symbol=symbol, inputs=inputs, trace=machine.trace, outcome=out['outcome']))
        for read_result, write_result, supply_data, disabled in itertools.product((0, 1), (0, 1), (False, True), (0, 1)):
            inputs = dict(raw=0xa5c3211098600243, disabled=disabled, read_result=read_result,
                          write_result=write_result, supply_data=supply_data)
            machine = EnergyMachine(fixture, inputs); out = machine.run(symbol)
            raw = inputs['raw'] if supply_data else 0
            assert out['outcome'] == 'returned'
            assert machine.trace == [dict(write=False, msr=0x1fc),
                                     dict(write=True, msr=0x1fc, value=(raw & ~(1 << bit)) | disabled << bit)]
            rows.append(dict(symbol=symbol, inputs=inputs, trace=machine.trace, outcome=out['outcome']))
    for raw, result in itertools.product(patterns, (0, 1)):
        inputs = dict(raw=raw, read_result=result)
        machine = EnergyMachine(fixture, inputs); out = machine.run(READER)
        assert out['outcome'] == 'returned' and machine.reg('rax') == raw
        assert machine.trace == [dict(write=False, msr=0x1fc)]
        rows.append(dict(symbol=READER, inputs=inputs, trace=machine.trace, value=machine.reg('rax'), outcome=out['outcome']))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
                scope='Three complete original functions; MSR calls are synthetic. No original process or hardware access.',
                findings=['Bits 19/20 are disable bits; valid bool input preserves all other 63 bits.',
                          'Original setters ignore read/write wrapper results and do not verify readback.',
                          'A failed read leaving outputs untouched can cause the original setters to zero other controls.',
                          'Reader reconstructs all 64 bits but ignores wrapper status; untouched error output is not characterized.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys(tuple(WRITERS) + (READER,))
        fixture, proof = shared.extract(args.binary)
        proof['scope'] = 'Complete original EE_TURBO_DIS, RTH_DIS and Rd_POWER_CTL_Tuner functions.'
        if args.export_fixture:
            args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    report = investigate(fixture)
    args.output.write_bytes(shared.encoded(report))
    print(str(len(report['scenarios'])) + ' original Intel energy scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
