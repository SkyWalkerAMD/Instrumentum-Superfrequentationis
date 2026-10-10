#!/usr/bin/env python3
"""Execute AMD limit UI rows and constructor flag regions with synthetic Qt/PCI."""
import argparse
import hashlib
import io
import itertools
import json
from pathlib import Path
import re
import struct
import importlib.util
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
from unicorn.x86_const import UC_X86_REG_RBP, UC_X86_REG_R12, UC_X86_REG_R13


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value


base = module('bindings_base', 'emulate-legacy-dispatch.py')
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-limit-bindings.json'
FIXTURE_SHA = '9a889879dc0f9a5c8394499df66b0e2d838c5cb8fad476b567d7ff6bd223775b'
SETUP, TEXT, FLAG = 'limits.setup-rows', 'limits.translate-rows', 'limits.constructor-flag'
REGIONS = (
    (SETUP, '_ZN15Ui_cpufunctions7setupUiEP7QDialog', 0x7407b4, 0x740f10),
    (TEXT, '_ZN15Ui_cpufunctions13retranslateUiEP7QDialog', 0x73b1f1, 0x73b51b),
    (FLAG, '_ZN12cpufunctionsC2EP7QWidget', 0x762911, 0x762943),
    (FLAG + '.branch', '_ZN12cpufunctionsC2EP7QWidget', 0x763c11, 0x763c5d),
)
LABELS = ('PPT', 'TDC', 'EDC', 'THM', 'FMax', 'FIT')
OFFSETS = (0x158, 0x170, 0x188, 0x1a0, 0x1b8, 0x1d0)


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF identity mismatch')
    elf = ELFFile(io.BytesIO(data))
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    objects = {s['st_value']: s for s in symbols if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}

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

    audit = module('bindings_audit', 'elf-runtime-audit.py')
    parents = {row[1] for row in REGIONS}
    report = audit.inspect(path, re.compile('^(?:' + '|'.join(map(re.escape, parents)) + ')$'), True)
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, functions=[], objects={}, literals=[])
    proofs, literals = [], {}
    dec = Cs(CS_ARCH_X86, CS_MODE_64); dec.detail = True
    for label, parent, start, end in REGIONS:
        fn = next(f for f in report['selected_functions'] if parent in f['symbols'])
        assert fn['address'] <= start < end <= fn['address'] + fn['size']
        code = read(start, end - start)
        instructions = list(dec.disasm(code, start))
        assert sum(i.size for i in instructions) == len(code)
        row = dict(symbol=label, address=start, size=len(code), code_hex=code.hex(), code_sha256=hashlib.sha256(code).hexdigest(),
                   parent=parent, parent_code_sha256=fn['code_sha256'], calls=[c for c in fn['calls'] if start <= c['instruction'] < end])
        fixture['functions'].append(row)
        proofs.append(dict((k, v) for k, v in row.items() if k != 'code_hex'))
        proofs[-1]['instructions'] = [dict(address=i.address, mnemonic=i.mnemonic, operands=i.op_str) for i in instructions]
        for i in instructions:
            for op in i.operands:
                if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP:
                    continue
                address = i.address + i.size + op.mem.disp
                if address in objects:
                    obj = objects[address]
                    fixture['objects'][obj.name] = dict(address=address, size=obj['st_size'], source_initial_hex=read(address, obj['st_size']).hex())
                else:
                    text = read(address, 128).split(b'\0')[0].decode('utf-8')
                    if not text or not text.isprintable():
                        raise ValueError('unclassified constant')
                    literals[address] = text
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    ctor = next(f for f in report['selected_functions'] if '_ZN12cpufunctionsC2EP7QWidget' in f['symbols'])
    zeroing = [i for i in ctor['instructions'] if 0x761743 <= i['address'] <= 0x761753]
    return fixture, dict(source_elf_sha256=base.SOURCE_SHA, proofs=proofs, constructor_zeroing=zeroing,
                         scope='Bounded original regions; Qt, allocations and FindPciDeviceById2 are synthetic. Not full constructors.')


