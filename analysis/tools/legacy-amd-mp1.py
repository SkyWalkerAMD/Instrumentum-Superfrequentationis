#!/usr/bin/env python3
"""Original MP1 path, PCI identity helper and two AMD slots, with synthetic I/O.

No ELF entry point, Qt parser, port instruction, OS syscall or hardware runs.
Constructor slices only establish a local flag under stated preconditions.
"""
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
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


transport = module('mp1_transport', 'legacy-amd-transport.py')
base = transport.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-amd-mp1.json'
FIXTURE_SHA = '45e108afa40e7925c76687e7f9c475ab329814a8c9ac5849c135e7e927f25b07'
MP1 = '_Z10MP1_C2PMSGjjRj'
FIND = '_Z18FindPciDeviceById2jjj'
SLOT3 = '_ZN12cpufunctions29on_per_ccx_oc_apply_3_clickedEv'
SLOT19 = '_ZN12cpufunctions30on_per_ccx_oc_apply_19_clickedEv'
CTOR = '_ZN10AMD_PM_LOGC2Ev'
UICTOR = '_ZN12cpufunctionsC2EP7QWidget'
FLAG = 'AMD_PM_LOG.constructor.identity-flags'
COPY = 'cpufunctions.constructor.copy-flags'
FUNCTIONS = (MP1, FIND, SLOT3, SLOT19, '_Z17ReadPciConfigWordjj',
             '_Z16libpci_read_wordiiii', 'pci_read_word')
FLAG_BLOCKS = ((0x2cb08b, 0x2cb0c9), (0x2cd282, 0x2cd325),
               (0x2cf083, 0x2cf0ef), (0x2cfa80, 0x2cfa98), (0x2cffe1, 0x2d0024))
IDENTITIES = ((0x1022, 0x1630), (0x1002, 0x1630), (0x1002, 0x1636),
              (0x1022, 0x14b5), (0x1022, 0x14e8), (0x1022, 0x1480),
              (0x1022, 0x1450), (0x1022, 0x14d8), (0x1022, 0x14a4))
