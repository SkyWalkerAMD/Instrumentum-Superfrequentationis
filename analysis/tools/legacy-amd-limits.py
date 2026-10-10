#!/usr/bin/env python3
"""Execute the original six-field AMD limits slot with synthetic Qt/SMU only.

This characterizes routing and integer conversion, not physical-unit truth or
hardware compatibility. No Qt parser, firmware, syscall or device is executed.
"""
import argparse
import copy
import hashlib
import io
import itertools
import json
from pathlib import Path
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import importlib.util


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    out = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(out)
    return out


mp1 = module('limits_mp1', 'legacy-amd-mp1.py')
SLOT = '_ZN12cpufunctions29on_per_ccx_oc_apply_4_clickedEv'
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-limits.json'
FIXTURE_SHA = 'da643658f5a8bcab669981b19a1f5060c47670b9db55171e6aadd02f7529bfca'
OFFSETS = (0x158, 0x170, 0x188, 0x1a0, 0x1b8, 0x1d0)
U32 = 0xffffffff


def encoded(obj):
    return (json.dumps(obj, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != mp1.base.SOURCE_SHA:
        raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data))
    objects = {s['st_value']: s for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}

    def read(address, size):
        for segment in elf.iter_segments():
            relative = address - segment['p_vaddr']
            if segment['p_type'] == 'PT_LOAD' and 0 <= relative and relative + size <= segment['p_filesz']:
                return data[segment['p_offset'] + relative:segment['p_offset'] + relative + size]
            if segment['p_type'] == 'PT_LOAD' and segment['p_filesz'] <= relative and relative + size <= segment['p_memsz']:
                return bytes(size)
        raise ValueError('unmapped file-backed original data')

    audit = module('limits_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^' + re.escape(SLOT) + '$'), True)
    fn = report['selected_functions'][0]
    code = read(fn['address'], fn['size'])
    fixture = dict(schema=1, source_elf_sha256=report['sha256'], mp1_fixture_sha256=mp1.FIXTURE_SHA,
                   functions=[dict(symbol=SLOT, address=fn['address'], size=len(code), code_hex=code.hex(),
                                   code_sha256=hashlib.sha256(code).hexdigest(), calls=fn['calls'])], objects={}, literals=[])
    dec = Cs(CS_ARCH_X86, CS_MODE_64)
    dec.detail = True
    literals = {}
    for instruction in dec.disasm(code, fn['address']):
        for operand in instruction.operands:
            if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                continue
            address = instruction.address + instruction.size + operand.mem.disp
            if address in objects:
                obj = objects[address]
                fixture['objects'][obj.name] = dict(address=address, size=obj['st_size'], source_initial_hex=read(address, obj['st_size']).hex())
            else:
                text = read(address, 128).split(b'\0')[0].decode('utf-8')
                if not text or not text.isprintable():
                    raise ValueError('unexpected original constant')
                literals[address] = text
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    return fixture, fn


def combined(fixture):
    if fixture['mp1_fixture_sha256'] != mp1.FIXTURE_SHA:
        raise ValueError('MP1 dependency changed')
    result = copy.deepcopy(mp1.load_fixture())
    result['functions'].extend(fixture['functions'])
    result['objects'].update(fixture['objects'])
    result['literals'].extend(fixture['literals'])
    return result


