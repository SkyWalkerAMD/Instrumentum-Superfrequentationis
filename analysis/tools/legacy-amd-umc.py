#!/usr/bin/env python3
"""Recover the original UMC field tables and execute their read-only consumer.

All PCI operations are synthetic, including the index writes required for reads.
The constructor stops before the original platform/BDF/channel search; this tool
does not establish that the layout applies to a particular CPU or memory type.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('umc_components', Path(__file__).with_name('legacy-nvl-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
base = c.base
CTOR = '_ZN7AMD_UMCC2Ev'
READ = '_ZN7AMD_UMC10Read_ValueEj'
POPULATE = '_ZN7AMD_UMC15Populate_ValuesEv'
STRING = '_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2IS3_EEPKcRKS3_.constprop.0'
HELPERS = (CTOR, READ, POPULATE, '_Z17max_valuefrombitsm', STRING)
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'analysis/fixtures/legacy-amd-umc.json'
FIXTURE_SHA = '0a4cfbda11940d92d8cc24bdde0bf48350631881359f4ae2572cb131de834be9'


def extract(elf):
    saved = c.apply.HELPERS
    try:
        c.apply.HELPERS = HELPERS
        value = c.apply.extract(elf, helper_addresses={STRING: 0x22eeb0})
    finally:
        c.apply.HELPERS = saved
    value['scope'] = ('Original AMD_UMC constructor prefix through 0x262a7e (excluded), '
                      'Read_Value, Populate_Values, mask helper and original vector/string helpers. '
                      'RW_MMIO_AMD construction and all libc/PCI leaves are synthetic. '
                      'Platform selection, channel search, refresh UI and writes are not executed.')
    return value


class UmcMachine(c.ComponentMachine):
    def __init__(self, fixture, inputs):
        previous = c.HELPERS
        try:
            c.HELPERS = HELPERS
            super().__init__(fixture, inputs)
        finally:
            c.HELPERS = previous
        self.index = None
        self.pci = []

    def prepare(self, symbol):
        c.apply.ApplyMachine.prepare(self, c.profile.LOAD)
        self.phase = 'components'
        self.entry = symbol
        self.timing_object = self.alloc(0x1000)
        self.nova_object = self.alloc(0x300)
        self.reg('rdi', self.timing_object)

    def hook(self, uc, pc, size, unused):
        if self.entry == CTOR and pc == 0x262a7e:
            self.stop('before-platform-probe')
            return
        super().hook(uc, pc, size, unused)

    def external(self, name):
        if name == 'memcmp@plt':
            length = self.reg('rdx')
            if length > 16384:
                raise ValueError('unbounded UMC string comparison')
            left = bytes(self.uc.mem_read(self.reg('rdi'), length))
            right = bytes(self.uc.mem_read(self.reg('rsi'), length))
            self.ret((left > right) - (left < right))
            return
        if name == 'usleep@plt':
            if self.reg('rdi') != 4000:
                raise ValueError('unexpected UMC delay')
            self.ret(0)
            return
        if name in ('_ZN11RW_MMIO_AMDC1Ev', '_ZN11RW_MMIO_AMDC2Ev'):
            self.ret()
            return
        if name == '_Z19WritePciConfigDwordjjj':
            bdf, offset, value = (self.reg(r) & 0xffffffff for r in ('rcx', 'rdx', 'r8'))
            if bdf != 0 or offset != 0xe0:
                raise ValueError('unexpected UMC PCI write')
            self.index = value
            self.pci.append(dict(write=True, bdf=bdf, offset=offset, value=value))
            self.ret(0)
            return
        if name == '_Z18ReadPciConfigDwordjj':
            bdf, offset = (self.reg(r) & 0xffffffff for r in ('rcx', 'rdx'))
            if bdf != 0 or offset != 0xe4 or self.index is None:
                raise ValueError('unexpected UMC PCI read')
            relative = self.index - 0x50000
            value = self.inputs.get('registers', {}).get(hex(relative), self.inputs.get('default', 0))
            self.pci.append(dict(write=False, bdf=bdf, offset=offset, index=self.index, value=value))
            self.ret(value)
            return
        super().external(name)

    def execute(self, symbol):
        self.entry, self.outcome = symbol, None
        self.reg('rdi', self.timing_object)
        self.reg('rsp', base.STACK+0xfff8)
        self.put64(self.reg('rsp'), base.STOP)
        before = self.steps
        self.uc.emu_start(self.entries[symbol], base.STOP+1, timeout=15000000, count=1000000)
        expected = 'before-platform-probe' if symbol == CTOR else 'returned'
        if self.outcome != expected:
            raise ValueError('unexpected execution boundary: '+str(self.outcome))
        return self.steps-before


def analyze(fixture, raw_fixture):
    machine = UmcMachine(fixture, {})
    machine.prepare(CTOR)
    constructor_steps = machine.execute(CTOR)
    assert not machine.pci
    obj = machine.timing_object
    fields = machine.nested(obj+0xc0, 4)
    labels = machine.strings(obj+0xd8)
    assert len(fields) == len(labels) == 212
    assert all(len(row) == 3 and row[0] % 4 == 0 and 0 <= row[1] <= row[2] < 32 for row in fields)
    groups = [machine.strings(obj+offset) for offset in range(0xf0, 0x210, 0x18)]
    assert Counter(x for group in groups for x in group) == Counter(labels)
    cases = []
    for inputs in ({'default': 0}, {'default': 0xffffffff}, {'default': 0x12345678},
                   {'registers': {hex(row[0]): (row[0]*0x01010101) & 0xffffffff for row in fields}},
                   {'registers': {'0x1204': 0xfff, '0x1208': 0xffff, '0x120c': 0x20000,
                                  '0x1210': (63 << 12) | (63 << 20)}}):
        m = UmcMachine(fixture, inputs)
        m.prepare(CTOR)
        m.execute(CTOR)
        steps = m.execute(POPULATE)
        values = m.flat(m.timing_object+0xa8, 4)
        expected = []
        for offset, low, high in fields:
            raw = inputs.get('registers', {}).get(hex(offset), inputs.get('default', 0))
            expected.append((raw >> low) & ((1 << (high-low+1))-1))
        assert values == expected
        names_to_values = {}
        for name, value in zip(labels, values):
            names_to_values.setdefault(name, value)
        output_groups = []
        for offset in (0x210, 0x228, 0x240):
            output_groups += m.nested(m.timing_object+offset, 4)
        assert output_groups == [[names_to_values[name] for name in group] for group in groups]
        reads = [row for row in m.pci if not row['write']]
        writes = [row for row in m.pci if row['write']]
        assert len(reads) == len(writes) == len({row[0] for row in fields})
        assert all(w['value'] == r['index'] for w, r in zip(writes, reads))
        cases.append(dict(inputs=inputs, instructions=steps, fields=values,
                          groups=output_groups, unique_register_reads=len(reads)))
    proof = [{key: f[key] for key in ('symbol', 'address', 'size', 'code_sha256')}
             for f in fixture['functions'] if f['symbol'] in HELPERS]
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA,
                fixture_sha256=hashlib.sha256(raw_fixture).hexdigest(), evidence=proof,
                constructor_prefix_instructions=constructor_steps, scenarios=cases,
                descriptors=[dict(index=i, name=name, offset=row[0], lsb=row[1], msb=row[2])
                             for i, (name, row) in enumerate(zip(labels, fields))],
                label_groups=groups, scope=fixture['scope'],
                duplicate_labels={k:v for k,v in Counter(labels).items() if v>1},
                limits=['No CPU/DDR generation compatibility established',
                        'No physical register access or memory timing change',
                        'Field values are legacy encodings, not universally cycles/MHz',
                        'Platform/BDF/channel selection and UI transforms remain to be recovered'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', type=Path)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.extract:
        args.fixture.write_bytes((json.dumps(extract(args.extract), indent=2)+'\n').encode('utf-8'))
    raw = args.fixture.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('UMC fixture hash mismatch')
    fixture = json.loads(raw)
    if fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('source identity mismatch')
    for f in fixture['functions']:
        if hashlib.sha256(bytes.fromhex(f['code_hex'])).hexdigest() != f['code_sha256']:
            raise ValueError('function hash mismatch')
    result = analyze(fixture, raw)
    args.output.write_bytes((json.dumps(result, indent=2)+'\n').encode('utf-8'))
    print(json.dumps(dict(fields=len(result['descriptors']), groups=len(result['label_groups']),
                         scenarios=len(result['scenarios']), output=str(args.output))))


if __name__ == '__main__':
    main()