U32 = 0xffffffff


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

    names = FUNCTIONS + (CTOR, UICTOR)
    audit = module('mp1_audit', 'elf-runtime-audit.py')
    audited = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, names)) + ')$'), True)
    functions = {n: next(f for f in audited['selected_functions'] if n in f['symbols']) for n in names}
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA,
                   transport_fixture_sha256=transport.FIXTURE_SHA,
                   scope='Original MP1/identity/slots and bounded flag regions; synthetic PCI, Qt, libc and TSC boundaries',
                   functions=[], objects={}, literals=[])
    proofs, literals = [], {}
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True

    def add(parent, label, start=None, end=None, stop=None):
        f = functions[parent]
        start = f['address'] if start is None else start
        end = f['address'] + f['size'] if end is None else end
        if not f['address'] <= start < end <= f['address'] + f['size']:
            raise ValueError('slice outside original function')
        code = read(start, end - start)
        ins = list(decoder.disasm(code, start))
        if sum(i.size for i in ins) != len(code):
            raise ValueError('incomplete original decode')
        row = dict(symbol=label, address=start, size=len(code), code_hex=code.hex(),
                   code_sha256=hashlib.sha256(code).hexdigest(),
                   calls=[c for c in f['calls'] if start <= c['instruction'] < end],
                   parent_symbol=parent, parent_function_sha256=f['code_sha256'])
        if stop is not None:
            row['stop_address'] = stop
        fixture['functions'].append(row)
        proof = {k: v for k, v in row.items() if k != 'code_hex'}
        proof['instructions'] = [dict(address=i.address, mnemonic=i.mnemonic, operands=i.op_str) for i in ins]
        proofs.append(proof)
        for i in ins:
            for op in i.operands:
                if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP:
                    continue
                address = i.address + i.size + op.mem.disp
                if address in objects:
                    s = objects[address]
                    fixture['objects'][s.name] = dict(address=address, size=s['st_size'],
                                                     source_initial_hex=read(address, s['st_size']).hex())
                else:
                    raw = read(address, 256).split(b'\0')[0]
                    text = raw.decode('utf-8')
                    if not text or not all(c.isprintable() or c in '\n\t\r' for c in text):
                        raise ValueError('unknown original RIP-relative data')
                    literals[address] = text

    for name in FUNCTIONS:
        add(name, name)
    for n, (start, end) in enumerate(FLAG_BLOCKS):
        add(CTOR, FLAG if n == 0 else FLAG + '.' + str(n), start, end, 0x2cb0c9)
    add(UICTOR, COPY, 0x7629e9, 0x762a03, 0x762a03)
    fixture['literals'] = [dict(address=a, text=t) for a, t in sorted(literals.items())]
    fixture['flag_preconditions'] = {
        'rbx': 'synthetic AMD_PM_LOG object; bytes +0x78..+0x7b initially zero',
        'rsp': 'synthetic live constructor frame; writable local +0x58',
        'initial_zero_evidence': [i for i in functions[CTOR]['instructions']
                                  if i['address'] in (0x2c79a6, 0x2c79c4)],
        'copy': 'rbp=cpufunctions; embedded AMD_PM_LOG begins at +0x1f0',
        'embedding_evidence': [i for i in functions[UICTOR]['instructions']
                               if 0x761714 <= i['address'] <= 0x761723],
        'excluded': 'All preceding/following constructor bodies, intervening refresh calls, aliases and concurrent mutations'}
    report = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, scope=fixture['scope'],
                  fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                  flag_preconditions=fixture['flag_preconditions'], proofs=proofs)
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('MP1 fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA:
        raise ValueError('MP1 source/schema mismatch')
    return fixture


def combined(fixture):
    if fixture['transport_fixture_sha256'] != transport.FIXTURE_SHA:
        raise ValueError('transport dependency mismatch')
    result = copy.deepcopy(transport.load_fixture())
    if {f['address'] for f in result['functions']} & {f['address'] for f in fixture['functions']}:
        raise ValueError('duplicate executable span')
    result['functions'].extend(fixture['functions'])
    for name, obj in fixture['objects'].items():
        if name in result['objects'] and result['objects'][name] != obj:
            raise ValueError('object dependency mismatch')
        result['objects'][name] = obj
    result['literals'].extend(fixture['literals'])
    return result


class Mp1Machine(transport.TransportMachine):
    def __init__(self, fixture, inputs):
        super().__init__(combined(fixture), inputs)
        for name, key in (('GLOBAL_IS_SHIMADA', 'shimada'), ('GLOBAL_IS_GPT', 'gpt'),
                          ('GLOBAL_IS_GRANITE', 'granite')):
            self.uc.mem_write(self.objects[name], bytes([inputs.get(key, 0)]))
        self.status_reads = 0
        self.identity_reads = []
        self.find_queries = []
        self.qt_reads = 0
        self.conversions = []
        self.command_calls = []
        self.this = self.alloc(0x1100)
        self.arg_out = self.alloc(8)
        self.put32(self.arg_out, 0xa5a5a5a5)
        self.uc.mem_write(self.this + 0xf88, bytes([inputs.get('object_flag', 0)]))
        self.phase = None

    def prepare(self, symbol):
        p = self.inputs
        self.phase = symbol
        if symbol == MP1:
            self.reg('rdi', p.get('command', 0x18))
            self.reg('rsi', p.get('argument', 1))
            self.reg('rdx', self.arg_out)
        elif symbol == FIND:
            for reg, value in zip(('rdi', 'rsi', 'rdx'), p.get('query', [0x1022, 0x1480, 7])):
                self.reg(reg, value)
        elif symbol in (SLOT3, SLOT19):
            self.reg('rdi', self.this)
            if symbol == SLOT19:
                ui, widget = self.alloc(0x930), self.alloc(32)
                self.put64(self.this + 0x1008, ui)
                self.put64(ui + 0x268, widget)
                self.widget = widget
        elif symbol == FLAG:
            self.reg('rbx', self.this + 0x1f0)
            # Only the original identity region executes; the source's earlier
            # qword initializer clears these four bytes (recorded in fixture).
            self.uc.mem_write(self.this + 0x268, bytes(4))
            self.reg('rsp', base.STACK + 0xf000)
        elif symbol == COPY:
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_RBP, self.this)
        else:
            raise ValueError('unexpected MP1 entry')

    def hook(self, uc, pc, size, unused):
        stop = 0x2cb0c9 if self.phase == FLAG else 0x762a03 if self.phase == COPY else None
        if pc == stop:
            self.steps += 1
            self.stop('region-end')
            return
        if pc == self.entries[FIND]:
            self.find_queries.append([self.reg(r) & U32 for r in ('rdi', 'rsi', 'rdx')])
        if pc in (self.entries[MP1], self.entries[transport.CMD]):
            self.command_calls.append(dict(symbol=MP1 if pc == self.entries[MP1] else transport.CMD,
                                           command=self.reg('rdi'), argument=self.reg('rsi')))
        super().hook(uc, pc, size, unused)

    def pci_backend(self, write):
        dev, offset, buffer, size = (self.reg(r) for r in ('rdi', 'rsi', 'rdx', 'rcx'))
        mode = self.inputs.get('mode', 'success')
        domain = struct.unpack('<I', self.uc.mem_read(dev + 0xf0, 4))[0]
        bdf = list(self.uc.mem_read(dev + 0xa, 3))
        if domain or bdf != [0, 0, 0]:
            raise ValueError('original path no longer targets domain0/BDF0')
        if size == 2 and not write and offset in (0, 2):
            value = self.inputs.get('vendor' if offset == 0 else 'device', 0xffff) & 0xffff
            result = 0 if self.inputs.get('identity_error') else 1
            if result:
                self.uc.mem_write(buffer, struct.pack('<H', value))
            row = dict(offset=offset, value=value if result else None, backend_result=result,
                       domain=domain, bdf=bdf)
            self.identity_reads.append(row)
            self.trace.append({'identity_read': row})
            self.ret(result)
            return
        if size != 4 or offset not in (0xb8, 0xbc, 0xf8, 0xfc):
            raise ValueError('unexpected synthetic PCI request')
        result = 1
        if write:
            value = struct.unpack('<I', self.uc.mem_read(buffer, 4))[0]
            if offset in (0xb8, 0xf8):
                self.selected = value
            if mode == 'write-error':
                result = 0
        else:
            if offset not in (0xbc, 0xfc):
                raise ValueError('unexpected data read')
            self.pci_reads += 1
            # Reading the selected response and argument addresses is checked
            # from the original preceding index writes, not by total read order.
            status_addresses = (0x3b10a80, 0x3b10970, 0x3b10570, self.config['SMU_IOPORT'])
            status = self.selected in status_addresses
            if status:
                self.status_reads += 1
                sequence = self.inputs.get('statuses', [1])
                value = sequence[min(self.status_reads - 1, len(sequence) - 1)]
            else:
                value = self.inputs.get('reply', 0xdeadbeef)
            if mode == 'read-error' or mode == ('status-read-error' if status else 'argument-read-error'):
                result, value = 0, None
            else:
                self.put32(buffer, value)
        row = dict(op='write' if write else 'read', domain=domain, bdf=bdf, offset=offset,
                   attempted_index=self.selected, value=value, backend_result=result)
        self.pci.append(row)
        self.trace.append({'pci': row})
        self.ret(result)

    def external(self, name):
        p = self.inputs
        value = 0
        if name == 'usleep@plt':
            delay = self.reg('rdi')
            if delay not in (1000, 2000, 100000):
                raise ValueError('unexpected delay')
            value = -1 if p.get('mode') == 'sleep-error' else 0
            self.trace.append(dict(sleep_us=delay, result=value))
        elif name == '_ZNK9QLineEdit4textEv':
            if self.reg('rsi') != self.widget:
                raise ValueError('wrong original Ui member')
            lengths = p.get('lengths', [1, 1])
            if self.qt_reads >= len(lengths):
                raise ValueError('unexpected repeated text access')
            descriptor = self.alloc(32)
            self.put32(descriptor, p.get('refcount', -1))
            self.put32(descriptor + 4, lengths[self.qt_reads])
            self.put64(self.reg('rdi'), descriptor)
            self.qt_reads += 1
        elif name == '_ZNK7QString5toIntEPbi':
            if self.reg('rsi') != 0 or self.reg('rdx') != 10:
                raise ValueError('Qt parser arguments changed')
            value = p.get('parsed_int', 0)
            self.conversions.append(dict(base=10, ok_pointer=0, synthetic_result=value))
        elif name == '_ZN11QMessageBox11informationEP7QWidgetRK7QStringS4_6QFlagsINS_14StandardButtonEES6_':
            self.events.append({'information_boundary': True})
        else:
            return super().external(name)
        self.calls.append(name)
        self.ret(value)


