#!/usr/bin/env python3
"""Trace original NGU operations into original MMIO requests; synthetic I/O only."""
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

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import unicorn

import importlib.util


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


msr = module('ngu_msr', 'emulate-legacy-msr.py')
mailbox = module('ngu_mailbox', 'emulate-legacy-mailbox.py')
base = msr.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-intel-ngu.json'
FIXTURE_SHA = '25c9a574a7bd50bcb07a4fa1ff18fd0313690be3dc4f1231717582411fdb8e68'
NGU = '_Z13set_ngu_ratioi'
SAGV = '_Z27Rd_SAGV_CONFIG_POLICY_Tunerv'
NVL_WRITE = '_ZN11NVL_MEM_CFG23write_BIOS_MAILBOX_DATAEjj'
RW_CTOR = '_ZN7RW_MMIOC2Ev'
RW_READ = '_ZN7RW_MMIO7Rd_MMIOEm'
RW_WRITE = '_ZN7RW_MMIO7Wr_MMIOEjj'
SLOTS = ('_ZN10intel_ctl611ngu_clickedEv', '_ZN10intel_ctl324on_pushButton_17_clickedEv')
FUNCTIONS = (NGU, SAGV, NVL_WRITE, RW_CTOR, RW_READ, RW_WRITE,
             '_ZN7RW_MMIOD2Ev', '_Z9Read_MMIOm', '_Z10Write_MMIOmm') + SLOTS
U32 = 0xffffffff
READ_STAGES = {0x2bfc88: 'query', 0x2bfca0: 'extra', 0x2bfce0: 'commit',
               0x2bfd33: 'nvl-first', 0x2bfd68: 'nvl-second', 0x384857: 'policy'}
