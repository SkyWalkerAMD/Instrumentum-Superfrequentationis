#!/usr/bin/env python3
"""Execute the original complete AMD_UMC constructor against synthetic PCI.

RW_MMIO_AMD construction, PCI lookup, TR5 detection and PCI IO are explicit
substitutes. No host PCI, MSR, memory or port IO occurs in this experiment.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('umc_fields', Path(__file__).with_name('legacy-amd-umc.py'))
u = importlib.util.module_from_spec(spec)
spec.loader.exec_module(u)


class SelectionMachine(u.UmcMachine):
    def prepare(self, symbol):
        super().prepare(symbol)
        for name, key in (('GLOBAL_IS_SHIMADA', 'shimada'), ('GLOBAL_IS_GRANITE', 'granite')):
            self.uc.mem_write(self.objects[name], bytes([int(bool(self.inputs.get(key, False)))]))
        self.queries = []
        # Record whether the constructor initializes its lane selection.
        self.put32(self.timing_object+0x94, 0xa5a5a5a5)

    def hook(self, uc, pc, size, unused):
        u.c.ComponentMachine.hook(self, uc, pc, size, unused)

    def external(self, name):
        if name == '_Z18FindPciDeviceById2jjj':
            vendor, device, ordinal = (self.reg(r) & 0xffffffff for r in ('rdi', 'rsi', 'rdx'))
            assert ordinal == 0
            self.queries.append([vendor, device, ordinal])
            self.ret(self.inputs.get('devices', {}).get(f'{vendor:04x}:{device:04x}', -1))
            return
        if name == '_Z13find_pci_dev2jjj':
            args = [self.reg(r) for r in ('rdi', 'rsi', 'rdx')]
            assert args == [0, 0, 0]
            self.queries.append(['fallback', *args])
            self.ret(self.inputs.get('fallback_bdf', 0))
            return
        if name == '_Z9is_tr5_esv':
            self.ret(int(bool(self.inputs.get('tr5', False))))
            return
        if name == '_Z19WritePciConfigDwordjjj':
            bdf, offset, value = (self.reg(r) & 0xffffffff for r in ('rcx', 'rdx', 'r8'))
            assert offset == 0xe0
            self.index = value
            self.pci.append(dict(write=True, bdf=bdf, offset=offset, value=value))
            self.ret(0)
            return
        if name == '_Z18ReadPciConfigDwordjj':
            bdf, offset = (self.reg(r) & 0xffffffff for r in ('rcx', 'rdx'))
            assert offset == 0xe4 and self.index is not None
            value = self.inputs.get('absolute_registers', {}).get(hex(self.index), self.inputs.get('default', 0))
            self.pci.append(dict(write=False, bdf=bdf, offset=offset, index=self.index, value=value))
            self.ret(value)
            return
        super().external(name)

    def execute_constructor(self):
        self.entry, self.outcome = u.CTOR, None
        self.reg('rdi', self.timing_object)
        self.reg('rsp', u.base.STACK+0xfff8)
        self.put64(self.reg('rsp'), u.base.STOP)
        self.uc.emu_start(self.entries[u.CTOR], u.base.STOP+1, timeout=15000000, count=1000000)
        if self.outcome != 'returned':
            raise ValueError('unexpected constructor boundary: '+str(self.outcome))
        obj = self.timing_object
        return dict(bdf=self.u32(obj+0x98), base=self.u32(obj+0x9c),
                    initial_offset=self.u32(obj+0xa0), lane_offset=self.u32(obj+0x94),
                    read_addresses=[r['index'] for r in self.pci if not r['write']],
                    pci_queries=self.queries, instructions=self.steps)


def analyze(fixture):
    cases = []
    # All expectations are stated by each case, never derived from machine output.
    def run(name, inputs, expected):
        machine = SelectionMachine(fixture, inputs)
        machine.prepare(u.CTOR)
        output = machine.execute_constructor()
        for key, value in expected.items():
            assert output[key] == value, (name, key, output[key], value)
        cases.append(dict(name=name, inputs=inputs, output=output, checked=expected))
    standard = dict(bdf=0, base=0x50000, initial_offset=0, lane_offset=0,
                    read_addresses=[0x50200, 0x50200, 0x50260])
    for vendor, device in ((0x1022,0x1480), (0x1022,0x1630), (0x1022,0x1450),
                           (0x1002,0x1630), (0x1002,0x1636), (0x1022,0x14b5)):
        run(f'pci-{vendor:04x}:{device:04x}', dict(devices={f'{vendor:04x}:{device:04x}':0x123}, default=1200),
            dict(standard, bdf=0x123))
    for flag in ('shimada','granite','tr5'):
        run(flag+'-first-valid', {flag:True, 'default':1200}, standard)
    run('fallback', dict(default=1200, fallback_bdf=0x456), dict(standard, bdf=0x456))
    run('initial-hole', dict(default=1200, absolute_registers={'0x50200':0xffffffff}),
        dict(standard, initial_offset=0x100000, read_addresses=[0x50200,0x150200,0x150200,0x150260]))
    run('second-frequency', dict(default=1200, absolute_registers={'0x50200':0}),
        dict(standard, base=0x150000, read_addresses=[0x50200,0x50200,0x150200,0x150260]))
    for flag in ('shimada','tr5'):
        # The original advances its selected base AFTER finding a valid value.
        run(flag+'-second-frequency', {flag:True,'default':1200,'absolute_registers':{'0x50200':0}},
            dict(standard, base=0x250000, read_addresses=[0x50200,0x50200,0x150200,0x250260]))
    run('no-frequency', dict(default=0), dict(standard, base=0xc50000,
        read_addresses=[0x50200]+[0x50200+i*0x100000 for i in range(13)]+[0xc50260]))
    run('all-ones', dict(default=0xffffffff), dict(standard, initial_offset=0x400000, base=0xc50000,
        read_addresses=[0x50200+i*0x100000 for i in range(4)]+
                       [0x450200+i*0x100000 for i in range(13)]+[0x1050260]))
    run('shimada-all-ones', dict(shimada=True, default=0xffffffff),
        dict(standard, initial_offset=0x400000, base=0x1250000,
             read_addresses=[0x50200+i*0x100000 for i in range(4)]+
                 [0x50200+i*0x100000 for i in (4,5,7,8,10,11,13,14,16,17,19,20,22)]+[0x1650260]))
    # RFC search chooses the LAST non-sentinel lane in the first nonempty block.
    run('rfc-lanes', dict(default=1200, absolute_registers={'0x50260':0x138}),
        dict(standard, lane_offset=12, read_addresses=[0x50200,0x50200,0x50260,0x50260,0x50264,0x50268,0x5026c]))
    sentinel = {hex(0x50260+block+lane):0x138 for block in range(0,0x400,0x100) for lane in range(0,16,4)}
    run('rfc-all-sentinels', dict(default=1200, absolute_registers=sentinel),
        dict(standard, lane_offset=0, read_addresses=[0x50200,0x50200,0x50260]+[int(x,16) for x in sentinel]))
    fields=json.loads((u.ROOT/'analysis/contracts/amd-umc-fields.json').read_bytes())['descriptors']
    refresh=[]
    for slot in range(16):
        m=SelectionMachine(fixture, dict(default=1200))
        m.prepare(u.CTOR); m.execute_constructor()
        delta=(slot//4)*0x100+(slot%4)*4
        m.put32(m.timing_object+0x94, delta)
        offsets=[f['offset']+(delta if f['offset'] in (0x260,0x2c0) else 0) for f in fields]
        registers={hex(0x50000+offset):(offset*0x01010101)&0xffffffff for offset in offsets}
        m.inputs['absolute_registers']=registers
        expected=[(registers[hex(0x50000+offset)]>>f['lsb'])&((1<<(f['msb']-f['lsb']+1))-1)
                  for f,offset in zip(fields,offsets)]
        passes=[]
        for iteration in range(2):
            m.pci=[]
            instructions=m.execute(u.POPULATE)
            values=m.flat(m.timing_object+0xa8,4)
            assert values==expected, (slot,iteration)
            reads=[r['index'] for r in m.pci if not r['write']]
            assert sorted(reads)==sorted(0x50000+offset for offset in set(offsets))
            passes.append(dict(instructions=instructions,read_addresses=reads,fields=values))
        refresh.append(dict(slot=slot,delta=delta,passes=passes))
    return dict(schema=1, source_elf_sha256=u.base.SOURCE_SHA, fixture_sha256=u.FIXTURE_SHA,
                scope=__doc__, scenarios=cases, refresh_scenarios=refresh,
                limitations=['Synthetic PCI discovery and return values only; not hardware compatibility evidence',
                             'RW_MMIO_AMD constructor is a stub, not part of the instruction coverage',
                             'Original search bugs are evidence, not a production selection algorithm'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    raw = u.FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != u.FIXTURE_SHA:
        raise ValueError('fixture identity mismatch')
    result = analyze(json.loads(raw))
    args.output.write_bytes((json.dumps(result, indent=2)+'\n').encode('utf-8'))
    print(json.dumps(dict(scenarios=len(result['scenarios']), output=str(args.output))))


if __name__ == '__main__':
    main()