class LimitsMachine(mp1.Mp1Machine):
    def __init__(self, fixture, inputs):
        super().__init__(combined(fixture), dict(inputs, object_flag=inputs['f88'], profile='shimada'))
        self.uc.mem_write(self.this + 0xf89, bytes([inputs['f89']]))
        ui = self.alloc(0x1000)
        self.put64(self.this + 0x1008, ui)
        self.widgets = {}
        for index, offset in enumerate(OFFSETS):
            widget = self.alloc(16)
            self.put64(ui + offset, widget)
            self.widgets[widget] = index
        self.current = None

    def prepare(self, symbol):
        if symbol != SLOT:
            raise ValueError('unexpected entry')
        self.phase = symbol
        self.reg('rdi', self.this)

    def hook(self, uc, pc, size, unused):
        if pc in (self.entries[mp1.MP1], self.entries[mp1.transport.CMD]):
            self.steps += 1
            name = 'MP1_C2PMSG' if pc == self.entries[mp1.MP1] else 'smu_cmd2'
            self.command_calls.append(dict(transport=name, command=self.reg('rdi'), argument=self.reg('rsi')))
            self.put32(self.reg('rdx'), self.inputs.get('reply', 1))
            self.ret(self.inputs.get('reply', 1))
            return
        super().hook(uc, pc, size, unused)

    def external(self, name):
        if name == '_ZNK9QLineEdit4textEv':
            self.current = self.widgets[self.reg('rsi')]
            descriptor = self.alloc(32)
            self.put32(descriptor, U32)
            self.put32(descriptor + 4, int(self.inputs['fields'][self.current] is not None))
            self.put64(self.reg('rdi'), descriptor)
            self.qt_reads += 1
            self.ret()
        elif name == '_ZNK7QString6toUIntEPbi':
            if self.reg('rsi') or self.reg('rdx') != 10:
                raise ValueError('changed parser contract')
            value = self.inputs['fields'][self.current]
            self.conversions.append(dict(ui_offset=OFFSETS[self.current], parsed_u32=value))
            self.ret(value)
        else:
            super().external(name)


def expected(inputs):
    out = []
    mobile = bool(inputs['f88'] or inputs['gpt'])
    second = bool(inputs['f89'])
    for index, value in enumerate(inputs['fields']):
        if value is None:
            continue
        value = (value * 1000 if index < 3 else value) & U32
        transport = 'MP1_C2PMSG'
        if index < 4:
            commands = ((0x31, 0x32, 0x33, 0x34), (0x39, 0x3a), (0x3a, 0x3b), (0x37, 0x38))[index] if mobile else ((0x56 if second else 0x53) + index,)
        elif index == 4:
            commands = () if mobile else (0x70 if second else 0x6d,)
        else:
            commands = (0x5b if second else 0x2f,)
            if not second:
                transport = 'smu_cmd2'
        out += [dict(transport=transport, command=command, argument=value) for command in commands]
    return out


def investigate(fixture):
    scenarios = []

    def case(inputs):
        machine = LimitsMachine(fixture, inputs)
        result = machine.run(SLOT, instruction_limit=20000)
        assert result['outcome'] == 'returned', result
        assert machine.command_calls == expected(inputs), (inputs, machine.command_calls, expected(inputs))
        assert 'Applied!' in result['qt_texts']
        assert not machine.pci and not machine.identity_reads and not machine.trace
        scenarios.append(dict(inputs=inputs, calls=machine.command_calls, conversions=machine.conversions,
                              feedback=result['qt_texts'], instructions=result['steps']))

    for flags in itertools.product((0, 1), repeat=3):
        for mask in range(64):
            case(dict(f88=flags[0], f89=flags[1], gpt=flags[2],
                      fields=[value if mask & (1 << index) else None for index, value in enumerate((400, 280, 500, 95, 5400, 100))]))
        for index, value in itertools.product(range(6), (0, 1, 4294967, 4294968, 0x7fffffff, 0x80000000, U32)):
            fields = [None] * 6
            fields[index] = value
            case(dict(f88=flags[0], f89=flags[1], gpt=flags[2], fields=fields))
        for reply in (0, 0xfe, 0xff, U32):
            case(dict(f88=flags[0], f89=flags[1], gpt=flags[2], fields=[400, 280, 500, 95, 5400, 100], reply=reply))
    for flag in (2, 255):
        case(dict(f88=0, f89=flag, gpt=0, fields=[1] * 6))
    return dict(schema=1, source_elf_sha256=mp1.base.SOURCE_SHA, scope=__doc__, scenarios=scenarios,
                limits=['Fields identified by Ui offsets only; labels/units and target firmware are not inferred.',
                        'Both firmware entry points and Qt parsing are synthetic boundaries.',
                        'Constructor derivation of the second object flag is outside this slot experiment.',
                        'No production limits control is enabled from these synthetic inputs.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--static-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        fixture, static = extract(args.binary)
        if args.export_fixture:
            args.export_fixture.write_bytes(encoded(fixture))
        if args.static_output:
            args.static_output.write_bytes(encoded(static))
    else:
        raw = FIXTURE.read_bytes()
        if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
            raise ValueError('fixture identity mismatch')
        fixture = json.loads(raw)
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(json.dumps(dict(scenarios=len(result['scenarios']), fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest())))


if __name__ == '__main__':
    main()