def mp1_addresses(inputs):
    identity = (inputs.get('vendor', 0xffff), inputs.get('device', 0xffff))
    if inputs.get('identity_error'):
        identity = (0xffff, 0xffff)
    if identity in IDENTITIES[:5] or inputs.get('gpt', 0):
        return (0x3b10a20, 0x3b10a80, 0x3b10a88)
    return (0x3b10924, 0x3b10970, 0x3b10a40) if inputs.get('shimada', 0) else (0x3b10524, 0x3b10570, 0x3b10a40)


def investigate(fixture):
    rows = []

    def case(symbol, inputs, derive=False):
        m = Mp1Machine(fixture, inputs)
        phases = []
        if derive:
            for phase in (FLAG, COPY):
                m.outcome, m.steps = None, 0
                r = m.run(phase)
                if r['outcome'] != 'region-end':
                    raise AssertionError('flag region escaped')
                phases.append(dict(symbol=phase, steps=r['steps']))
        flag = bytes(m.uc.mem_read(m.this + 0xf88, 1))[0]
        if derive:
            identity = (inputs.get('vendor', 0xffff), inputs.get('device', 0xffff))
            expected_flag = bool(inputs.get('gpt') or identity == (0x1022, 0x14e8))
            if flag != int(expected_flag):
                raise AssertionError(('constructor-local flag derivation differs', inputs, flag, expected_flag))
        m.outcome, m.steps = None, 0
        r = m.run(symbol, instruction_limit=20000)
        if r['outcome'] != 'returned':
            raise AssertionError('MP1 path did not return')
        active = symbol == MP1 or (symbol == SLOT3 and flag)
        writes = [(x['attempted_index'], x['value']) for x in m.pci if x['op'] == 'write' and x['offset'] in (0xbc, 0xfc)]
        if active:
            cmd, status, arg = mp1_addresses(inputs)
            c, a = ((0x18, 1) if symbol == SLOT3 else (inputs.get('command', 0x18) & U32, inputs.get('argument', 1) & U32))
            if writes != [(0x50200, 1), (status, 0), (arg, a), (status, 0), (arg, a), (cmd, c)]:
                raise AssertionError('MP1 original write sequence differs')
            sequence = inputs.get('statuses', [1])
            failed = inputs.get('mode') in ('read-error', 'status-read-error')
            n = 21 if failed else next((i + 1 for i in range(21) if sequence[min(i, len(sequence) - 1)] == 1), 21)
            if m.status_reads != n or m.pci_reads != n + 1:
                raise AssertionError('MP1 polling limit differs')
            if len([x for x in m.pci if x['op'] == 'write']) != 14:
                raise AssertionError('MP1 selector/write count differs')
            if [x['value'] for x in m.trace if 'port_write' in x] != [1, 0]:
                raise AssertionError('MP1 lock/release differs')
            if sum(x.get('sleep_us') == 100000 for x in m.trace) != 1:
                raise AssertionError('MP1 fixed pre-read delay differs')
            wait_index = next(i for i, x in enumerate(m.trace) if x.get('sleep_us') == 100000)
            if any('sleep_us' in x for x in m.trace[wait_index + 1:]):
                raise AssertionError('unexpected delay inside/after MP1 polling')
            if symbol == MP1:
                expected = U32 if failed else sequence[min(n - 1, len(sequence) - 1)]
                if m.reg('rax') != expected:
                    raise AssertionError('MP1 return differs')
                reply = U32 if inputs.get('mode') in ('read-error', 'argument-read-error') else inputs.get('reply', 0xdeadbeef)
                if struct.unpack('<I', m.uc.mem_read(m.arg_out, 4))[0] != reply:
                    raise AssertionError('MP1 output differs')
        if symbol == SLOT3:
            if 'Applied!' not in r['qt_texts'] or len(m.command_calls) != 1:
                raise AssertionError('original unconditional notification differs')
            if m.command_calls[0] != dict(symbol=MP1 if flag else transport.CMD, command=0x18 if flag else 0x25, argument=1):
                raise AssertionError('slot3 routing differs')
        if symbol == SLOT19:
            enabled = bool(inputs.get('lengths', [1, 1])[1])
            expected_calls = [dict(symbol=transport.CMD, command=0x56,
                                   argument=(inputs.get('parsed_int', 0) & U32) - (1 << 32) if inputs.get('parsed_int', 0) & (1 << 31) else inputs.get('parsed_int', 0) & U32)] if enabled else []
            for call in expected_calls:
                call['argument'] &= base.MASK
            if m.command_calls != expected_calls or m.qt_reads != 2 or len(m.conversions) != 1 or r['qt_texts']:
                raise AssertionError('slot19 conversion/emptiness/feedback differs')
            if not enabled and m.pci:
                raise AssertionError('empty second read performed transport')
            if enabled:
                arg = expected_calls[0]['argument']
                by_address = dict(writes)
                if (by_address.get(m.config['SMU_ARG0']) != arg & U32 or
                        by_address.get(m.config['SMU_ARG1']) != (arg >> 32) & U32):
                    raise AssertionError('signed slot input changed before PCI writes')
        if symbol == FIND:
            vendor, device, _ = inputs['query']
            actual = (inputs.get('vendor', 0xffff), inputs.get('device', 0xffff))
            if inputs.get('identity_error'):
                actual = (0xffff, 0xffff)
            expected = 0 if actual == (vendor, device) else U32
            if m.reg('rax') != expected or m.pci:
                raise AssertionError('identity helper enumerated or returned different result')
        rows.append(dict(symbol=symbol, inputs=inputs, derived_flag=bool(derive), flag_value=flag,
                         phases=phases, return_u32=m.reg('rax') & U32,
                         command_calls=m.command_calls, identity_queries=m.find_queries,
                         identity_reads=m.identity_reads, status_reads=m.status_reads,
                         qt_conversions=m.conversions, qt_deallocations=len(m.deleted),
                         trace=m.trace, **r))

    # Every ID branch, fallbacks and original byte predicates; no marketing IDs.
    for identity, shimada, gpt, granite in itertools.product(IDENTITIES + ((0xffff, 0xffff),), (0, 1), (0, 1), (0, 1)):
        case(MP1, dict(vendor=identity[0], device=identity[1], shimada=shimada, gpt=gpt, granite=granite))
    for value in list(range(256)) + [0x100, 0x80000000, U32]:
        case(MP1, dict(statuses=[value]))
    for position in (1, 2, 3, 20, 21, 22):
        case(MP1, dict(statuses=[0] * (position - 1) + [1]))
    for command, argument in itertools.product((0, 255, 256, 0x12345678, U32), (0, 1, U32, 0x1234567887654321)):
        case(MP1, dict(command=command, argument=argument))
    for mode in ('write-error', 'read-error', 'status-read-error', 'argument-read-error', 'sleep-error'):
        case(MP1, dict(mode=mode, busy_reads=100, needs_init=True))
        for flag in (0, 1):
            case(SLOT3, dict(mode=mode, object_flag=flag, statuses=[0], busy_reads=100))
    for identity, shimada, gpt, granite in itertools.product(IDENTITIES + ((0xffff, 0xffff),), (0, 1), (0, 1), (0, 1)):
        case(SLOT3, dict(vendor=identity[0], device=identity[1], shimada=shimada, gpt=gpt, granite=granite), derive=True)
    for flag in (2, 255):
        case(SLOT3, dict(object_flag=flag, statuses=[U32]))
    for parsed, lengths, refcount in itertools.product((0, 1, 255, 256, 0x7fffffff, -1, -2147483648),
                                                       ([0, 0], [1, 1], [0, 1], [1, 0]), (-1, 1)):
        case(SLOT19, dict(parsed_int=parsed, lengths=lengths, refcount=refcount))
    for mode in ('write-error', 'read-error', 'sleep-error'):
        case(SLOT19, dict(parsed_int=-1, mode=mode))
    for identity, query_index in itertools.product(IDENTITIES + ((0xffff, 0xffff),), (0, 7, U32)):
        case(FIND, dict(vendor=identity[0], device=identity[1], query=[*identity, query_index]))
    for identity in ((0x1022, 0x1480), (0x8086, 0x1480), (0xffff, 0xffff)):
        for error in (False, True):
            case(FIND, dict(vendor=identity[0], device=identity[1], query=[0x1022, 0x1480, 0], identity_error=error))
            case(MP1, dict(vendor=identity[0], device=identity[1], identity_error=error))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                transport_fixture_sha256=transport.FIXTURE_SHA,
                scope=fixture['scope'] + '; flag slices do not execute full constructors or intervening refresh',
                environment=dict(system=platform.system(), python=platform.python_version(), unicorn=unicorn.__version__),
                cases_characterized=len(rows), observations=rows)


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
    print(str(result['cases_characterized']) + ' MP1/AMD slot cases; synthetic I/O only')


if __name__ == '__main__':
    main()
