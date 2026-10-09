#!/usr/bin/env python3
"""Original raw-memory slot and NVL query, with synthetic mailbox/mapping only."""
import argparse
import copy
import hashlib
import io
import itertools
import json
from pathlib import Path
import platform
import re
import struct

from elftools.elf.elffile import ELFFile
import unicorn

import importlib.util


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


ngu = module('client_ngu', 'legacy-intel-ngu.py')
base = ngu.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-mmio-clients.json'
FIXTURE_SHA = '2b317894c2c83a9184622cf318a9b17cb1d941d3d66d1596c725319c758b5a69'
RAW = '_ZN9rw_memory23on_pushButton_2_clickedEv'
NVL_READ = '_ZN11NVL_MEM_CFG22read_BIOS_MAILBOX_DATAEjRj'
WRITE64 = '_Z12Write_MMIO64mm'
MEMBER64 = '_ZN7RW_MMIO9Wr_MMIO64Emm'
FUNCTIONS = (RAW, NVL_READ, WRITE64, MEMBER64)
SYNTHETIC_MAPPING = 0x68000000


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    audit = module('client_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, FUNCTIONS)) + ')$'), True)
    report.pop('imports')
    elf = ELFFile(io.BytesIO(data))
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, ngu_fixture_sha256=ngu.FIXTURE_SHA,
                   scope='Original raw-memory slot, NVL read mailbox, 64-bit write member and router; synthetic Qt, libc, mailbox and mmap',
                   functions=[], literals=[], objects={})
    literals = {}
    for name in FUNCTIONS:
        f = next(f for f in report['selected_functions'] if name in f['symbols'])
        symbol = symbols[name]
        section = elf.get_section(symbol['st_shndx'])
        offset = section['sh_offset'] + symbol['st_value'] - section['sh_addr']
        code = data[offset:offset + symbol['st_size']]
        fixture['functions'].append(dict(symbol=name, address=f['address'], size=len(code), code_hex=code.hex(),
                                         code_sha256=hashlib.sha256(code).hexdigest(), calls=f['calls']))
        for literal in f['literals']:
            literals[literal['address']] = literal['text']
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']))
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('MMIO client fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('MMIO client source/schema mismatch')
    return fixture


def combined(fixture):
    if fixture['ngu_fixture_sha256'] != ngu.FIXTURE_SHA:
        raise ValueError('NGU dependency mismatch')
    result = copy.deepcopy(ngu.load_fixture())
    result['functions'].extend(fixture['functions'])
    result['literals'].extend(fixture['literals'])
    functions, _ = ngu.mailbox.fixture(ngu.MAILBOX_PATH)
    f = next(f for f in functions if f['symbol'] == '_Z19Write_MMIO64_kernelmm')
    result['functions'].append(dict(symbol=f['symbol'], address=f['address'], size=len(f['code']),
                                     code_hex=f['code'].hex(), code_sha256=hashlib.sha256(f['code']).hexdigest(),
                                     calls=[dict(instruction=f['write_call'], target=f['write_plt'], symbols=['write@plt'])]))
    return result


class ClientMachine(ngu.NguMachine):
    def __init__(self, fixture, inputs):
        super().__init__(combined(fixture), inputs)
        self.uc.mem_write(self.objects['MY_KMOD_LOADED'], bytes([inputs.get('module_loaded', 1)]))
        self.raw_this = self.alloc(0x100)
        ui = self.alloc(0x80)
        self.raw_widgets = {self.alloc(32): 'address', self.alloc(32): 'data'}
        self.by_field = {v: k for k, v in self.raw_widgets.items()}
        self.put64(self.raw_this + 0x68, ui)
        self.put64(ui + 0x18, self.by_field['address'])
        self.put64(ui + 0x10, self.by_field['data'])
        self.timer = self.alloc(32)
        self.put64(self.raw_this + 0x70, self.timer)
        self.descriptors = {}
        self.parse_calls, self.qt_events, self.mapping_events = [], [], []
        self.map(SYNTHETIC_MAPPING, 8192)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.memory_write)
        self.query_out = self.alloc(8)
        self.put32(self.query_out, 0xa5a5a5a5)

    def memory_write(self, uc, access, address, size, value, unused):
        if SYNTHETIC_MAPPING <= address < SYNTHETIC_MAPPING + 8192:
            self.mapping_events.append(dict(operation='mapped-store', offset=address-SYNTHETIC_MAPPING,
                                            width=size, value=value & ((1 << (8*size))-1)))

    def prepare(self, symbol):
        if symbol == RAW:
            self.reg('rdi', self.raw_this)
        elif symbol == NVL_READ:
            self.reg('rdi', self.nvl_object)
            self.reg('rsi', self.inputs.get('command', 0x80000022))
            self.reg('rdx', self.query_out)
        elif symbol == MEMBER64:
            self.reg('rdi', self.nvl_object)
            self.reg('rsi', self.inputs.get('address', 0x1234567887654321))
            self.reg('rdx', self.inputs.get('data', 0x1234567887654321))
        elif symbol == WRITE64:
            self.reg('rcx', self.inputs.get('address', 0x1234567887654321))
            self.reg('rdx', self.inputs.get('data', 0x1234567887654321))
        else:
            raise ValueError('unexpected original MMIO client entry')

    def external(self, name):
        p = self.inputs
        value = 0
        if name == '_ZNK9QLineEdit4textEv':
            widget = self.reg('rsi')
            if widget not in self.raw_widgets:
                raise ValueError('unexpected raw-memory widget')
            field = self.raw_widgets[widget]
            descriptor = self.alloc(32)
            self.put32(descriptor, p.get('refcount', -1))
            self.put32(descriptor + 4, 0 if p.get('empty_' + field) else 1)
            self.put64(self.reg('rdi'), descriptor)
            self.descriptors[descriptor] = field
        elif name == '_ZNK7QString6toLongEPbi':
            if not self.reg('rsi') or self.reg('rdx') != 16:
                raise ValueError('unexpected original toLong parameters')
            field = self.descriptors[self.u64(self.reg('rdi'))]
            value = p.get(field, 0x1234567887654321 if field == 'address' else 0x89abcdef)
            ok = p.get('parse_ok', True)
            self.uc.mem_write(self.reg('rsi'), bytes([ok]))
            self.parse_calls.append(dict(field=field, base=16, ok_pointer=self.reg('rsi'),
                                          synthetic_ok=ok, synthetic_result=value))
        elif name == '_ZN7QObject10disconnectEPKS_PKcS1_S3_':
            if self.reg('rdi') != self.timer or self.reg('rdx') != self.raw_this:
                raise ValueError('changed disconnect endpoints')
            self.qt_events.append(dict(operation='disconnect', signal=self.cstring(self.reg('rsi')), slot=self.cstring(self.reg('rcx'))))
            value = 1
        elif name == '_ZN7QObject7connectEPKS_PKcS1_S3_N2Qt14ConnectionTypeE':
            if self.reg('rsi') != self.timer or self.reg('rcx') != self.raw_this or self.reg('r9'):
                raise ValueError('changed reconnect endpoints')
            self.qt_events.append(dict(operation='connect', signal=self.cstring(self.reg('rdx')), slot=self.cstring(self.reg('r8'))))
            self.put64(self.reg('rdi'), 0)
        elif name in ('_ZN11QMetaObject10ConnectionD1Ev', '_ZN11QMetaObject10ConnectionD2Ev'):
            pass
        elif name == '_ZN6QTimer5startEi':
            if self.reg('rdi') != self.timer or self.reg('rsi') != 2000:
                raise ValueError('unexpected timer restart')
            self.qt_events.append(dict(operation='timer-start', interval=2000))
        elif name == 'write@plt' and self.reg('rdi') == 900:
            if self.reg('rdx') != 96 or self.reg('rsi') != self.objects['user_request']:
                raise ValueError('changed request pointer/width')
            raw = bytes(self.uc.mem_read(self.reg('rsi'), 96))
            op, token, address, data = struct.unpack('<12Q', raw)[:4]
            if op not in (0x0b, 0x0c, 0x0d) or token != 71 or self.u64(self.mail):
                raise ValueError('invalid original request')
            reply = 0
            if op == 0x0c:
                if address == self.mmio_base + 0x5da0:
                    reply = p.get('reply', 0x12345678)
                elif address == self.mmio_base + 0x5da4:
                    phase = 'post' if any(x['opcode'] in (0x0b, 0x0d) for x in self.mail_io) else 'pre'
                    self.mmio_reads[phase] += 1
                    reply = 0x80000000 if self.mmio_reads[phase] <= p.get(phase + '_busy', 0) else p.get('status', 0)
                else:
                    raise ValueError('unknown synthetic MMIO read target')
            mode = p.get('mailbox_mode', 'success')
            done = 0 if mode in ('no-completion', 'write-error', 'short-write') else ((-12 & ngu.U32) << 32) | 1 if mode == 'encoded-error' else 1
            value = -1 if mode in ('write-error', 'write-error-but-done') else 12 if mode == 'short-write' else 96
            self.put64(self.mail + 8, reply)
            self.put64(self.mail, done)
            self.mail_io.append(dict(opcode=op, address=address, value=data if op != 0x0c else None,
                                     request_hex=raw.hex(), reply=reply, done=done, write_result=value))
        elif name == 'open@plt':
            if self.cstring(self.reg('rdi')) != '/dev/mem' or self.reg('rsi') != 0x101002:
                raise ValueError('unexpected synthetic devmem open')
            value = 901
            self.mapping_events.append(dict(operation='open', path='/dev/mem', flags=0x101002, fd=value))
        elif name == 'sysconf@plt':
            if self.reg('rdi') != 30:
                raise ValueError('unexpected sysconf selector')
            value = 4096
        elif name == 'call_sys_mmap':
            flags, fd, address = struct.unpack('<3Q', self.uc.mem_read(self.reg('r9'), 24))
            if flags != 1 or fd != 901 or address & 4095 or self.reg('rdx') != 8192 or self.reg('r8') != 3:
                raise ValueError('unexpected synthetic mmap arguments')
            self.mapping_events.append(dict(operation='mmap-boundary', address=address, length=8192, flags=flags, fd=fd))
            value = SYNTHETIC_MAPPING
        elif name == 'munmap@plt':
            if self.reg('rdi') != SYNTHETIC_MAPPING or self.reg('rsi') != 8192:
                raise ValueError('unexpected synthetic munmap arguments')
            self.mapping_events.append(dict(operation='munmap-boundary', length=8192))
        elif name == 'close@plt':
            if self.reg('rdi') != 901:
                raise ValueError('unexpected synthetic close')
            self.mapping_events.append(dict(operation='close', fd=901))
        else:
            return super().external(name)
        self.calls.append(name)
        self.ret(value)


