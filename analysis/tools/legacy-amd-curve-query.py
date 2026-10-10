#!/usr/bin/env python3
"""Run original AMD_PBO constructor/query instructions with synthetic PCI only."""
import argparse
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value


base = module('curve_base', 'emulate-legacy-dispatch.py')
CTOR, QUERY = '_ZN7AMD_PBOC2Ev', '_ZN7AMD_PBO6get_coEii'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-curve-query.json'
FIXTURE_SHA = 'b9697d32e80989e79ff0e9c7376759682d545bf97ca1495483909671d76f9b18'
U32 = 0xffffffff
MOBILE = ((0x1022, 0x1630), (0x1002, 0x1630), (0x1002, 0x1636), (0x1022, 0x14b5), (0x1022, 0x14e8))
OLD = ((0x1022, 0x1480), (0x1022, 0x1450))
DESKTOP = ((0x1022, 0x14d8), (0x1022, 0x14a4))


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data))
    objects = {s['st_value']: s for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}

    def read(address, size):
        for segment in elf.iter_segments():
            relative = address - segment['p_vaddr']
            if segment['p_type'] != 'PT_LOAD' or relative < 0:
                continue
            if relative + size <= segment['p_filesz']:
                return data[segment['p_offset'] + relative:segment['p_offset'] + relative + size]
            if segment['p_filesz'] <= relative and relative + size <= segment['p_memsz']:
                return bytes(size)
        raise ValueError('unmapped original data')

    audit = module('curve_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(?:' + '|'.join(map(re.escape, (CTOR, QUERY))) + ')$'), True)
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, functions=[], objects={}, literals=[])
    dec = Cs(CS_ARCH_X86, CS_MODE_64); dec.detail = True
    for fn in report['selected_functions']:
        symbol = next(s for s in fn['symbols'] if s in (CTOR, QUERY))
        code = read(fn['address'], fn['size'])
        fixture['functions'].append(dict(symbol=symbol, address=fn['address'], size=len(code), code_hex=code.hex(),
                                         code_sha256=hashlib.sha256(code).hexdigest(), calls=fn['calls']))
        for instruction in dec.disasm(code, fn['address']):
            for op in instruction.operands:
                if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP:
                    continue
                address = instruction.address + instruction.size + op.mem.disp
                if address not in objects:
                    raise ValueError('unclassified original data')
                obj = objects[address]
                fixture['objects'][obj.name] = dict(address=address, size=obj['st_size'], source_initial_hex=read(address, obj['st_size']).hex())
    return fixture, dict(source_elf_sha256=base.SOURCE_SHA, functions=report['selected_functions'],
                         scope='Complete constructor and get_co; PCI wrappers, identity helpers, mutex and delay are synthetic.')


def load_fixture():
    raw = FIXTURE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('AMD curve fixture identity mismatch')
    fixture = json.loads(raw)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return fixture


class CurveMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.this = self.alloc(64)
        self.uc.mem_write(self.this, bytes([0xa5]) * 64)
        for name, key in (('GLOBAL_IS_SHIMADA', 'shimada'), ('GLOBAL_IS_GPT', 'gpt'), ('GLOBAL_IS_GRANITE', 'granite')):
            self.uc.mem_write(self.objects[name], bytes([inputs.get(key, 0)]))
        self.finds, self.pci, self.delays, self.locks = [], [], [], []
        self.index, self.polls, self.argument = 0, 0, inputs.get('stale', 0x87654321)
        self.command_sent = False

    def u32(self, offset):
        return struct.unpack('<I', self.uc.mem_read(self.this + offset, 4))[0]

    def prepare(self, symbol):
        self.reg('rdi', self.this)
        if symbol == QUERY:
            self.reg('rsi', self.inputs.get('ccd', 3) & U32)
            self.reg('rdx', self.inputs.get('core', 7) & U32)

    def external(self, name):
        self.calls.append(name)
        p = self.inputs
        if name == '_Z18FindPciDeviceById2jjj':
            query = [self.reg(r) for r in ('rdi', 'rsi', 'rdx')]
            assert query[2] == 0
            self.finds.append(query)
            match = not p.get('identity_error') and query[:2] == p.get('identity', [0x1022, 0x153a])
            self.ret(p.get('found', 0x234) if match else U32)
        elif name == '_Z13find_pci_dev2jjj':
            assert [self.reg(r) for r in ('rdi', 'rsi', 'rdx')] == [0, 0, 0]
            self.finds.append([0, 0, 0]); self.ret(p.get('fallback', 0x456))
        elif name == '_Z10FamilyTypev':
            self.ret(p.get('family', 0x1a))
        elif name in ('_Z14lock_amd_mutexv', '_Z17release_amd_mutexv'):
            self.locks.append(name); self.ret()
        elif name == 'usleep@plt':
            assert self.reg('rdi') == 2000
            self.delays.append(2000); self.ret()
        elif name in ('_Z19WritePciConfigDwordjjj', '_Z18ReadPciConfigDwordjj'):
            write = name.startswith('_Z19Write')
            bdf, offset = self.reg('rcx'), self.reg('rdx')
            assert bdf == 0 and offset in (0xb8, 0xbc)
            error = len(self.pci) == p.get('fail_at', -1)
            value = self.reg('r8') & U32 if write else 0
            row = dict(write=write, bdf=bdf, offset=offset, index=self.index, error=error)
            if write:
                row['value'] = value
                if not error:
                    if offset == 0xb8:
                        self.index = value
                    elif self.index == self.u32(12):
                        self.argument = value
                    elif self.index == self.u32(8):
                        assert value == self.u32(32)
                        self.command_sent = True
                        if p.get('response', 1) == 1:
                            self.argument = p.get('returned', 0xffffffe2)
                self.ret(0 if error else p.get('write_return', 1))
            else:
                assert offset == 0xbc
                if self.index == self.u32(4):
                    self.polls += 1
                    value = p.get('response', 1) if self.command_sent and self.polls > p.get('busy_reads', 0) else 0
                elif self.index == self.u32(12):
                    value = self.argument
                else:
                    # Failed index write can leave an unrelated register selected.
                    value = p.get('stale', 0x87654321)
                if error:
                    value = p.get('read_failure', U32)
                row['value'] = value
                self.ret(value)
            self.pci.append(row)
        else:
            raise ValueError('unexpected boundary: ' + name)

    def construct(self):
        assert self.run(CTOR)['outcome'] == 'returned'
        self.outcome, self.steps = None, 0
        return dict(response=self.u32(4), command=self.u32(8), argument=self.u32(12), query=self.u32(32),
                    device=self.u32(24), delay=self.u32(20), flags=list(self.uc.mem_read(self.this + 16, 2)),
                    gpt=self.uc.mem_read(self.this + 48, 1)[0], family=self.uc.mem_read(self.this, 1)[0])