MAILBOX_PATH = Path(__file__).resolve().parents[1] / 'fixtures/legacy-mmio-wrappers.json'


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    elf = ELFFile(io.BytesIO(data))
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    objects = {s['st_value']: s for s in symbols if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}

    def read(address, size):
        for segment in elf.iter_segments():
            rel = address - segment['p_vaddr']
            if segment['p_type'] != 'PT_LOAD' or rel < 0:
                continue
            if rel + size <= segment['p_filesz']:
                return data[segment['p_offset'] + rel:segment['p_offset'] + rel + size]
            if segment['p_filesz'] <= rel and rel + size <= segment['p_memsz']:
                return bytes(size)
        raise ValueError('unmapped original bytes')

    audit = module('ngu_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, FUNCTIONS)) + ')$'), True)
    report.pop('imports')
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA,
                   msr_fixture_sha256=msr.FIXTURE_SHA, mailbox_fixture_sha256=mailbox.FIXTURE_SHA256,
                   scope='Original NGU/MSR/MMIO business and request instructions; synthetic libc, PCI, object constructors and mailbox responses',
                   functions=[], objects={}, literals=[])
    literals = {}
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    for name in FUNCTIONS:
        f = next(f for f in report['selected_functions'] if name in f['symbols'])
        code = read(f['address'], f['size'])
        fixture['functions'].append(dict(symbol=name, address=f['address'], size=len(code), code_hex=code.hex(),
                                         code_sha256=hashlib.sha256(code).hexdigest(), calls=f['calls']))
        for literal in f['literals']:
            literals[literal['address']] = literal['text']
        for ins in decoder.disasm(code, f['address']):
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    address = ins.address + ins.size + op.mem.disp
                    if address in objects:
                        s = objects[address]
                        fixture['objects'][s.name] = dict(address=address, size=s['st_size'],
                                                         source_initial_hex=read(address, s['st_size']).hex())
                    elif address not in literals:
                        raise ValueError('unknown RIP-relative original data')
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    target = next(f['address'] for f in fixture['functions'] if f['symbol'] == NGU)
    member_target = next(f['address'] for f in fixture['functions'] if f['symbol'] == RW_WRITE)
    seen, callers, member_callers = set(), [], []
    for s in symbols:
        if s['st_info']['type'] != 'STT_FUNC' or not s['st_size'] or not isinstance(s['st_shndx'], int):
            continue
        if not elf.get_section(s['st_shndx'])['sh_flags'] & 4:
            continue
        key = (s['st_value'], s['st_size'])
        if key in seen:
            continue
        seen.add(key)
        code = read(*key)
        ins = list(Cs(CS_ARCH_X86, CS_MODE_64).disasm_lite(code, key[0]))
        hits = [n for n, i in enumerate(ins) if i[2] == 'call' and i[3] == hex(target)]
        if hits:
            callers.append(dict(symbol=s.name, address=key[0], size=key[1], code_sha256=hashlib.sha256(code).hexdigest(),
                                calls=[dict(instruction=ins[n][0], context=[dict(address=i[0], mnemonic=i[2], operands=i[3])
                                      for i in ins[max(0, n-10):n+6]]) for n in hits]))
        member_hits = [n for n, i in enumerate(ins) if i[2] == 'call' and i[3] == hex(member_target)]
        if member_hits:
            member_callers.append(dict(symbol=s.name, address=key[0], size=key[1],
                                       code_sha256=hashlib.sha256(code).hexdigest(),
                                       calls=[dict(instruction=ins[n][0], context=[dict(address=i[0], mnemonic=i[2], operands=i[3])
                                             for i in ins[max(0, n-6):n+2]]) for n in member_hits]))
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']), direct_callers=callers,
                  member_write_callers=member_callers,
                  caller_scope='Aligned direct CALLs in declared executable STT_FUNC only; contexts do not classify absolute/relative address intent or execute all callers')
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('NGU fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('NGU source/schema mismatch')
    return fixture


def combined(fixture):
    if (fixture['msr_fixture_sha256'] != msr.FIXTURE_SHA or
            fixture['mailbox_fixture_sha256'] != mailbox.FIXTURE_SHA256):
        raise ValueError('NGU dependency mismatch')
    result = copy.deepcopy(fixture)
    previous = msr.load_fixture()
    result['functions'].extend(f for f in previous['functions'] if f['symbol'] in ('_Z5RdmsrjPjS_', '_Z5Wrmsrjjj'))
    result['literals'].extend(previous['literals'])
    functions, objects = mailbox.fixture(MAILBOX_PATH)
    for f in functions:
        if f['symbol'] not in ('_Z16Read_MMIO_kernelm', '_Z17Write_MMIO_kernelmm'):
            continue
        result['functions'].append(dict(symbol=f['symbol'], address=f['address'], size=len(f['code']),
                                         code_hex=f['code'].hex(), code_sha256=hashlib.sha256(f['code']).hexdigest(),
                                         calls=[dict(instruction=f['write_call'], target=f['write_plt'], symbols=['write@plt'])]))
    for name, address in objects.items():
        result['objects'][name] = dict(address=address, size=96 if name == 'user_request' else 8,
                                       source_initial_hex='00' * (96 if name == 'user_request' else 8))
    return result


class NguMachine(msr.MsrMachine):
    def __init__(self, fixture, inputs):
        super().__init__(combined(fixture), inputs)
        for name, key in (('GLOBAL_IS_NVL', 'nvl'), ('GLOBAL_IS_GNR_SP', 'gnr')):
            self.uc.mem_write(self.objects[name], bytes([bool(inputs.get(key))]))
        self.uc.mem_write(self.objects['MY_KMOD_LOADED'], b'\x01')
        self.mail = self.alloc(4096)
        self.put64(self.objects['kernel_address'], self.mail)
        self.put32(self.objects['kernel_fd'], 900)
        self.put64(self.objects['user_request'] + 8, 71)
        self.put64(self.mail, 1)
        self.mail_io, self.helper_events, self.pci = [], [], []
        self.stage, self.offset = None, None
        self.stage_reads = {}
        self.mmio_reads = {'pre': 0, 'post': 0}
        self.qt_reads = 0
        self.qt_conversions = []
        self.mmio_base = inputs.get('mmio_base', 0x123450000)
        self.nvl_object = self.alloc(0xe28)
        self.put64(self.nvl_object + 0x18, self.mmio_base)
        self.put64(self.objects['GLOBAL_NVL_MEM_CFG'], 0 if inputs.get('allocate') else self.nvl_object)
        self.gnr_object = self.alloc(0x588)
        self.gnr_backend = self.alloc(32)
        self.put64(self.gnr_object + 0x558, self.gnr_backend)
        self.put64(self.objects['GLOBAL_GNR_SP_MEM_CFG'], 0 if inputs.get('allocate') else self.gnr_object)

    def prepare(self, symbol):
        if symbol == NGU:
            self.reg('rdi', self.inputs.get('ratio', 37))
        elif symbol == NVL_WRITE:
            self.reg('rdi', self.nvl_object)
            self.reg('rsi', self.inputs.get('command', 0x80001322))
            self.reg('rdx', self.inputs.get('data', 37))
        elif symbol == RW_READ:
            self.reg('rdi', self.nvl_object)
            self.reg('rsi', self.inputs.get('address', 0x5da4))
        elif symbol == RW_WRITE:
            self.reg('rdi', self.nvl_object)
            self.reg('rsi', self.inputs.get('address', 0x5da0))
            self.reg('rdx', self.inputs.get('data', 37))
        elif symbol == RW_CTOR:
            self.reg('rdi', self.nvl_object)
        elif symbol in SLOTS:
            obj, ui, widget = self.alloc(0x600), self.alloc(0x1500), self.alloc(32)
            self.input_widget = widget
            self.reg('rdi', obj)
            if symbol == SLOTS[0]:
                left = self.alloc(0x40)
                self.put64(obj + 0x30, left)
                self.put64(left + 0x30, ui)
                self.put64(ui + 0x490, widget)
            else:
                self.put64(obj + 0x4e0, ui)
                self.put64(ui + 0x1498, widget)
        else:
            raise ValueError('unexpected NGU entry')

    def hook(self, uc, pc, size, unused):
        if pc == self.entries['_Z5RdmsrjPjS_']:
            parent = self.u64(self.reg('rsp'))
            if parent not in READ_STAGES:
                raise ValueError('unknown original MSR caller')
            self.stage = READ_STAGES[parent]
        super().hook(uc, pc, size, unused)

    def external(self, name):
        p = self.inputs
        value = 0
        if name == '_ZNK9QLineEdit4textEv':
            if self.reg('rsi') != self.input_widget:
                raise ValueError('wrong original NGU input widget')
            descriptor = self.alloc(32)
            self.put32(descriptor, p.get('refcount', -1))
            self.put32(descriptor + 4, 0 if p.get('empty') else 1)
            self.put64(self.reg('rdi'), descriptor)
            self.qt_reads += 1
        elif name == '_ZNK7QString6toUIntEPbi':
            if self.reg('rsi') or self.reg('rdx') != 10:
                raise ValueError('changed NGU Qt conversion')
            value = p.get('parsed_uint', 37)
            self.qt_conversions.append(dict(base=10, ok_pointer=0, synthetic_result=value))
        elif name in ('_ZN11QMessageBoxC2EP7QWidget', '_ZN11QMessageBoxD2Ev'):
            pass
        elif name == 'lseek@plt':
            self.offset = self.reg('rsi')
            if self.offset not in (0x150, 0x607, 0x608) or self.reg('rdx'):
                raise ValueError('unexpected MSR seek')
            value = -1 if self.mode in ('open-error', 'seek-error') else self.offset
            self.io.append(dict(op='lseek', fd=self.reg('rdi') & U32, offset=self.offset, result=value))
        elif name == 'write@plt' and self.reg('rdi') == 900:
            if self.reg('rdx') != 96 or self.reg('rsi') != self.objects['user_request']:
                raise ValueError('bad original 96-byte request')
            raw = bytes(self.uc.mem_read(self.reg('rsi'), 96))
            request = struct.unpack('<12Q', raw)
            op, token, address, data = request[:4]
            if op not in (0x0c, 0x0d) or token != 71 or self.u64(self.mail):
                raise ValueError('original mailbox request changed')
            reply = 0
            if op == 0x0c:
                stage = 'post' if any(x['opcode'] == 0x0d for x in self.mail_io) else 'pre'
                self.mmio_reads[stage] += 1
                busy = p.get(stage + '_busy', 0)
                reply = 0x80000000 if self.mmio_reads[stage] <= busy else p.get('mmio_reply', 0)
            mode = p.get('mailbox_mode', 'success')
            done = 0 if mode in ('no-completion', 'write-error', 'short-write') else ((-12 & U32) << 32) | 1 if mode == 'encoded-error' else 1
            value = -1 if mode in ('write-error', 'write-error-but-done') else 12 if mode == 'short-write' else 96
            self.put64(self.mail + 8, reply)
            self.put64(self.mail, done)
            self.mail_io.append(dict(opcode=op, address=address, value=data if op == 0x0d else None,
                                     request_hex=raw.hex(), reply=reply, done=done, write_result=value))
        elif name == 'read@plt':
            if self.reg('rdx') != 8 or self.offset not in (0x150, 0x608):
                raise ValueError('unexpected original MSR read')
            self.reads += 1
            self.stage_reads[self.stage] = self.stage_reads.get(self.stage, 0) + 1
            address = self.reg('rsi')
            before = bytes(self.uc.mem_read(address, 8))
            low, high = p.get('reply_low', 0x12345678), 0
            if self.stage in ('query', 'commit'):
                if self.stage_reads[self.stage] <= p.get(self.stage + '_busy', 0):
                    high = 0x80000000
            if self.stage == 'extra':
                low, high = p.get('extra_low', 0x87654321), p.get('extra_high', 0)
            if self.stage in ('nvl-first', 'nvl-second'):
                low, high = p.get('nvl_low', 0x12345678), p.get('nvl_high', 0xabcdef01)
            if self.stage == 'policy':
                low, high = p.get('policy', 0x89abcdef), p.get('policy_high', 0x76543210)
            mode = self.mode if p.get('fail_stage') in (None, self.stage) else 'success'
            value = -1 if mode in ('open-error', 'read-error') else 0 if mode == 'read-eof' else 4 if mode == 'short-read-4' else 7 if mode == 'short-read-7' else 8
            if value > 0:
                self.uc.mem_write(address, struct.pack('<II', low, high)[:value])
            self.read_buffers.append(address)
            self.io.append(dict(op='read', fd=self.reg('rdi') & U32, offset=self.offset, stage=self.stage,
                                result=value, buffer=address, before_hex=before.hex(),
                                after_hex=bytes(self.uc.mem_read(address, 8)).hex()))
        elif name == '_Z13find_pci_dev2jjj':
            if any(self.reg(r) for r in ('rdi', 'rsi', 'rdx')):
                raise ValueError('unexpected BDF pack arguments')
        elif name == '_Z18ReadPciConfigDwordjj':
            offset = self.reg('rdx')
            if self.reg('rcx') != 0 or offset not in (0x48, 0x4c):
                raise ValueError('unexpected PCI constructor read')
            value = p.get('pci_low', 0xfed10001) if offset == 0x48 else p.get('pci_high', 1)
            self.pci.append(dict(bdf=0, offset=offset, synthetic_value=value))
        elif name.startswith('_ZN11NVL_MEM_CFGC'):
            obj = self.reg('rdi')
            self.put64(obj + 0x18, self.mmio_base)
            self.helper_events.append(dict(boundary='NVL_MEM_CFG constructor', object=obj))
        elif name.startswith('_ZN14GNR_SP_MEM_CFGC'):
            if self.reg('rsi') != 1:
                raise ValueError('GNR constructor argument differs')
            self.put64(self.reg('rdi') + 0x558, self.gnr_backend)
            self.helper_events.append(dict(boundary='GNR_SP_MEM_CFG constructor', argument=1))
        elif name == '_ZN14gnr_punit_pcie23read_BIOS_MAILBOX_DATA2EiRj':
            if self.reg('rdi') != self.gnr_backend or self.reg('rsi') != 0x80000022:
                raise ValueError('GNR policy boundary arguments differ')
            self.put32(self.reg('rdx'), p.get('policy', 0x89abcdef))
            value = p.get('policy_return', 0)
            self.helper_events.append(dict(boundary='GNR policy read', returned=value,
                                          output=p.get('policy', 0x89abcdef)))
        else:
            super().external(name)
            if name == 'write@plt':
                self.io[-1]['offset'] = self.offset
            return
        self.calls.append(name)
        self.ret(value)


def clamped(value):
    value &= U32
    signed = value - (1 << 32) if value & (1 << 31) else value
    return min(signed, 255) & U32


def investigate(fixture):
    rows = []

    def case(symbol, inputs, bounded=False):
        m = NguMachine(fixture, inputs)
        result = m.run(symbol, instruction_limit=20000, allow_instruction_bound=bounded)
        if result['outcome'] != ('instruction-limit' if bounded else 'returned'):
            raise AssertionError('unexpected NGU completion')
        writes = [x for x in m.io if x['op'] == 'write']
        mmio_writes = [(x['address'], x['value']) for x in m.mail_io if x['opcode'] == 0x0d]
        if (symbol == NGU or symbol in SLOTS) and not bounded and not inputs.get('empty'):
            value = clamped(inputs.get('ratio' if symbol == NGU else 'parsed_uint', 37))
            expected_low = (inputs.get('extra_low', 0x87654321) & 0xffffff00) | (value & 255)
            if [(x['offset'], x['low'], x['high']) for x in writes[:2]] != [(0x150, 0, 0x80000710), (0x150, expected_low, 0x80000711)]:
                raise AssertionError('query/update/clamping differs')
            if inputs.get('nvl'):
                if mmio_writes != [(0x5da0, value), (0x5da4, 0x80001322)]:
                    raise AssertionError('NVL original absolute write request differs')
                reads = [x for x in m.mail_io if x['opcode'] == 0x0c]
                if not reads or any(x['address'] != m.mmio_base + 0x5da4 for x in reads):
                    raise AssertionError('NVL original relative read request differs')
                if [(x['offset'], x['low'], x['high']) for x in writes[2:]] != [(0x607, 0x80001222, 0), (0x607, 0x80001322, inputs.get('nvl_high', 0xabcdef01))]:
                    raise AssertionError('NVL additional MSR side effects differ')
            else:
                payload = (inputs.get('policy', 0x89abcdef) & 0xffff00ff) | ((value & 255) << 8) | 0x10000
                if mmio_writes != [(0x5da0, payload), (0x5da4, 0x80000122)] * 2:
                    raise AssertionError('non-NVL repeated absolute writes differ')
                if len(m.pci) != 1:
                    raise AssertionError('RW_MMIO base construction differs')
        if symbol == NVL_WRITE and not bounded:
            expected_command = (inputs.get('command', 0x80001322) & 0x1fffffff) | 0x80000000
            if mmio_writes != [(0x5da0, inputs.get('data', 37) & U32), (0x5da4, expected_command)]:
                raise AssertionError('BIOS mailbox command mask/address differs')
            for phase in ('pre', 'post'):
                if m.mmio_reads[phase] != min(inputs.get(phase + '_busy', 0) + 1, 101):
                    raise AssertionError('BIOS mailbox busy budget differs')
        if symbol == RW_READ and m.mail_io[0]['address'] != (m.mmio_base + inputs.get('address', 0x5da4)) & base.MASK:
            raise AssertionError('read no longer adds base')
        if symbol == RW_WRITE and m.mail_io[0]['address'] != inputs.get('address', 0x5da0) & U32:
            raise AssertionError('write no longer sends offset as address')
        if symbol == RW_CTOR:
            expected = (inputs.get('pci_low', 0xfed10001) - 1) & U32
            if inputs.get('nvl'):
                expected |= inputs.get('pci_high', 1) << 32
            if m.u64(m.nvl_object + 0x18) != expected:
                raise AssertionError('constructor subtraction/upper width differs')
        if symbol in SLOTS:
            if inputs.get('empty'):
                if m.mail_io or m.io or m.qt_conversions or m.qt_reads != 1 or 'Nothing to Write!' not in result['qt_texts']:
                    raise AssertionError('empty UI path differs')
            elif bounded:
                if 'Applied!' in result['qt_texts']:
                    raise AssertionError('bounded failure reached success notification')
            elif 'Applied!' not in result['qt_texts'] or len(m.qt_conversions) != 1 or m.qt_reads != 2:
                raise AssertionError('returned NGU path did not show original notification')
        rows.append(dict(symbol=symbol, inputs=inputs, msr_io=m.io, mailbox_io=m.mail_io,
                         mmio_phase_read_counts=m.mmio_reads, helper_events=m.helper_events,
                         qt_reads=m.qt_reads, qt_conversions=m.qt_conversions,
                         pci_constructor_reads=m.pci, stop_pc=m.reg('rip'), **result))

    for nvl, gnr, value, allocate in itertools.product((False, True), (False, True),
             (-2147483648, -257, -256, -1, 0, 1, 254, 255, 256, 257, 2147483647), (False, True)):
        case(NGU, dict(nvl=nvl, gnr=gnr, ratio=value, allocate=allocate))
    for nvl, stage, mode in itertools.product((False, True), ('query', 'commit'),
             ('read-error', 'read-eof', 'short-read-4', 'short-read-7')):
        case(NGU, dict(nvl=nvl, io_mode=mode, fail_stage=stage), True)
    for nvl, mode in itertools.product((False, True), ('open-error', 'write-error', 'short-write', 'seek-error', 'close-error')):
        case(NGU, dict(nvl=nvl, io_mode=mode), mode == 'open-error')
    for nvl in (False, True):
        case(NGU, dict(nvl=nvl, query_busy=3, commit_busy=2))
        case(NGU, dict(nvl=nvl, extra_high=0x80000000, extra_low=0x11223344))
        for mode in ('encoded-error', 'no-completion', 'write-error', 'short-write', 'write-error-but-done'):
            case(NGU, dict(nvl=nvl, mailbox_mode=mode), mode != 'write-error-but-done')
    for pre, post in itertools.product((0, 1, 99, 100, 101, 1000), repeat=2):
        case(NVL_WRITE, dict(pre_busy=pre, post_busy=post))
    for command in (0, 1, 0x1fffffff, 0x20000000, 0x40000000, 0x60000000, 0x80000000, U32):
        case(NVL_WRITE, dict(command=command, data=U32))
    for offset, objbase in itertools.product((0, 0x5da0, 0x5da4, 0x100005da4), (0, 0xfed10000, 0x123450000)):
        case(RW_READ, dict(address=offset, mmio_base=objbase))
        case(RW_WRITE, dict(address=offset, mmio_base=objbase))
    for nvl, low in itertools.product((False, True), (0, 1, 0xfed10001, U32)):
        case(RW_CTOR, dict(nvl=nvl, pci_low=low, pci_high=0x12345678))
    for symbol, nvl, value, refcount in itertools.product(SLOTS, (False, True),
             (0, 255, 256, 2147483647, 2147483648, U32), (-1, 1)):
        case(symbol, dict(nvl=nvl, parsed_uint=value, refcount=refcount))
    for symbol, nvl in itertools.product(SLOTS, (False, True)):
        case(symbol, dict(nvl=nvl, empty=True))
        case(symbol, dict(nvl=nvl, io_mode='write-error'))
        case(symbol, dict(nvl=nvl, io_mode='read-error'), True)
    for symbol in SLOTS:
        case(symbol, dict(nvl=True, pre_busy=1000, post_busy=1000))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                msr_fixture_sha256=msr.FIXTURE_SHA, mailbox_fixture_sha256=mailbox.FIXTURE_SHA256,
                scope=fixture['scope'] + '; module-loaded path only, no hardware, complete constructors or actual GUI',
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
            parser.error('binary extraction needs both paths')
        report, fixture = extract(args.binary)
        args.export_fixture.write_bytes(encoded(fixture))
        args.analysis_output.write_bytes(encoded(report))
        print('fixture SHA ' + report['fixture_sha256'], flush=True)
    else:
        fixture = load_fixture(args.fixture)
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(str(result['cases_characterized']) + ' NGU/MMIO cases; ' + str(result['expected_bounded_loops']) + ' expected bounded loops; no hardware')


if __name__ == '__main__':
    main()