def investigate(fixture):
    rows = []

    def case(symbol, inputs, bounded=False):
        m = ClientMachine(fixture, inputs)
        result = m.run(symbol, instruction_limit=20000, allow_instruction_bound=bounded)
        if result['outcome'] != ('instruction-limit' if bounded else 'returned'):
            raise AssertionError('unexpected client completion')
        writes = [x for x in m.mail_io if x['opcode'] != 0x0c]
        stores = [x for x in m.mapping_events if x['operation'] == 'mapped-store']
        if symbol == RAW:
            empty = inputs.get('empty_address') or inputs.get('empty_data')
            if empty:
                if m.mail_io or m.mapping_events or m.parse_calls or m.qt_events or 'Nothing to Write!' not in result['qt_texts']:
                    raise AssertionError('empty raw-memory behavior changed')
            else:
                data = inputs.get('data', 0x89abcdef) & base.MASK
                width = 8 if data >> 32 else 4
                if inputs.get('module_loaded', 1):
                    if len(writes) != 1 or (writes[0]['opcode'], writes[0]['address'], writes[0]['value']) != (0x0b if width == 8 else 0x0d, 0, data):
                        raise AssertionError('raw UI no longer passes zero address')
                elif not bounded:
                    if stores != [dict(operation='mapped-store', offset=0, width=width, value=data & ngu.U32)]:
                        raise AssertionError('fallback data truncation differs')
                if m.u64(m.raw_this + 0x48) != inputs.get('address', 0x1234567887654321) & base.MASK:
                    raise AssertionError('parsed address was not stored in embedded object')
                if [x['field'] for x in m.parse_calls] != ['address', 'data']:
                    raise AssertionError('parse order differs')
                if bounded:
                    if 'Applied!' in result['qt_texts'] or [e['operation'] for e in m.qt_events] != ['disconnect']:
                        raise AssertionError('stalled request still reconnected or showed success')
                elif 'Applied!' not in result['qt_texts'] or [e['operation'] for e in m.qt_events] != ['disconnect', 'connect', 'timer-start']:
                    raise AssertionError('raw UI completion differs')
        elif symbol == NVL_READ and not bounded:
            command = (inputs.get('command', 0x80000022) & 0x1fffffff) | 0x80000000
            if [(x['address'], x['value']) for x in writes] != [(0x5da4, command)]:
                raise AssertionError('read mailbox command write differs')
            for phase in ('pre', 'post'):
                if m.mmio_reads[phase] != min(inputs.get(phase + '_busy', 0) + 1, 101):
                    raise AssertionError('query poll count differs')
            expected_status = 0x80000000 if inputs.get('post_busy', 0) >= 101 else inputs.get('status', 0)
            if m.reg('rax') != expected_status or struct.unpack('<I', m.uc.mem_read(m.query_out, 4))[0] != inputs.get('reply', 0x12345678):
                raise AssertionError('busy query return/output differs')
        elif symbol in (MEMBER64, WRITE64) and not bounded:
            address, data = inputs['address'] & base.MASK, inputs['data'] & base.MASK
            if inputs.get('module_loaded', 1):
                if [(x['opcode'], x['address'], x['value']) for x in writes] != [(0x0b, address, data)]:
                    raise AssertionError('module 64-bit write changed')
            elif stores != [dict(operation='mapped-store', offset=address % 4096, width=8, value=data & ngu.U32)]:
                raise AssertionError('original devmem 64-bit truncation differs')
        rows.append(dict(symbol=symbol, inputs=inputs, mailbox_io=m.mail_io, mapping_events=m.mapping_events,
                         parse_calls=m.parse_calls, qt_events=m.qt_events, poll_reads=m.mmio_reads,
                         return_u32=m.reg('rax') & ngu.U32, **result))

    for loaded, address, data, refcount in itertools.product((0, 1), (0, 0xfed10000, 0x1234567887654321),
            (0, 1, ngu.U32, 0x100000000, 0x1234567887654321, -1), (-1, 1)):
        case(RAW, dict(module_loaded=loaded, address=address, data=data, refcount=refcount))
    for empty_address, empty_data in ((True, False), (False, True), (True, True)):
        case(RAW, dict(empty_address=empty_address, empty_data=empty_data))
    for loaded in (0, 1):
        case(RAW, dict(module_loaded=loaded, address=0, data=0, parse_ok=False))
    for mode in ('encoded-error', 'no-completion', 'write-error', 'short-write', 'write-error-but-done'):
        for data in (1, 0x100000000):
            case(RAW, dict(data=data, mailbox_mode=mode), mode != 'write-error-but-done')
    for pre, post in itertools.product((0, 1, 99, 100, 101, 1000), repeat=2):
        case(NVL_READ, dict(pre_busy=pre, post_busy=post))
    for command in (0, 0x1fffffff, 0x20000000, 0x60000000, ngu.U32):
        case(NVL_READ, dict(command=command, reply=ngu.U32, status=0xff))
    for symbol, loaded, address, data in itertools.product((MEMBER64, WRITE64), (0, 1),
            (0, 0x1234567887654fff), (0, ngu.U32, 0x100000000, 0x1234567887654321, -1)):
        case(symbol, dict(module_loaded=loaded, address=address, data=data))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                ngu_fixture_sha256=ngu.FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(), python=platform.python_version(), unicorn=unicorn.__version__),
                cases_characterized=len(rows), expected_bounded_loops=sum(r['outcome'] == 'instruction-limit' for r in rows),
                observations=rows)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--analysis-output', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary:
        if not args.export_fixture or not args.analysis_output:
            parser.error('binary extraction needs both output paths')
        report, fixture = extract(args.binary)
        args.export_fixture.write_bytes(encoded(fixture))
        args.analysis_output.write_bytes(encoded(report))
        print('fixture SHA ' + report['fixture_sha256'], flush=True)
    else:
        fixture = load_fixture(args.fixture)
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(str(result['cases_characterized']) + ' original MMIO client cases; no hardware')


if __name__ == '__main__':
    main()
