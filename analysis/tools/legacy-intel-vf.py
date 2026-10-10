#!/usr/bin/env python3
"""Characterize original client V/F selectors and hidden override changes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


shared = module('vf_extract', 'legacy-intel-ratio.py')
QUERY = '_Z16check_vfpt_validm'
CORE = '_Z17Wr_VFPoint_offsetii'
CACHE = '_Z22Wr_VFPoint_offset_ringii'
OVERRIDE = '_Z19percoreoverride_disv'
FUNCTIONS = (QUERY, CORE, CACHE, OVERRIDE)
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-vf.json'
FIXTURE_SHA = '0d0b237fd7eae253ffc4dccff0dd7951a275b859a8ddde18b6182238f050078d'


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('original VF fixture identity mismatch')
    result = json.loads(raw)
    if result['schema'] != 1 or result['source_elf_sha256'] != shared.base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return result


class VfMachine(shared.RatioMachine):
    def prepare(self, symbol):
        self.reg('rdi', self.inputs.get('point', 1))
        self.reg('rsi', self.inputs.get('millivolts', -50))


def investigate(fixture):
    rows = []
    for symbol in (QUERY, CORE, CACHE):
        for point in (0, 1, 8, 15, 16, 255, 256, -1):
            for millivolts in (0, -1, -50, 50, 1000):
                inputs = dict(point=point, millivolts=millivolts, data=0xf351234d)
                m = VfMachine(fixture, inputs); result = m.run(symbol)
                assert result['outcome'] == 'returned'
                selector = (point & 255) << 16 | (0x200 if symbol == CACHE else 0)
                if symbol == QUERY:
                    assert m.writes == [dict(low=0, high=0x80000010 | selector)] and result['return_al'] == 1
                else:
                    prefix = [dict(low=0, high=0x80000014), dict(low=inputs['data'] & ~8, high=0x80000015)] if symbol == CORE else []
                    assert m.writes[:-1] == prefix + [dict(low=0, high=0x80000010 | selector)]
                    assert m.writes[-1]['high'] == 0x80000011 | selector
                    assert not m.writes[-1]['low'] & 0x1fffff
                rows.append(dict(symbol=symbol, inputs=inputs, writes=m.writes, read_count=len(m.reads), outcome=result['outcome']))
        for status in (1, 3, 0xfe, 0xff):
            inputs = dict(point=15, millivolts=-50, data=0xf351234d, status=status, read_result=0, write_result=0)
            m = VfMachine(fixture, inputs); result = m.run(symbol)
            assert result['outcome'] == 'returned'
            if symbol == QUERY:
                assert result['return_al'] == 0
            else:
                assert m.writes[-1]['high'] & 255 == 0x11
            rows.append(dict(symbol=symbol, inputs=inputs, writes=m.writes, read_count=len(m.reads), outcome=result['outcome']))
        m = VfMachine(fixture, dict(point=1, busy=True))
        result = m.run(symbol, instruction_limit=3000, allow_instruction_bound=True)
        assert result['outcome'] == 'instruction-limit' and len(m.writes) == 1
        rows.append(dict(symbol=symbol, inputs=m.inputs, writes=m.writes, read_count=len(m.reads), outcome=result['outcome']))
    return dict(schema=1, source_elf_sha256=shared.base.SOURCE_SHA, scenarios=rows,
        scope='Original complete client routines; NVL flag zero. MSR wrappers are synthetic, no hardware or OS code.',
        findings=['Selectors truncate to uint8; the recovered API rejects out-of-range input.',
                  'Core setter executes query 0x14 then setting 0x15 clearing data bit 3 before accessing the VF point.',
                  'Cache setter does not call that override routine.',
                  'Original VF setters ignore completion status/wrapper failure and submit zero data bits 20:0.',
                  'Busy loops are instruction-bounded only by this analysis harness, not the original code.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        shared.FUNCTIONS = dict.fromkeys(FUNCTIONS)
        fixture, proof = shared.extract(args.binary)
        if args.export_fixture:
            args.export_fixture.write_bytes(shared.encoded(fixture))
        if args.static_output:
            audit = module('vf_static', 'elf-runtime-audit.py')
            all_names = FUNCTIONS + ('_Z10check_vfptv', '_Z15check_vfpt_ringv')
            report = audit.inspect(args.binary, re.compile('^(?:' + '|'.join(map(re.escape, all_names)) + ')$'), True)
            proof['functions'] = report['selected_functions']
            proof['scope'] = 'Four complete routines emulated; core/cache enumeration functions are static evidence only.'
            args.static_output.write_bytes(shared.encoded(proof))
        print('fixture SHA ' + hashlib.sha256(shared.encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture)
    args.output.write_bytes(shared.encoded(result))
    print(str(len(result['scenarios'])) + ' original Intel VF scenarios; synthetic MSR only')


if __name__ == '__main__':
    main()