def load_fixture():
    data = FIXTURE.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('AMD limit binding fixture identity mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('fixture schema/source mismatch')
    return fixture


class BindingMachine(base.Machine):
    def __init__(self, fixture, inputs):
        super().__init__(fixture, inputs)
        self.this, self.ui = self.alloc(0x1100), self.alloc(0x1000)
        self.strings, self.names, self.labels, self.rows = {}, {}, {}, {}
        self.queries = []
        self.uc.mem_write(self.objects['GLOBAL_IS_GRANITE'], bytes([inputs.get('granite', 0)]))
        self.put64(self.ui + 0x138, self.alloc(16)); self.put64(self.ui + 0x140, self.alloc(16))
        self.put32(self.this + 0xf8c, 5)
        for stop in (0x740f10, 0x73b51b, 0x762943):
            self.map(stop, 1); self.uc.mem_write(stop, b'\xc3')

    def prepare(self, symbol):
        self.phase = symbol
        if symbol == FLAG:
            self.uc.reg_write(UC_X86_REG_RBP, self.this)
        else:
            self.reg('rbx', self.ui)
            self.uc.reg_write(UC_X86_REG_RBP, self.alloc(16))
            self.uc.reg_write(UC_X86_REG_R12, self.alloc(16))
            self.uc.reg_write(UC_X86_REG_R13, self.alloc(16))
            self.reg('rsp', base.STACK + 0xf000)

    def cstring(self, address):
        out = bytearray()
        for n in range(128):
            value = bytes(self.uc.mem_read(address + n, 1))
            if value == b'\0':
                return out.decode('utf-8')
            out.extend(value)
        raise ValueError('unterminated text')

    def hook(self, uc, pc, size, unused):
        if pc == {SETUP: 0x740f10, TEXT: 0x73b51b, FLAG: 0x762943}.get(self.phase):
            self.steps += 1; self.stop('region-end'); return
        super().hook(uc, pc, size, unused)

    def external(self, name):
        self.calls.append(name)
        name = {'_ZN11QHBoxLayoutC2Ev': '_ZN11QHBoxLayoutC1Ev',
                '_ZN6QLabelC2EP7QWidget6QFlagsIN2Qt10WindowTypeEE': '_ZN6QLabelC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE',
                '_ZN9QLineEditC2EP7QWidget': '_ZN9QLineEditC1EP7QWidget'}.get(name, name)
        if name == '_Z18FindPciDeviceById2jjj':
            query = [self.reg(r) for r in ('rdi', 'rsi', 'rdx')]
            assert query[0] == 0x1022 and query[2] == 0
            self.queries.append(query)
            self.ret(0 if not self.inputs.get('identity_error') and query[1] == self.inputs.get('device') and self.inputs.get('vendor') == 0x1022 else 0xffffffff)
        elif name == '_Znwm@plt':
            self.ret(self.alloc(self.reg('rdi')))
        elif name in ('_ZN7QString15fromUtf8_helperEPKci', '_ZN16QCoreApplication9translateEPKcS1_S1_i'):
            self.strings[self.reg('rdi')] = self.cstring(self.reg('rsi') if 'fromUtf8' in name else self.reg('rdx'))
            self.ret()
        elif name == '_ZN7QObject13setObjectNameERK7QString':
            self.names[self.reg('rdi')] = self.strings[self.reg('rsi')]; self.ret()
        elif name == '_ZN6QLabel7setTextERK7QString':
            self.labels[self.reg('rdi')] = self.strings[self.reg('rsi')]; self.ret()
        elif name == '_ZN10QBoxLayout9addWidgetEP7QWidgeti6QFlagsIN2Qt13AlignmentFlagEE':
            self.rows.setdefault(self.reg('rdi'), []).append(self.reg('rsi')); self.ret()
        elif name in ('_ZN11QHBoxLayoutC1Ev', '_ZN11QHBoxLayoutC2Ev', '_ZN6QLabelC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE',
                      '_ZN9QLineEditC1EP7QWidget', '_ZN7QStringD1Ev', '_ZN7QStringD2Ev', '_ZN7QWidget7setFontERK5QFont',
                      '_ZN10QBoxLayout9addLayoutEP7QLayouti', '_ZN7QWidget12setStatusTipERK7QString', '_ZN7QWidget12setWhatsThisERK7QString'):
            self.ret()
        else:
            raise ValueError('unexpected boundary: ' + name)


def investigate(fixture):
    m = BindingMachine(fixture, {})
    for phase in (SETUP, TEXT):
        m.outcome, m.steps = None, 0
        assert m.run(phase, instruction_limit=8000)['outcome'] == 'region-end'
    bindings = []
    for offset, expected in zip(OFFSETS, LABELS):
        widget = m.u64(m.ui + offset)
        layout, children = next((layout, children) for layout, children in m.rows.items() if widget in children)
        assert len(children) == 2 and children[1] == widget and m.labels[children[0]] == expected
        bindings.append(dict(ui_offset=offset, object_name=m.names[widget], label=m.labels[children[0]],
                             label_object=m.names[children[0]], layout=m.names[layout]))
    cases = []
    for vendor, device, granite, error in itertools.product((0x1022, 0x8086), (0x153a, 0x14d8, 0x14a4, 0x14b5, 0xffff), (0, 1, 2, 255), (False, True)):
        inputs = dict(vendor=vendor, device=device, granite=granite, identity_error=error)
        m = BindingMachine(fixture, inputs)
        assert m.run(FLAG)['outcome'] == 'region-end'
        flag = bytes(m.uc.mem_read(m.this + 0xf89, 1))[0]
        expected = bool(granite or (not error and vendor == 0x1022 and device in (0x14d8, 0x14a4)))
        assert flag == int(expected)
        selection = struct.unpack('<I', m.uc.mem_read(m.this + 0xf8c, 4))[0]
        assert selection == (3 if expected else 5)
        cases.append(dict(inputs=inputs, f89=flag, f8c=selection, identity_queries=m.queries))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, bindings=bindings, cases=cases,
                limits=['Qt labels/layout/name calls and PCI lookup are synthetic, but their calling instructions are original.',
                        'Constructor entry assumes the documented earlier zeroing, without running intervening code.',
                        'Label and branch recovery does not establish firmware units, board compatibility or write safety.'])


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
    print('6 original Qt row bindings; ' + str(len(result['cases'])) + ' constructor flag cases; synthetic boundaries only')


if __name__ == '__main__':
    main()
