#!/usr/bin/env python3
"""Original mailbox/FIVR/FCH algorithms through original requests, synthetic I/O."""
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
import importlib.util

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


ngu = module('services_ngu', 'legacy-intel-ngu.py')
base = ngu.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-mmio-services.json'
FIXTURE_SHA = '09acb5fe81cf16c1ac7d0181490ab4c13502ed36b2e28db39bccee085c66ce56'
POLL = '_Z16PollMailboxReadyj'
QUERY = '_Z11MailboxReadjjPjS_'
SPREAD = '_Z20Wr_intel_fivr_spreadi'
FSW = '_Z17Wr_intel_fivr_fswt'
DYNAMIC = '_Z16SET_DYNAMIC_FREQh'
FCH = '_ZN11RW_MMIO_AMD11Wr_FCH_MISCEjjjj'
UPDATE = '_ZN11RW_MMIO_AMD18CG1_cfg_update_reqEv'
ASM_READ = '_Z12AsmReadMsr64i'
PACK = '_Z13find_pci_dev2jjj'
FUNCTIONS = (POLL, QUERY, SPREAD, FSW, DYNAMIC, FCH, UPDATE, ASM_READ, PACK)
READ_MSR = '_Z5RdmsrjPjS_'
WRITE_MSR = '_Z5Wrmsrjjj'
U32 = ngu.U32


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    elf = ELFFile(io.BytesIO(data))
    audit = module('services_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, FUNCTIONS)) + ')$'), True)
    report.pop('imports')

    def read(address, size):
        for segment in elf.iter_segments():
            offset = address - segment['p_vaddr']
            if segment['p_type'] == 'PT_LOAD' and 0 <= offset and offset + size <= segment['p_filesz']:
                return data[segment['p_offset']+offset:segment['p_offset']+offset+size]
        raise ValueError('original data outside file mappings')

    previous = ngu.load_fixture()
    known = {r['address'] for r in previous['objects'].values()}
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, ngu_fixture_sha256=ngu.FIXTURE_SHA,
                   scope='Original PollMailboxReady/MailboxRead, FIVR, dynamic and AMD FCH; original MSR/MMIO leaves with synthetic libc/PCI/mailbox',
                   functions=[], literals=[], data=[], read_contexts={})
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    literals, constants = {}, {}
    for name in FUNCTIONS:
        f = next(f for f in report['selected_functions'] if name in f['symbols'])
        code = read(f['address'], f['size'])
        fixture['functions'].append(dict(symbol=name, address=f['address'], size=len(code), code_hex=code.hex(),
                                         code_sha256=hashlib.sha256(code).hexdigest(), calls=f['calls']))
        for row in f['literals']:
            literals[row['address']] = row['text']
        for ins in decoder.disasm(code, f['address']):
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    address = ins.address + ins.size + op.mem.disp
                    if address in known or address in literals:
                        continue
                    if ins.mnemonic == 'divsd' and op.size == 8:
                        raw = read(address, 8)
                        constants[address] = dict(address=address, bytes_hex=raw.hex(),
                                                  sha256=hashlib.sha256(raw).hexdigest(), binary64=struct.unpack('<d', raw)[0])
                    else:
                        raise ValueError('unknown original data reference')
        # Resolve caller contexts from original CALLs, never patch their instructions.
        reads = [c for c in f['calls'] if READ_MSR in c['symbols'] or ngu.RW_READ in c['symbols']]
        if name == POLL:
            tags = ['poll'] * len(reads)
        elif name == QUERY:
            tags = ['status-first', 'data-first', 'status-second',
                    'status-first', 'data-first', 'status-second', 'data-second']
        elif name == ASM_READ:
            tags = ['data-second']
        else:
            tags = ['field' if name != UPDATE else 'update'] * len(reads)
        if len(reads) != len(tags):
            raise ValueError('changed original read call topology')
        for call, tag in zip(reads, tags):
            ins = next(i for i in f['instructions'] if i['address'] == call['instruction'])
            fixture['read_contexts'][str(ins['address'] + ins['size'])] = tag
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    fixture['data'] = list(constants.values())
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']), data=fixture['data'])
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('MMIO service fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA or fixture['ngu_fixture_sha256'] != ngu.FIXTURE_SHA:
        raise ValueError('MMIO service source/dependency mismatch')
    return fixture


class ServiceMachine(ngu.NguMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(ngu.load_fixture())
        combined['functions'].extend(fixture['functions'])
        combined['literals'].extend(fixture['literals'])
        super().__init__(combined, inputs)
        self.contexts = {int(k): v for k, v in fixture['read_contexts'].items()}
        for row in fixture['data']:
            raw = bytes.fromhex(row['bytes_hex'])
            if hashlib.sha256(raw).hexdigest() != row['sha256']:
                raise ValueError('original constant bytes mismatch')
            self.map(row['address'], len(raw))
            self.uc.mem_write(row['address'], raw)
        self.query_data, self.query_status = self.alloc(4), self.alloc(4)
        self.put32(self.query_data, inputs.get('initial_data', 0x11223344))
        self.put32(self.query_status, 0xa5a5a5a5)
        self.put32(self.nvl_object + 0x3c, inputs.get('fch_base', 0xfed80000))
        self.command_sent = False
        self.polls = dict(pre=0, post=0)
        self.delays, self.diagnostics = [], []
        self.access_tag = None

    def prepare(self, symbol):
        p = self.inputs
        if symbol in (POLL, QUERY):
            self.reg('rdi', p.get('kind', 1))
            if symbol == QUERY:
                self.reg('rsi', p.get('command', 0x12345678))
                self.reg('rdx', self.query_data)
                self.reg('rcx', self.query_status)
        elif symbol in (SPREAD, FSW, DYNAMIC):
            self.reg('rdi', p.get('input', 37))
        elif symbol in (FCH, UPDATE):
            self.reg('rdi', self.nvl_object)
            if symbol == FCH:
                self.reg('rsi', p.get('address', 0x84))
                self.reg('rdx', p.get('input', 37))
                self.reg('rcx', p.get('start', 8))
                self.reg('r8', p.get('end', 15))
        else:
            raise ValueError('unexpected MMIO service entry')

    def hook(self, uc, pc, size, unused):
        if pc in (self.entries[READ_MSR], self.entries[ngu.RW_READ]):
            parent = self.u64(self.reg('rsp'))
            if parent not in self.contexts:
                raise ValueError('unknown MMIO service read caller ' + hex(parent))
            self.access_tag = self.contexts[parent]
        # Keep the shared instruction/unknown-target/privileged-opcode checks;
        # NGU-specific caller tags are inapplicable to this separate fixture.
        base.Machine.hook(self, uc, pc, size, unused)

    def reply(self):
        p, tag = self.inputs, self.access_tag
        if tag == 'poll':
            phase = 'post' if self.command_sent else 'pre'
            self.polls[phase] += 1
            value = p.get('poll_status', 0x12)
            if self.polls[phase] <= p.get(phase + '_busy', 0):
                value |= 0x80000000
            return value | (p.get('poll_high', 0) << 32)
        if tag in ('status-first', 'status-second', 'data-first', 'data-second'):
            return p.get(tag.replace('-', '_'), 0xabcdef12 if tag.startswith('status') else 0x87654321)
        if tag == 'field':
            return p.get('old', 0x89abcdef)
        if tag == 'update':
            return p.get('old_update', 0x12345678)
        raise ValueError('unclassified synthetic read')

    def external(self, name):
        p, value = self.inputs, 0
        if name == 'write@plt' and self.reg('rdi') == 900:
            if self.reg('rdx') != 96 or self.reg('rsi') != self.objects['user_request']:
                raise ValueError('changed request width/pointer')
            raw = bytes(self.uc.mem_read(self.reg('rsi'), 96))
            op, token, address, data = struct.unpack('<12Q', raw)[:4]
            if token != 71 or op not in (0x0c, 0x0d) or self.u64(self.mail):
                raise ValueError('changed original request')
            reply = self.reply() & U32 if op == 0x0c else 0
            if op == 0x0d and address == 0x5da4:
                self.command_sent = True
            mode = p.get('mailbox_mode', 'success')
            done = 0 if mode in ('no-completion', 'write-error', 'short-write') else ((-12 & U32) << 32) | 1 if mode == 'encoded-error' else 1
            value = -1 if mode in ('write-error', 'write-error-but-done') else 12 if mode == 'short-write' else 96
            self.put64(self.mail + 8, reply)
            self.put64(self.mail, done)
            self.mail_io.append(dict(opcode=op, address=address, value=data if op == 0x0d else None,
                                     request_hex=raw.hex(), reply=reply, done=done, write_result=value,
                                     context=self.access_tag if op == 0x0c else None))
        elif name == 'read@plt':
            if self.reg('rdx') != 8 or self.offset not in (0x607, 0x608):
                raise ValueError('unexpected service MSR read')
            address = self.reg('rsi')
            before = bytes(self.uc.mem_read(address, 8))
            reply = struct.pack('<Q', self.reply() & base.MASK)
            value = -1 if self.mode in ('open-error', 'read-error') else 0 if self.mode == 'read-eof' else 4 if self.mode == 'short-read-4' else 7 if self.mode == 'short-read-7' else 8
            if value > 0:
                self.uc.mem_write(address, reply[:value])
            self.io.append(dict(op='read', offset=self.offset, context=self.access_tag, result=value,
                                buffer=address, before_hex=before.hex(), after_hex=bytes(self.uc.mem_read(address, 8)).hex()))
        elif name == 'usleep@plt':
            value_us = self.reg('rdi')
            if value_us not in (10, 1000):
                raise ValueError('unexpected service delay')
            self.delays.append(value_us)
        elif name == 'puts@plt':
            self.diagnostics.append(self.cstring(self.reg('rdi')))
        else:
            super().external(name)
            if name == 'write@plt' and self.offset == 0x607:
                self.command_sent = True
            return
        self.calls.append(name)
        self.ret(value)


def u32_at(m, address):
    return struct.unpack('<I', m.uc.mem_read(address, 4))[0]


def investigate(fixture):
    rows = []

    def case(symbol, inputs, bounded=False):
        m = ServiceMachine(fixture, inputs)
        result = m.run(symbol, instruction_limit=20000, allow_instruction_bound=bounded)
        if result['outcome'] != ('instruction-limit' if bounded else 'returned'):
            raise AssertionError('service completion differs')
        writes = [(x['address'], x['value']) for x in m.mail_io if x['opcode'] == 0x0d]
        read_addresses = [x['address'] for x in m.mail_io if x['opcode'] == 0x0c]
        kind = inputs.get('kind', 1)
        if symbol in (POLL, QUERY) and not bounded:
            # Error-read cases preserve original stack data, so their observed busy
            # counts are recorded rather than replaced with idealized replies.
            if inputs.get('io_mode', 'success') not in ('open-error', 'read-error', 'read-eof', 'short-read-4', 'short-read-7'):
                expected = min(inputs.get('pre_busy', 0) + 1, 10) if kind in (1, 3) else 0
                if m.polls['pre'] != expected:
                    raise AssertionError('initial poll budget differs')
                if symbol == QUERY and kind in (1, 3) and m.polls['post'] != min(inputs.get('post_busy', 0) + 1, 10):
                    raise AssertionError('post-command poll budget differs')
            if symbol == POLL and m.reg('rax') != 0:
                raise AssertionError('original poll no longer returns zero')
            if kind not in (1, 3) and (len(m.delays) != 10 or m.mail_io or m.io):
                raise AssertionError('unsupported poll behavior changed')
            if symbol == QUERY and inputs.get('io_mode', 'success') == 'success':
                status1, status2 = inputs.get('status_first', 0xabcdef12), inputs.get('status_second', 0xabcdef12)
                data1, data2 = inputs.get('data_first', 0x87654321), inputs.get('data_second', 0x87654321)
                mismatch = (status1 & U32) != (status2 & U32) and (data1 & U32) != (data2 & U32)
                code = 0x8000000000000003 if kind not in (1, 3) else 0x8000000000000002 if mismatch else 0
                if m.reg('rax') != code:
                    raise AssertionError('two-read AND comparison/return differs')
                expected_data = inputs.get('initial_data', 0x11223344) if code else data1 & U32
                expected_status = 0xa5a5a5a5 if code else status1 & 255
                if (u32_at(m, m.query_data), u32_at(m, m.query_status)) != (expected_data, expected_status):
                    raise AssertionError('query output mutation differs')
                if kind == 1 and writes != [(0x5da4, (inputs.get('command', 0x12345678) | 0x80000000) & U32)]:
                    raise AssertionError('query command mask/address differs')
                if kind == 3:
                    msr_writes = [(x['offset'], x['low'], x['high']) for x in m.io if x['op'] == 'write']
                    if msr_writes != [(0x608, inputs.get('initial_data', 0x11223344), 0), (0x607, (inputs.get('command', 0x12345678) | 0x80000000) & U32, 0)]:
                        raise AssertionError('MSR query writes differ')
        elif not bounded:
            value, old = inputs.get('input', 37), inputs.get('old', 0x89abcdef)
            if symbol == SPREAD:
                expected = [(0x5a08, ((old & 0xffffff00) | value | 0x100) & U32)]
            elif symbol == FSW:
                field = min(((value & 0xffff) + 3) // 9, 1023)
                expected = [(0x5a18, (old & 0xff00c7ff) | ((field & 7) << 11) | ((field >> 3) << 16))]
            elif symbol == DYNAMIC:
                expected = [(0x5da0, value & 255), (0x5da4, 0x80000122)]
            else:
                expected = [(0x40, inputs.get('old_update', 0x12345678) | 0x40000000)]
                if symbol == FCH:
                    start, end = inputs.get('start', 8), inputs.get('end', 15)
                    mask = (1 << ((end-start+1) & 31)) - 1
                    shift = start & 63
                    changed = ((old & ~(mask << shift)) | ((value & mask) << shift)) & U32
                    expected.insert(0, (inputs.get('address', 0x84) & U32, changed))
            if writes != expected:
                raise AssertionError(('field computation/address differs', symbol, inputs, writes, expected))
            if symbol in (SPREAD, FSW):
                if read_addresses != [((inputs.get('pci_low', 0xfed10001)-1) & U32) + expected[0][0]]:
                    raise AssertionError('FIVR no longer overwrites high base bits')
            elif symbol == DYNAMIC and read_addresses:
                raise AssertionError('dynamic helper added MMIO verification')
            elif symbol in (FCH, UPDATE):
                offsets = ([inputs.get('address', 0x84) & U32] if symbol == FCH else []) + [0x40]
                if read_addresses != [(inputs.get('fch_base', 0xfed80000) & U32) + offset for offset in offsets]:
                    raise AssertionError('FCH original read base differs')
        rows.append(dict(symbol=symbol, inputs=inputs, return_u64=m.reg('rax'), query_data=u32_at(m, m.query_data),
                         query_status=u32_at(m, m.query_status), polls=m.polls, delays_us=m.delays,
                         mailbox_io=m.mail_io, msr_io=m.io, pci=m.pci, diagnostics=m.diagnostics, **result))

    for kind, busy, high in itertools.product((1, 3), (0, 1, 8, 9, 10, 1000), (0, 0x80000000)):
        case(POLL, dict(kind=kind, pre_busy=busy, poll_high=high))
    for kind in (0, 2, 4, U32):
        case(POLL, dict(kind=kind))
        case(QUERY, dict(kind=kind))
    for kind, pre, post in itertools.product((1, 3), (0, 9, 10, 1000), (0, 9, 10, 1000)):
        case(QUERY, dict(kind=kind, pre_busy=pre, post_busy=post))
    for kind, status2, data2 in itertools.product((1, 3), (0x12345612, 0x12345634, 0xabcdef0012345612),
                                                (0x87654321, 0x87654322, 0xabcdef0087654321)):
        case(QUERY, dict(kind=kind, status_first=0x12345612, status_second=status2, data_second=data2))
    for kind, command in itertools.product((1, 3), (0, 0x20000000, 0x60000000, U32)):
        case(QUERY, dict(kind=kind, command=command))
    for mode in ('read-error', 'read-eof', 'short-read-4', 'short-read-7', 'open-error', 'write-error', 'short-write', 'seek-error', 'close-error'):
        case(QUERY, dict(kind=3, io_mode=mode))
    for mode in ('encoded-error', 'no-completion', 'write-error', 'short-write', 'write-error-but-done'):
        case(QUERY, dict(kind=1, mailbox_mode=mode), mode != 'write-error-but-done')
    for symbol, nvl, old in itertools.product((SPREAD, FSW, DYNAMIC), (False, True), (0, U32)):
        values = (0, 1, 255, 256, 0x10000, 0x80000000, U32) if symbol != FSW else (0, 5, 6, 8, 9, 14, 15, 9203, 9204, 9212, 9213, 65535, 65536, U32)
        for value in values:
            case(symbol, dict(input=value, nvl=nvl, old=old))
    for (start, end), value, old in itertools.product(((0, 31), (0, 30), (8, 15), (31, 31), (32, 39), (63, 64), (64, 71), (15, 8)), (0, U32), (0, U32)):
        case(FCH, dict(start=start, end=end, input=value, old=old))
    for fch_base, address in itertools.product((0, 0xfed80000, U32), (0, 0x40, 0x100000084)):
        case(FCH, dict(fch_base=fch_base, address=address))
    for old in (0, 0x12345678, U32):
        case(UPDATE, dict(old_update=old))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                ngu_fixture_sha256=ngu.FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(), python=platform.python_version()),
                cases_characterized=len(rows), expected_bounded_loops=sum(r['outcome'] == 'instruction-limit' for r in rows), observations=rows)


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
            parser.error('extraction requires fixture and analysis paths')
        report, fixture = extract(args.binary)
        args.export_fixture.write_bytes(encoded(fixture))
        args.analysis_output.write_bytes(encoded(report))
        print('fixture SHA ' + report['fixture_sha256'], flush=True)
    else:
        fixture = load_fixture(args.fixture)
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(str(result['cases_characterized']) + ' original MMIO service cases; no hardware')


if __name__ == '__main__':
    main()
