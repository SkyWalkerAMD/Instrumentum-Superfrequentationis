#!/usr/bin/env python3
"""Characterize Ring DownBin / Vmax setters with synthetic MSR boundaries."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('misc_tvb', Path(__file__).with_name('legacy-intel-tvb.py'))
tvb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tvb)
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-misc-ratio.json'
FIXTURE_SHA = 'dad1bb702eff3fbdbb4f8a9c3ac7e5a735c02c3ecfdf821f77af81676ac5b00e'
FUNCTIONS = {'_Z19disable_ringdownbinb': 2, '_Z12disable_vmaxb': 16}


def extract(path):
    previous = tvb.ratio.FUNCTIONS
    try:
        tvb.ratio.FUNCTIONS = FUNCTIONS
        fixture, proof = tvb.ratio.extract(path)
    finally:
        tvb.ratio.FUNCTIONS = previous
    proof['scope'] = 'Complete original bool Ring DownBin and Vmax setters. No original process launch.'
    return fixture, proof


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original miscellaneous ratio fixture identity mismatch')
    fixture = json.loads(raw)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != tvb.base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return fixture


def investigate(fixture):
    rows = []

    def case(symbol, inputs):
        machine = tvb.TvbMachine(fixture, inputs)
        busy = bool(inputs.get('busy'))
        result = machine.run(symbol, instruction_limit=3000, allow_instruction_bound=busy)
        assert result['outcome'] == ('instruction-limit' if busy else 'returned')
        expected = [dict(low=0, high=0x80000018)]
        if not busy:
            mask = FUNCTIONS[symbol]
            expected.append(dict(low=(inputs['data'] & ~mask) | (mask if inputs['argument'] else 0), high=0x80000019))
            assert len(machine.reads) == 2  # Neither setter checks SET completion.
        assert machine.writes == expected
        rows.append(dict(symbol=symbol, inputs=inputs, writes=machine.writes, reads=len(machine.reads),
                         outcome=result['outcome'], instructions=result['steps']))

    for symbol in FUNCTIONS:
        for data in [0, 0xffffffff, 0xa5c32110, 0x87654321] + [1 << bit for bit in range(32)]:
            for argument in (0, 1):
                case(symbol, dict(data=data, argument=argument))
        for status in (1, 5, 0xfe, 0xff):
            case(symbol, dict(data=0xa5c32110, argument=1, status=status, read_result=0, write_result=0))
        case(symbol, dict(data=0xa5c32110, argument=1, busy=True))
        case(symbol, dict(data=0xa5c32110, argument=1, write_busy=True))
    return dict(schema=1, source_elf_sha256=tvb.base.SOURCE_SHA, scenarios=rows,
                scope='Isolated original instructions with synthetic MSR/delay boundaries; no hardware or syscalls.',
                findings=['True sets Ring DownBin disable bit 1 or Vmax disable bit 4; false clears it. Other 31 bits are preserved.',
                          'Original setters ignore firmware and wrapper failures, poll GET without a bound, and never verify SET completion/readback.',
                          'Production scope adds Ring DownBin only on 06_B7. Vmax runtime behavior on B7 remains unvalidated.'])


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
            args.export_fixture.write_bytes(tvb.ratio.encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(tvb.ratio.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(tvb.ratio.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(tvb.ratio.encoded(result))
    print(str(len(result['scenarios'])) + ' original miscellaneous ratio scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
