#!/usr/bin/env python3
"""Original NVL profile import gates; stop before device actions or oversized I/O."""
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
from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_REG_RBP
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
import unicorn


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


msr = module('profile_msr', 'emulate-legacy-msr.py')
base = msr.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-nvl-profile.json'
FIXTURE_SHA = '70f812290be3721799ac44f142ee74ac91941096ea5be27d5d3422ba20ce8a9d'
LOAD = '_ZN10intel_ctl624on_pushButton_12_clickedEv'
SAVE = '_ZN10intel_ctl624on_pushButton_11_clickedEv'
SAVE_SLICE = 'nvl-profile-save-write-slice'
PROFILE_SIZE = 0x18f2
PROFILE_DISP = -0x1930
CANARY_DISP = -0x38
FIRST_DEVICE_ACTION = '_Z18percoreoverride_env'
READ = '_ZNSi4readEPcl@plt'
WRITE = '_ZNSo5writeEPKcl@plt'
FILEBUF_OPEN = '_ZNSt13basic_filebufIcSt11char_traitsIcEE4openEPKcSt13_Ios_Openmode@plt'
FILEBUF_CLOSE = '_ZNSt13basic_filebufIcSt11char_traitsIcEE5closeEv@plt'


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
        if isinstance(sec, RelocationSection):
            syms = elf.get_section(sec['sh_link'])
            for rel in sec.iter_relocations():
                relocations[rel['r_offset']] = dict(type=rel['r_info_type'], addend=rel['r_addend'],
                                                    symbol=syms.get_symbol(rel['r_info_sym']).name)

    def read(address, size):
        for seg in elf.iter_segments():
            offset = address-seg['p_vaddr']
            if seg['p_type'] != 'PT_LOAD' or offset < 0:
                continue
            if offset+size <= seg['p_filesz']:
                return data[seg['p_offset']+offset:seg['p_offset']+offset+size]
            if seg['p_filesz'] <= offset and offset+size <= seg['p_memsz']:
                return bytes(size)
        raise ValueError('original profile bytes unavailable')

    audit = module('profile_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, (LOAD, SAVE, FIRST_DEVICE_ACTION))) + ')$'), True)
    report.pop('imports')
    loader = next(f for f in report['selected_functions'] if LOAD in f['symbols'])
    saver = next(f for f in report['selected_functions'] if SAVE in f['symbols'])
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, msr_fixture_sha256=msr.FIXTURE_SHA,
                   scope='Original NVL import entry through original percoreoverride_en and Wrmsr to first MSR file-write boundary; original 24-byte exporter write slice. Synthetic Qt, streams, libc and controlled buffer contents; no host I/O, subsequent device actions or oversized copy',
                   functions=[], objects={}, literals=loader['literals'], relocations=[], data=[])
    code = read(loader['address'], loader['size'])
    fixture['functions'].append(dict(symbol=LOAD, address=loader['address'], size=len(code), code_hex=code.hex(),
                                     code_sha256=hashlib.sha256(code).hexdigest(), calls=loader['calls']))
    start, end = 0x6177ca, 0x6177e2
    raw = read(start, end-start)
    calls = [c for c in saver['calls'] if start <= c['instruction'] < end]
    if len(calls) != 1 or WRITE not in calls[0]['symbols']:
        raise ValueError('original exporter slice changed')
    fixture['functions'].append(dict(symbol=SAVE_SLICE, address=start, size=len(raw), code_hex=raw.hex(),
                                     code_sha256=hashlib.sha256(raw).hexdigest(), calls=calls))
    helper = next(f for f in report['selected_functions'] if FIRST_DEVICE_ACTION in f['symbols'])
    raw = read(helper['address'], helper['size'])
    fixture['functions'].append(dict(symbol=FIRST_DEVICE_ACTION, address=helper['address'], size=len(raw), code_hex=raw.hex(),
                                     code_sha256=hashlib.sha256(raw).hexdigest(), calls=helper['calls']))
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    seen, profile_refs = set(), []
    literal_addresses = {x['address'] for x in fixture['literals']}
    for ins in decoder.disasm(code, loader['address']):
        for op in ins.operands:
            if op.type != X86_OP_MEM:
                continue
            if op.mem.base == X86_REG_RBP and PROFILE_DISP <= op.mem.disp < CANARY_DISP:
                profile_refs.append(dict(instruction=ins.address, mnemonic=ins.mnemonic, operands=ins.op_str,
                                         relative_offset=op.mem.disp-PROFILE_DISP, width=op.size, capstone_access=op.access))
            if op.mem.base != X86_REG_RIP:
                continue
            address = ins.address+ins.size+op.mem.disp
            if address in seen or address in literal_addresses:
                continue
            seen.add(address)
            if address in symbols:
                sym = symbols[address]
                fixture['objects'][sym.name] = dict(address=address, size=sym['st_size'], source_initial_hex=read(address, sym['st_size']).hex())
            elif address in relocations:
                fixture['relocations'].append(dict(address=address, **relocations[address]))
            elif ins.mnemonic == 'movsd' and op.size == 8:
                raw = read(address, 8)
                fixture['data'].append(dict(address=address, bytes_hex=raw.hex(), binary64=struct.unpack('<d', raw)[0]))
            else:
                raise ValueError('unknown profile RIP-relative data')
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                  profile_size_from_export_slice=PROFILE_SIZE, canary_distance=CANARY_DISP-PROFILE_DISP,
                  profile_stack_references=profile_refs,
                  profile_reference_scope='Direct RBP-relative operands in the profile frame interval only; aliases and indexed tables require separate analysis; not a complete format definition')
    return report, fixture


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('NVL profile fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA or fixture['msr_fixture_sha256'] != msr.FIXTURE_SHA:
        raise ValueError('profile source/schema mismatch')
    return fixture


class ProfileMachine(msr.MsrMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(fixture)
        leaves = msr.load_fixture()
        combined['functions'].extend(f for f in leaves['functions'] if f['symbol'] == '_Z5Wrmsrjjj')
        combined['literals'].extend(leaves['literals'])
        super().__init__(combined, inputs)
        self.file_events = []
        self.file_opens = self.file_closes = self.tells = 0
        self.read_request = self.save_request = None
        self.first_msr_write = None
        self.profile_buffer = base.STACK+0xfff0+PROFILE_DISP
        self.uc.mem_write(self.profile_buffer, bytes([inputs.get('initial_buffer_byte', 0xa5)])*PROFILE_SIZE)
        self.virtual_tables = {}
        for row in fixture['relocations']:
            if row['type'] != 6 or not row['symbol'].startswith(('_ZTV', '_ZTT')):
                raise ValueError('unexpected profile relocation')
            ptr = self.alloc(0x200)
            self.virtual_tables[row['symbol']] = ptr
            self.map(row['address'], 8)
            self.put64(row['address'], ptr)
        ifstream = self.virtual_tables['_ZTVSt14basic_ifstreamIcSt11char_traitsIcEE']
        self.put64(ifstream, 0x100)
        vtt = self.virtual_tables['_ZTTSt14basic_ifstreamIcSt11char_traitsIcEE']
        self.put64(vtt+8, ifstream+0x18)
        self.put64(vtt+0x10, ifstream+0x40)
        self.put64(self.virtual_tables['_ZTVSi'], 0x10)
        for row in fixture['data']:
            self.map(row['address'], 8)
            self.uc.mem_write(row['address'], bytes.fromhex(row['bytes_hex']))

    def prepare(self, symbol):
        if symbol == SAVE_SLICE:
            frame = base.STACK+0xe000
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_RBP, frame)
            self.put64(frame-0x1ee0, self.alloc(32))
        elif symbol != LOAD:
            raise ValueError('unexpected profile entry')

    def cstring(self, ptr):
        raw = bytearray()
        for n in range(256):
            value = bytes(self.uc.mem_read(ptr+n, 1))
            if value == b'\0':
                return raw.decode('utf-8')
            raw.extend(value)
        raise ValueError('unterminated profile string')

    def descriptor(self, text):
        raw = text.encode('utf-8')
        ptr = self.alloc(32+len(raw)+1)
        self.put32(ptr, -1)
        self.put32(ptr+4, len(raw))
        self.put64(ptr+0x10, 24)
        self.uc.mem_write(ptr+24, raw+b'\0')
        return ptr

    def external(self, name):
        p, value = self.inputs, 0
        if name == '_ZNK11QMetaObject2trEPKcS1_i':
            text = self.cstring(self.reg('rdx'))
            self.qt_texts.append(text)
            self.put64(self.reg('rdi'), self.descriptor(text))
        elif name.startswith('_ZN11QFileDialog15getOpenFileName'):
            self.put64(self.reg('rdi'), self.descriptor('' if p.get('cancel') else 'fixture.pro'))
        elif name in ('_ZN5QFileC1ERK7QString', '_ZN5QFileC2ERK7QString', '_ZN5QFileD1Ev', '_ZN5QFileD2Ev', '_ZN11QFileDevice5closeEv'):
            self.file_events.append(dict(operation=name))
        elif name == '_ZN5QFile4openE6QFlagsIN9QIODevice12OpenModeFlagEE':
            if self.reg('rsi') != 0x11:
                raise ValueError('QFile open mode changed')
            value = int(p.get('qt_open', not p.get('cancel', False)))
            self.file_events.append(dict(operation='QFile.open', flags=0x11, result=value))
        elif name == '_ZN7QString13toUtf8_helperERKS_':
            self.put64(self.reg('rdi'), self.descriptor('fixture.pro'))
        elif name.startswith('_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE12_M_construct'):
            raw = bytes(self.uc.mem_read(self.reg('rsi'), self.reg('rdx')-self.reg('rsi')))
            self.cpp_string(self.reg('rdi'), raw.decode('utf-8'))
        elif name == FILEBUF_OPEN:
            self.file_opens += 1
            flags = 8 if self.file_opens == 1 else 12
            if self.reg('rdx') != flags or self.cstring(self.reg('rsi')) != 'fixture.pro':
                raise ValueError('C++ reopen path/mode changed')
            opened = p.get('first_open' if self.file_opens == 1 else 'second_open', True)
            value = self.reg('rdi') if opened else 0
            self.file_events.append(dict(operation='filebuf.open', ordinal=self.file_opens, flags=flags, result=value))
        elif name == FILEBUF_CLOSE:
            self.file_closes += 1
            value = 0 if p.get('close_error') else self.reg('rdi')
            self.file_events.append(dict(operation='filebuf.close', ordinal=self.file_closes, result=value))
        elif name == '_ZNSi5tellgEv@plt':
            self.tells += 1
            default = 0 if self.tells == 1 else PROFILE_SIZE
            if not p.get('first_open', True):
                default = -1
            value = p.get('tell_first' if self.tells == 1 else 'tell_last', default)
            self.file_events.append(dict(operation='tellg', ordinal=self.tells, synthetic_result=value))
        elif name == '_ZNSi5seekgElSt12_Ios_Seekdir@plt':
            if self.reg('rsi') or self.reg('rdx') != 2:
                raise ValueError('profile seek changed')
            value = self.reg('rdi')
        elif name == '_ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate@plt':
            self.put32(self.reg('rdi')+0x20, self.reg('rsi'))
            self.file_events.append(dict(operation='ios.clear', state=self.reg('rsi')))
        elif name == READ:
            count, ptr = self.reg('rdx'), self.reg('rsi')
            if ptr != self.profile_buffer:
                raise ValueError('profile stack buffer moved')
            self.read_request = dict(requested_bytes=count, buffer=ptr, exporter_bytes=PROFILE_SIZE,
                                     canary_distance=CANARY_DISP-PROFILE_DISP,
                                     reaches_canary_if_fully_copied=count > CANARY_DISP-PROFILE_DISP)
            if count > PROFILE_SIZE:
                self.stop('oversize-read-boundary')
                return
            actual = min(count, p.get('actual_read', count))
            self.uc.mem_write(ptr, bytes([p.get('payload_byte', 0)])*actual)
            self.read_request.update(synthetic_bytes_delivered=actual, unchecked_short_read=actual<count)
            self.file_events.append(dict(operation='istream.read', requested=count, synthetic_delivered=actual))
            value = self.reg('rdi')
        elif name == WRITE:
            self.save_request = dict(count=self.reg('rdx'), buffer=self.reg('rsi'))
            self.stop('export-write-boundary')
            return
        elif name == 'write@plt':
            if self.reg('rdi') != 700 or self.reg('rdx') != 8:
                raise ValueError('unexpected original first MSR write')
            low, high = struct.unpack('<II', self.uc.mem_read(self.reg('rsi'), 8))
            self.first_msr_write = dict(path='/dev/cpu/0/msr', offset=0x150, low=low, high=high,
                                        request_hex=bytes(self.uc.mem_read(self.reg('rsi'), 8)).hex())
            if (low, high) != (0, 0x80000014):
                raise ValueError('original first profile hardware command changed')
            self.stop('first-msr-write-boundary')
            return
        elif name == '_ZN7QStringC1EPKc' or name == '_ZN7QStringC2EPKc':
            text = self.cstring(self.reg('rsi'))
            self.qt_texts.append(text)
            self.put64(self.reg('rdi'), self.descriptor(text))
        elif name == '_ZN11QMessageBox11informationEP7QWidgetRK7QStringS4_6QFlagsINS_14StandardButtonEES6_':
            self.events.append(dict(information_boundary=True))
        elif name in ('_ZNSt8ios_baseC2Ev@plt', '_ZNSt8ios_baseD2Ev@plt',
                      '_ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E@plt',
                      '_ZNSt13basic_filebufIcSt11char_traitsIcEEC1Ev@plt',
                      '_ZNSt13basic_filebufIcSt11char_traitsIcEED1Ev@plt',
                      '_ZNSt12__basic_fileIcED1Ev@plt', '_ZNSt6localeD1Ev@plt', '_ZN7QStringD2Ev'):
            pass
        else:
            return super().external(name)
        self.calls.append(name)
        self.ret(value)


def investigate(fixture):
    rows = []

    def case(inputs, expected, symbol=LOAD):
        m = ProfileMachine(fixture, inputs)
        result = m.run(symbol, instruction_limit=15000)
        if result['outcome'] != expected:
            raise AssertionError(('profile gate differs', inputs, result['outcome']))
        if expected == 'first-msr-write-boundary':
            if m.read_request['requested_bytes'] != PROFILE_SIZE or 'Applied!' in result['qt_texts']:
                raise AssertionError('unexpected first device action')
        if expected == 'export-write-boundary' and m.save_request['count'] != PROFILE_SIZE:
            raise AssertionError('original fixed exporter size differs')
        if expected == 'returned' and m.read_request:
            raise AssertionError('rejected profile was read')
        sample = bytes(m.uc.mem_read(m.profile_buffer, PROFILE_SIZE))
        rows.append(dict(symbol=symbol, inputs=inputs, read_request=m.read_request, save_request=m.save_request,
                         file_events=m.file_events, first_msr_write=m.first_msr_write, msr_libc_events=m.io,
                         buffer_sha256=hashlib.sha256(sample).hexdigest(),
                         buffer_first16=sample[:16].hex(), buffer_last16=sample[-16:].hex(), **result))

    case({}, 'export-write-boundary', SAVE_SLICE)
    for length in (0, 1, 6384, 6385, 6386, 6387, 6392, 6393, 8192, 1<<32, (1<<63)-1):
        expected = 'returned' if length < PROFILE_SIZE else 'first-msr-write-boundary' if length == PROFILE_SIZE else 'oversize-read-boundary'
        case(dict(tell_last=length), expected)
    for delivered, fill, seed in itertools.product((0, 1, 4, PROFILE_SIZE-1, PROFILE_SIZE), (0, 255), (0x5a, 0xa5)):
        case(dict(actual_read=delivered, payload_byte=fill, initial_buffer_byte=seed), 'first-msr-write-boundary')
    for inputs in (dict(cancel=True), dict(qt_open=False), dict(first_open=False), dict(second_open=False)):
        case(inputs, 'returned')
    case(dict(close_error=True), 'first-msr-write-boundary')
    for first, last in ((-1, -1), (0, -1), (-1, 6385), (1, 6386), (1, 6387)):
        count = (last-first) & base.MASK
        expected = 'returned' if count < PROFILE_SIZE else 'first-msr-write-boundary' if count == PROFILE_SIZE else 'oversize-read-boundary'
        case(dict(tell_first=first, tell_last=last), expected)
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(), python=platform.python_version(), unicorn=unicorn.__version__),
                cases_characterized=len(rows), profile_export_bytes=PROFILE_SIZE, observations=rows)


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
    print(str(result['cases_characterized']) + ' original NVL profile gates; no hardware or oversized copy')


if __name__ == '__main__':
    main()