def investigate(fixture):
    constructors, queries = [], []
    identities = MOBILE + OLD + DESKTOP + ((0x1022, 0x153a), (0x8086, 0x153a), (0xffff, 0xffff))
    for identity, shimada, gpt, granite in itertools.product(identities, (0, 1), (0, 1), (0, 1)):
        p = dict(identity=list(identity), shimada=shimada, gpt=gpt, granite=granite)
        m = CurveMachine(fixture, p); observed = m.construct()
        mobile = identity in MOBILE
        desktop = not mobile and identity not in OLD and (bool(shimada or granite) or identity in DESKTOP)
        alternate = mobile or gpt
        assert observed['argument'] == (0x3b10a88 if alternate else 0x3b10a40)
        assert observed['command'] == (0x3b10924 if shimada else 0x3b10a20 if alternate else 0x3b10524)
        assert observed['response'] == (0x3b10970 if shimada else 0x3b10a80 if alternate else 0x3b10570)
        assert observed['query'] == (0xa3 if shimada else 0xc3 if alternate else 0xd5 if desktop else 0x7c)
        assert observed['flags'] == [int(mobile), int(desktop)]
        assert observed['gpt'] == int(bool(gpt or identity == (0x1022, 0x14e8)))
        constructors.append(dict(inputs=p, observed=observed, identity_queries=m.finds))
    for flag in (2, 255):
        m = CurveMachine(fixture, dict(shimada=flag, gpt=0, granite=0, identity_error=True, family=0x11))
        observed = m.construct()
        assert observed['query'] == 0xa3 and observed['argument'] == 0x3b10a40 and observed['family'] == 0x11
        constructors.append(dict(inputs=m.inputs, observed=observed, identity_queries=m.finds))

    def query(inputs):
        p = dict(shimada=1, **inputs)
        m = CurveMachine(fixture, p); fields = m.construct()
        assert m.run(QUERY)['outcome'] == 'returned'
        assert m.locks == ['_Z14lock_amd_mutexv', '_Z17release_amd_mutexv']
        assert not any(e.get('value') == 0x50200 for e in m.pci if e['write'])
        out = m.reg('rax') & U32
        queries.append(dict(inputs=p, object=fields, pci=m.pci, polls=m.polls,
                            delay_calls=len(m.delays), returned=out, signed_return=out if out < 0x80000000 else out - (1 << 32)))
        return m, out

    for ccd, core in itertools.product((0, 3, 15, 16, -1), (0, 7, 8, 15, 255, 256, -1)):
        m, out = query(dict(ccd=ccd, core=core))
        assert out == 0xffffffe2 and m.polls == 1
        assert m.pci[3]['value'] == ((ccd << 28) | ((core & 255) << 20)) & U32
    for returned in (0, 1, 30, 0x80000000, U32, 0xfffffff0):
        _, out = query(dict(returned=returned)); assert out == returned
    for response, busy in itertools.product((0, 1, 0xfe, U32), (0, 1, 9, 10)):
        m, out = query(dict(response=response, busy_reads=busy))
        assert m.polls == (min(busy + 1, 10) if response == 1 else 10)
        assert out == (0xffffffe2 if response == 1 else 0x30700000)
    for failure in range(10):
        query(dict(fail_at=failure))
    m, out = query(dict(write_return=0))
    assert out == 0xffffffe2  # Original ignores the write wrapper's result.
    for options in (dict(identity=[0x1022, 0x1480], found=0xabcd), dict(gpt=1), dict(identity=[0x1022, 0x14e8])):
        query(options)
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, constructors=constructors, queries=queries,
                limits=['Original constructor/query instructions; identity, PCI, delay and mutex boundaries are synthetic.',
                        'Injected writes can fail before applying; failed index selection leaves the previous index.',
                        'No firmware acceptance, initialization independence, physical units or CPU-to-firmware mapping established.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path); parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        fixture, proof = extract(args.binary)
        if args.export_fixture: args.export_fixture.write_bytes(encoded(fixture))
        if args.static_output: args.static_output.write_bytes(encoded(proof))
        print('fixture SHA ' + hashlib.sha256(encoded(fixture)).hexdigest())
    else:
        fixture = load_fixture()
    result = investigate(fixture); args.output.write_bytes(encoded(result))
    print(str(len(result['constructors'])) + ' constructor paths; ' + str(len(result['queries'])) + ' query scenarios; no host hardware')


if __name__ == '__main__':
    main()
