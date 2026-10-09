#!/usr/bin/env python3
"""Run the original I2C MMIO state machine against explicit synthetic devices."""
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
from elftools.elf.relocation import RelocationSection
import unicorn


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


ngu = module('i2c_ngu', 'legacy-intel-ngu.py')
base = ngu.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-mmio-i2c.json'
FIXTURE_SHA = '91c77a2bee73d5a1fb4eab6dfec63eb7ee9d596e34792c67136204b896ab0618'
ENTRY = '_Z21CpmReadWriteI2CBytes5hhmPhmS_'
TABLES = ('mI2CConfiStp', 'mI2CConfigRplRmb')
SHIMADA = 'GLOBAL_IS_SHIMADA'
READ_CONTEXTS = {
    0x230fc7: 'disable', 0x230fef: 'disable', 0x231062: 'clear', 0x23106f: 'clear',
    0x2310ac: 'enable', 0x2310d7: 'enable', 0x23110d: 'outer-status', 0x231122: 'outer-level',
    0x2311a3: 'abort', 0x2311c2: 'abort', 0x231202: 'idle', 0x231227: 'idle', 0x23157d: 'idle',
    0x2312dd: 'rx-ready', 0x2312f5: 'rx-ready', 0x231324: 'rx-data', 0x23147d: 'clear',
}
INSERT = '_ZSt16__ostream_insertIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_PKS3_l@plt'
ENDL = '_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_.isra.0'


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    elf = ELFFile(io.BytesIO(data))
    symbols = {s['st_value']: s for s in elf.get_section_by_name('.symtab').iter_symbols()
               if s['st_info']['type'] == 'STT_OBJECT' and s['st_size']}
    relocations = {}
    for sec in elf.iter_sections():
        if not isinstance(sec, RelocationSection):
            continue
        syms = elf.get_section(sec['sh_link'])
        for rel in sec.iter_relocations():
            relocations[rel['r_offset']] = dict(type=rel['r_info_type'], addend=rel['r_addend'],
                                                symbol=syms.get_symbol(rel['r_info_sym']).name)

    def read(address, size):
        for seg in elf.iter_segments():
            offset = address - seg['p_vaddr']
            if seg['p_type'] != 'PT_LOAD' or offset < 0:
                continue
            if offset + size <= seg['p_filesz']:
                return data[seg['p_offset']+offset:seg['p_offset']+offset+size]
            if seg['p_filesz'] <= offset and offset+size <= seg['p_memsz']:
                return bytes(size)
        raise ValueError('unmapped original I2C data')

    audit = module('i2c_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^' + re.escape(ENTRY) + '$'), True)
    report.pop('imports')
    f = report['selected_functions'][0]
    code = read(f['address'], f['size'])
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, ngu_fixture_sha256=ngu.FIXTURE_SHA,
                   scope='Original I2C state machine and MMIO requests; synthetic PCI, platform predicate, stream, timers and device replies',
                   functions=[dict(symbol=ENTRY, address=f['address'], size=len(code), code_hex=code.hex(),
                                    code_sha256=hashlib.sha256(code).hexdigest(), calls=f['calls'])],
                   objects={}, literals=f['literals'], relocations=[])
    literal_addresses = {x['address'] for x in f['literals']}
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    seen = set()
    for ins in decoder.disasm(code, f['address']):
        for op in ins.operands:
            if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP:
                continue
            address = ins.address + ins.size + op.mem.disp
            if address in seen or address in literal_addresses:
                continue
            seen.add(address)
            if address in symbols:
                sym = symbols[address]
                if sym.name not in TABLES + (SHIMADA,):
                    raise ValueError('unknown named I2C data')
                fixture['objects'][sym.name] = dict(address=address, size=sym['st_size'],
                                                     source_initial_hex=read(address, sym['st_size']).hex())
            elif address in relocations:
                fixture['relocations'].append(dict(address=address, **relocations[address]))
            else:
                raise ValueError('unknown RIP-relative I2C data')
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(), fixture_code_bytes=len(code),
                  objects=fixture['objects'], relocations=fixture['relocations'])
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('I2C fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['source_elf_sha256'] != base.SOURCE_SHA or fixture['ngu_fixture_sha256'] != ngu.FIXTURE_SHA or fixture['schema'] != 1:
        raise ValueError('I2C fixture source/dependency mismatch')
    return fixture


class I2cMachine(ngu.NguMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(ngu.load_fixture())
        combined['functions'].extend(fixture['functions'])
        combined['literals'].extend(fixture['literals'])
        combined['objects'].update(fixture['objects'])
        super().__init__(combined, inputs)
        self.new_fixture = fixture
        for name in TABLES:
            self.uc.mem_write(self.objects[name], bytes.fromhex(fixture['objects'][name]['source_initial_hex']))
        self.uc.mem_write(self.objects[SHIMADA], bytes([bool(inputs.get('shimada'))]))
        self.rx_buffer, self.tx_buffer = self.alloc(64), self.alloc(64)
        self.uc.mem_write(self.rx_buffer, b'\xcc'*64)
        self.tx_bytes = inputs.get('tx_bytes', [0x11, 0x22, 0x33, 0x44])
        self.uc.mem_write(self.tx_buffer, bytes(self.tx_bytes))
        self.cout, vtable, ctype = self.alloc(0x100), self.alloc(64), self.alloc(0x80)
        self.put64(self.cout, vtable+24)
        self.put64(vtable, 0)
        self.put64(self.cout+0xf0, ctype)
        self.ctype = ctype
        self.uc.mem_write(ctype+0x38, bytes([not inputs.get('uninitialized_ctype')]))
        self.uc.mem_write(ctype+0x43, b'\n')
        ctype_vtable = self.alloc(0x40)
        self.put64(ctype, ctype_vtable)
        for row in fixture['relocations']:
            self.map(row['address'], 8)
            if row['symbol'] == '_ZSt4cout' and row['type'] == 6:
                self.put64(row['address'], self.cout)
            elif not row['symbol'] and row['type'] == 8:
                self.put64(row['address'], row['addend'])
                self.put64(ctype_vtable+0x30, row['addend'])
            else:
                raise ValueError('unexpected I2C stream relocation')
        self.tag = None
        self.counts, self.rx_poll_counts = {}, {}
        self.rx_index = 0
        self.delays, self.diagnostics, self.stream_events = [], [], []
        self.table_access = None
        self.executed_writes = set()

    def prepare(self, symbol):
        if symbol != ENTRY:
            raise ValueError('unexpected I2C entry')
        for reg, value in zip(('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9'),
                              (self.inputs.get('bus', 0), self.inputs.get('slave', 0x50),
                               self.inputs.get('rx_count', 1), self.rx_buffer,
                               self.inputs.get('tx_count', 1), self.tx_buffer)):
            self.reg(reg, value)

    def hook(self, uc, pc, size, unused):
        if pc == 0x230f9b:
            table = self.reg('rdx')
            row = self.reg('rax') // 3
            name = next((n for n in TABLES if self.objects[n] == table), None)
            if name is None:
                raise ValueError('unrecognized original table base')
            self.table_access = dict(symbol=name, row=row, address=table+self.reg('rax')*4+8)
            if row >= self.new_fixture['objects'][name]['size'] // 12:
                self.stop('out-of-table-index')
                return
            self.table_access['base'] = struct.unpack('<I', uc.mem_read(self.table_access['address'], 4))[0]
        if pc == self.entries[ngu.RW_READ]:
            parent = self.u64(self.reg('rsp'))
            if parent not in READ_CONTEXTS:
                raise ValueError('unknown I2C read caller')
            self.tag = READ_CONTEXTS[parent]
        if pc == self.entries[ngu.RW_WRITE]:
            self.executed_writes.add(self.u64(self.reg('rsp'))-5)
        base.Machine.hook(self, uc, pc, size, unused)

    def reply(self):
        p, tag = self.inputs, self.tag
        self.counts[tag] = self.counts.get(tag, 0) + 1
        count = self.counts[tag]
        if tag == 'disable':
            return int(count <= p.get('disable_busy', 0))
        if tag == 'enable':
            return int(count > p.get('enable_busy', 0))
        if tag == 'outer-status':
            return 8 if count <= p.get('outer_status_blocked', 0) else 0
        if tag == 'outer-level':
            return 1 if count <= p.get('outer_level_blocked', 0) else 0
        if tag == 'abort':
            return 0x40 if count <= p.get('abort_busy', 0) else 0
        if tag == 'idle':
            return 0x20 if count <= p.get('idle_busy', 0) else 0
        if tag == 'rx-ready':
            self.rx_poll_counts[self.rx_index] = self.rx_poll_counts.get(self.rx_index, 0) + 1
            budget = p.get('rx_busy_per_byte', [0])
            busy = budget[min(self.rx_index, len(budget)-1)]
            return 8 if self.rx_poll_counts[self.rx_index] > busy else 0
        if tag == 'rx-data':
            value = (0xa0 + self.rx_index) & 255
            self.rx_index += 1
            return value
        if tag == 'clear':
            return 0
        raise ValueError('unclassified I2C response')

    def external(self, name):
        value = 0
        if name == 'write@plt' and self.reg('rdi') == 900:
            if self.reg('rdx') != 96 or self.reg('rsi') != self.objects['user_request']:
                raise ValueError('I2C request pointer/length changed')
            raw = bytes(self.uc.mem_read(self.reg('rsi'), 96))
            op, token, address, data = struct.unpack('<12Q', raw)[:4]
            if token != 71 or op not in (0x0c, 0x0d) or self.u64(self.mail):
                raise ValueError('invalid original I2C request')
            reply = self.reply() if op == 0x0c else 0
            mode = self.inputs.get('mailbox_mode', 'success')
            done = 0 if mode in ('no-completion', 'write-error', 'short-write') else ((-12 & ngu.U32) << 32) | 1 if mode == 'encoded-error' else 1
            value = -1 if mode in ('write-error', 'write-error-but-done') else 12 if mode == 'short-write' else 96
            self.put64(self.mail+8, reply)
            self.put64(self.mail, done)
            self.mail_io.append(dict(opcode=op, address=address, value=data if op == 0x0d else None, reply=reply,
                                     context=self.tag if op == 0x0c else None, request_hex=raw.hex(), done=done, write_result=value))
        elif name == '_Z9is_tr5_esv':
            value = int(bool(self.inputs.get('tr5_es')))
        elif name == 'usleep@plt':
            value = self.reg('rdi')
            if value not in (10, 100):
                raise ValueError('unexpected I2C delay')
            self.delays.append(value)
            value = 0
        elif name == INSERT:
            if self.reg('rdi') != self.cout:
                raise ValueError('wrong synthetic stream')
            raw = bytes(self.uc.mem_read(self.reg('rsi'), self.reg('rdx')))
            self.diagnostics.append(raw.rstrip(b'\0').decode('ascii'))
            value = self.cout
        elif name in ('_ZNSo3putEc@plt', '_ZNSo5flushEv@plt', ENDL):
            if self.reg('rdi') != self.cout:
                raise ValueError('wrong stream endpoint')
            if name == '_ZNSo3putEc@plt' and self.reg('rsi') != 10:
                raise ValueError('wrong newline')
            self.stream_events.append(name)
            value = self.cout
        elif name == '_ZNKSt5ctypeIcE13_M_widen_initEv@plt':
            if self.reg('rdi') != self.ctype:
                raise ValueError('wrong synthetic ctype')
            self.uc.mem_write(self.ctype+0x38, b'\x01')
            self.stream_events.append(name)
        else:
            return super().external(name)
        self.calls.append(name)
        self.ret(value)


def verify_all_call_sites(fixture, i2c_sites):
    clients = module('i2c_clients', 'legacy-mmio-clients.py')
    services = module('i2c_services', 'legacy-mmio-services.py')
    nf, cf, sf = ngu.load_fixture(), clients.load_fixture(), services.load_fixture()
    probes = [(ngu.NguMachine, nf, ngu.NGU, {}), (ngu.NguMachine, nf, ngu.NVL_WRITE, {}),
              (clients.ClientMachine, cf, clients.RAW, {}), (clients.ClientMachine, cf, clients.NVL_READ, {})]
    probes += [(services.ServiceMachine, sf, symbol, {}) for symbol in
               (services.FCH, services.QUERY, services.SPREAD, services.FSW, services.DYNAMIC)]
    expected, covered, rows = set(), set(i2c_sites), []
    callers = []
    for source in (nf, cf, sf, fixture):
        for f in source['functions']:
            sites = [c['instruction'] for c in f['calls'] if c['kind'] == 'call' and ngu.RW_WRITE in c['symbols']]
            if sites:
                expected.update(sites)
                callers.append(dict(symbol=f['symbol'], address=f['address'], code_sha256=f['code_sha256'], sites=sites))
    for machine, source, symbol, inputs in probes:
        m = machine(source, inputs)
        reached = set()
        non_call_returns = set()

        def observe(uc, address, size, unused):
            returned_to = m.u64(m.reg('rsp'))
            if returned_to-5 in expected:
                reached.add(returned_to-5)
            else:
                non_call_returns.add(returned_to)

        m.uc.hook_add(unicorn.UC_HOOK_CODE, observe, begin=m.entries[ngu.RW_WRITE], end=m.entries[ngu.RW_WRITE])
        result = m.run(symbol, instruction_limit=20000)
        if result['outcome'] != 'returned':
            raise AssertionError('cross-caller request probe did not return')
        covered.update(reached)
        rows.append(dict(symbol=symbol, inputs=inputs, sites=sorted(reached),
                         non_call_return_addresses=sorted(non_call_returns),
                         requests=[x['request_hex'] for x in m.mail_io], outcome=result['outcome']))
    if len(callers) != 10 or len(expected) != 28 or not expected <= covered:
        raise AssertionError(('incomplete 10-caller/28-CALL inventory', sorted(expected-covered)))
    return dict(scope='All 28 aligned direct CALLs in the existing 10-function Wr_MMIO(unsigned,unsigned) inventory; not indirect, inline, tail or all MMIO API coverage',
                caller_count=len(callers), direct_call_count=len(expected), callers=callers,
                covered_direct_calls=sorted(covered & expected),
                probes=rows, dependency_sha256=dict(ngu=ngu.FIXTURE_SHA, clients=clients.FIXTURE_SHA, services=services.FIXTURE_SHA))


def investigate(fixture):
    rows, sites = [], set()

    def case(inputs, expected='returned', expected_return=1, expected_diagnostic=None):
        m = I2cMachine(fixture, inputs)
        result = m.run(ENTRY, instruction_limit=50000, allow_instruction_bound=expected == 'instruction-limit')
        if result['outcome'] != expected:
            raise AssertionError(('I2C completion differs', inputs, result['outcome']))
        sites.update(m.executed_writes)
        if expected == 'returned' and m.reg('rax') != expected_return:
            raise AssertionError(('I2C return differs', inputs, m.reg('rax')))
        if expected_diagnostic and not any(expected_diagnostic in x for x in m.diagnostics):
            raise AssertionError(('missing original diagnostic', inputs, m.diagnostics))
        use_stp = inputs.get('shimada') or inputs.get('tr5_es')
        if m.table_access['symbol'] != TABLES[0 if use_stp else 1]:
            raise AssertionError('table selection differs')
        if (inputs.get('shimada') and '_Z9is_tr5_esv' in m.calls or
                not inputs.get('shimada') and '_Z9is_tr5_esv' not in m.calls):
            raise AssertionError('table predicate short-circuit differs')
        writes = [(r['address'], r['value']) for r in m.mail_io if r['opcode'] == 0x0d]
        reads = [r for r in m.mail_io if r['opcode'] == 0x0c]
        if expected == 'out-of-table-index':
            if m.mail_io or m.pci:
                raise AssertionError('out-of-table probe reached I/O')
        elif writes[0] != (0x6c, 0):
            raise AssertionError('I2C first absolute write differs')
        if reads and any(not (m.table_access['base'] <= r['address'] <= m.table_access['base']+0x9c) for r in reads):
            raise AssertionError('I2C read base differs')
        if writes and any(address > 0x6c for address, _ in writes):
            raise AssertionError('I2C write unexpectedly adds base')
        if expected == 'returned' and not expected_diagnostic:
            rx, tx = inputs.get('rx_count', 1), inputs.get('tx_count', 1)
            transfer = []
            if (rx+tx) & base.MASK:
                transfer = [v | (0x200 if n == tx-1 and not rx else 0) for n, v in enumerate(m.tx_bytes[:tx])]
                transfer += [0x100 if n != rx-1 else 0x300 for n in range(rx)]
            if [value for address, value in writes if address == 0x10] != transfer:
                raise AssertionError(('I2C transfer sequence differs', inputs, writes, transfer))
            received = bytes(m.uc.mem_read(m.rx_buffer, 64))
            count = 0 if not ((rx+tx) & base.MASK) else rx
            if received != bytes((0xa0+n) & 255 for n in range(count)) + b'\xcc'*(64-count):
                raise AssertionError('I2C receive buffer differs')
        rows.append(dict(inputs=inputs, table_access=m.table_access, return_u64=m.reg('rax'),
                         receive_hex=bytes(m.uc.mem_read(m.rx_buffer, 8)).hex(), counts=m.counts,
                         rx_poll_counts=m.rx_poll_counts, delays_us=m.delays, diagnostics=m.diagnostics,
                         stream_events=m.stream_events, mailbox_io=m.mail_io, pci=m.pci,
                         original_write_call_sites=sorted(m.executed_writes), **result))

    for shimada, tr5_es, bus in itertools.product((False, True), (False, True), range(6)):
        case(dict(shimada=shimada, tr5_es=tr5_es, bus=bus))
    for rx, tx, slave in itertools.product(range(4), range(4), (0, 0x50, 0x1ff)):
        case(dict(rx_count=rx, tx_count=tx, slave=slave))
    for bus in (6, 7, 255, 256, -1):
        case(dict(bus=bus), 'returned' if (bus & 255) < 6 else 'out-of-table-index')
    case(dict(rx_count=base.MASK, tx_count=1))
    for initialized in (False, True):
        for stage, good, bad, message, retval in (
                ('disable_busy', 200, 201, 'I2cDisable', 1),
                ('enable_busy', 200, 201, 'I2cEnable', 0),
                ('abort_busy', 10, 11, 'I2C TX ABRT', 0),
                ('idle_busy', 201, 202, 'I2c Idle', 0)):
            case({stage: good, 'uninitialized_ctype': initialized})
            case({stage: bad, 'uninitialized_ctype': initialized}, expected_return=retval, expected_diagnostic=message)
    for busy, retval in ((0, 1), (199, 1), (200, 1), (201, 0), (1000, 0)):
        case(dict(rx_busy_per_byte=[busy], rx_count=1), expected_return=retval,
             expected_diagnostic=None if retval else 'No Rx Out')
    case(dict(rx_count=3, rx_busy_per_byte=[100, 100, 0]))
    case(dict(rx_count=3, rx_busy_per_byte=[100, 100, 1]), expected_return=0, expected_diagnostic='No Rx Out')
    for key in ('outer_status_blocked', 'outer_level_blocked'):
        case({key: 3})
        case({key: 1000000}, 'instruction-limit')
    for mode in ('encoded-error', 'no-completion', 'write-error', 'short-write', 'write-error-but-done'):
        case(dict(mailbox_mode=mode), 'returned' if mode == 'write-error-but-done' else 'instruction-limit')
    expected_sites = {c['instruction'] for c in fixture['functions'][0]['calls'] if ngu.RW_WRITE in c['symbols']}
    if sites != expected_sites:
        raise AssertionError(('not all original I2C write CALLs exercised', sorted(expected_sites-sites)))
    coverage = verify_all_call_sites(fixture, sites)
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                ngu_fixture_sha256=ngu.FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(), python=platform.python_version(), unicorn=unicorn.__version__),
                cases_characterized=len(rows)+len(coverage['probes']), i2c_cases=len(rows),
                cross_caller_probe_cases=len(coverage['probes']), expected_bounded_loops=sum(r['outcome'] == 'instruction-limit' for r in rows),
                out_of_table_stops=sum(r['outcome'] == 'out-of-table-index' for r in rows),
                write_call_sites_exercised=sorted(sites), cross_caller_coverage=coverage, observations=rows)


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
            parser.error('extraction needs fixture and analysis paths')
        report, fixture = extract(args.binary)
        args.export_fixture.write_bytes(encoded(fixture))
        args.analysis_output.write_bytes(encoded(report))
        print('fixture SHA ' + report['fixture_sha256'], flush=True)
    else:
        fixture = load_fixture(args.fixture)
    result = investigate(fixture)
    args.output.write_bytes(encoded(result))
    print(str(result['cases_characterized']) + ' original I2C/caller cases; 10 callers/28 direct write sites; no hardware')


if __name__ == '__main__':
    main()
