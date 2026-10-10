#!/usr/bin/env python3
"""Original NVL ratio query/write, Save prefix and Load continuation; synthetic I/O."""
import argparse
from collections import Counter
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
from elftools.elf.elffile import ELFFile
import unicorn

import importlib.util
spec = importlib.util.spec_from_file_location('nvl_cores', Path(__file__).with_name('legacy-nvl-cores.py'))
cores = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cores)
profile, msr, base = cores.profile, cores.msr, cores.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-nvl-ratios.json'
FIXTURE_SHA = '3866b2f640411b665750df660b7a14fc39f4b5705f63895dc0978bdd593030c1'
SET = '_ZN11NVL_MEM_CFG22Set_OcTurboRatioLimitsEbiRSt6vectorIiSaIiEES3_'
GET = '_ZN11NVL_MEM_CFG22Get_OcTurboRatioLimitsEbiRSt6vectorIiSaIiEES3_'
GROW = '_ZNSt6vectorIiSaIiEE17_M_realloc_insertIJiEEEvN9__gnu_cxx17__normal_iteratorIPiS1_EEDpOT_'
HELPERS = (SET, GET, GROW)
SAVE_RATIO = 'nvl-save-ratio-prefix'
SAVE_START, SAVE_END, LOAD_END = 0x615630, 0x6168b4, 0x618ee6
RATIO_START, RATIO_END = 0x1638, 0x1678


def extract(path):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    audit = profile.module('ratio_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, HELPERS + (profile.SAVE,))) + ')$'), True)
    elf = ELFFile(io.BytesIO(raw))

    def read(start, end):
        segment = next(s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and
                       s['p_vaddr'] <= start and end <= s['p_vaddr'] + s['p_filesz'])
        offset = segment['p_offset'] + start - segment['p_vaddr']
        return raw[offset:offset+end-start]

    functions, literals, widgets = [], {}, set()
    for name in HELPERS + (profile.SAVE,):
        original = next(f for f in report['selected_functions'] if name in f['symbols'])
        start, end = original['address'], original['address']+original['size']
        spans = [(name, start, end)]
        if name == profile.SAVE:
            instructions = list(Cs(CS_ARCH_X86, CS_MODE_64).disasm(read(start, end), start))
            prefix = [i for i in instructions if SAVE_START <= i.address < SAVE_END]
            spans = [(SAVE_RATIO, SAVE_START, SAVE_END)]
            # Include original out-of-line QString cleanup paths reached by
            # this prefix, each ending in an unconditional jump back to it.
            targets = {int(i.op_str, 16) for i in prefix if i.mnemonic.startswith('j') and
                       i.op_str.startswith('0x') and int(i.op_str, 16) > SAVE_END}
            for target in sorted(targets):
                block = [i for i in instructions if i.address >= target]
                jump = next(i for i in block if i.mnemonic == 'jmp')
                if not SAVE_START <= int(jump.op_str, 16) <= SAVE_END:
                    raise ValueError('Save cleanup leaves selected prefix: '+hex(target))
                spans.append(('nvl-save-ratio-cleanup-'+hex(target), target, jump.address+jump.size))
            for instruction in prefix:
                match = re.fullmatch(r'rsi, qword ptr \[rax(?: \+ (0x[0-9a-f]+|[0-9]+))?\]', instruction.op_str)
                if instruction.mnemonic == 'mov' and match:
                    widgets.add(int(match[1] or '0', 0))
        for symbol, first, last in spans:
            code = read(first, last)
            functions.append(dict(symbol=symbol, address=first, size=len(code), code_hex=code.hex(),
                                  code_sha256=hashlib.sha256(code).hexdigest(),
                                  calls=[c for c in original['calls'] if first <= c['instruction'] < last]))
            for row in original['literals']:
                if first <= row['instruction'] < last:
                    literals[row['address']] = row
    if len(widgets) != 64:
        raise ValueError('Save ratio widget count differs')
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, core_fixture_sha256=cores.FIXTURE_SHA,
                scope='Original Load entry through 0x618ee6; full ratio Set/Get and vector growth helpers; '
                      'original Save entry through 0x6168b4 plus QString cleanup branches. '
                      'Topology/config objects, Qt parsing/destruction, streams and libc are synthetic. '
                      'Stop before clocks and other fields; no complete Save, hardware or real Qt execution.',
                functions=functions, literals=list(literals.values()), save_widget_offsets=sorted(widgets))


def load_fixture(path=FIXTURE):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('NVL ratio fixture hash mismatch')
    fixture = json.loads(raw)
    if (fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA or
            fixture['core_fixture_sha256'] != cores.FIXTURE_SHA or
            tuple(f['symbol'] for f in fixture['functions'][:4]) != HELPERS + (SAVE_RATIO,)):
        raise ValueError('NVL ratio fixture dependencies differ')
    return fixture


def command(inputs, index, write):
    """Reference bit operations only; flags/shift have no assigned hardware meaning."""
    selector = inputs.get('selector', 0)
    value = ((inputs.get('flag_b10', 0) ^ 1) & 255) if selector == 0 else (
        inputs.get('flag_b11', 2) & 255 if selector == 1 else selector & 255)
    domain = (value << (inputs.get('shift_e18', 0) & 31) << 13) & 0xffe000
    high = domain | (index << 8) | (inputs.get('kind', 0) << 26)
    if write:
        return ((high | (int(index == 7) << 28)) & 0x1cffff00) | 0x8000002b
    return (high & 0x0cffff00) | 0x8000002a


def groups():
    return [dict(first=[0x30+16*g+i for i in range(8)], second=[0x80+16*g+i for i in range(8)]) for g in range(4)]


class RatioMachine(cores.CoreMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(cores.load_fixture())
        combined['functions'].extend(fixture['functions'])
        combined['literals'].extend(fixture['literals'])
        super().__init__(combined, inputs)
        self.ratio_fixture = fixture
        self.ratio_calls, self.ratio_accesses, self.qt_parses = [], [], []
        self.vector_ptrs, self.widget_ptrs = [], {}
        self.slot, self.slot_reads, self.group, self.last_widget = 0, 0, 0, None
        data = bytearray(self.source_payload)
        source_groups = inputs.get('groups', groups())
        if len(source_groups) != 4:
            raise ValueError('synthetic profile needs four groups')
        for index, group in enumerate(source_groups):
            if len(group['first']) != 8 or len(group['second']) != 8:
                raise ValueError('synthetic profile needs four pairs of eight bytes')
            data[RATIO_START+16*index:RATIO_START+16*(index+1)] = bytes(group['first']+group['second'])
        self.source_payload = bytes(data)

    def flat_vector(self, values):
        obj, data = self.alloc(24), self.alloc(4*len(values))
        for index, value in enumerate(values):
            self.put32(data+4*index, value)
        self.uc.mem_write(obj, struct.pack('<3Q', data, data+4*len(values), data+4*len(values)))
        return obj

    def vector_values(self, ptr):
        start, end, capacity = (self.u64(ptr+offset) for offset in (0, 8, 16))
        if not start <= end <= capacity or (end-start) % 4 or end-start > 1024:
            raise ValueError('unexpected synthetic vector descriptor')
        return [cores.signed(self.u32(address)) for address in range(start, end, 4)]

    def prepare(self, symbol):
        super().prepare(profile.LOAD)
        self.entry = symbol
        control = self.reg('rdi')
        config = self.u64(self.objects['GLOBAL_NVL_MEM_CFG'])
        self.put64(control+0x38, config)
        self.uc.mem_write(config+0xb10, bytes([self.inputs.get('flag_b10', 0), self.inputs.get('flag_b11', 2)]))
        self.put64(config+0xe18, self.inputs.get('shift_e18', 0))
        if symbol in (SET, GET):
            self.phase = 'ratio'
            self.vector_ptrs = [self.flat_vector(self.inputs.get(key, default)) for key, default in
                                (('first', list(range(0x30, 0x38))), ('second', list(range(1, 9))))]
            self.reg('rdi', config)
            self.reg('rsi', self.inputs.get('kind', 0))
            self.reg('rdx', self.inputs.get('selector', 0))
            self.reg('rcx', self.vector_ptrs[0])
            self.reg('r8', self.vector_ptrs[1])
        elif symbol == SAVE_RATIO:
            self.phase = 'save-prefix'
            panel, ui = self.alloc(0x40), self.alloc(0x800)
            self.put64(control+0x30, panel)
            self.put64(panel+0x30, ui)
            for offset in self.ratio_fixture['save_widget_offsets']:
                ptr = self.alloc(16)
                self.put64(ui+offset, ptr)
                self.widget_ptrs[ptr] = offset
        elif symbol != profile.LOAD:
            raise ValueError('unsupported ratio experiment entry')

    def hook(self, uc, pc, size, unused):
        if self.entry == profile.LOAD and pc == cores.LOAD_END:
            self.phase = 'ratio'
            base.Machine.hook(self, uc, pc, size, unused)
            return
        if ((self.entry == profile.LOAD and pc == LOAD_END) or
                (self.entry == SAVE_RATIO and pc == SAVE_END)):
            self.stop('ratio-load-complete' if self.entry == profile.LOAD else 'ratio-save-prefix-complete')
            return
        if pc in (self.entries[SET], self.entries[GET]):
            self.group += 1
            self.ratio_calls.append(dict(operation='set' if pc == self.entries[SET] else 'get',
                                         kind=self.reg('rsi'), selector=cores.signed(self.reg('rdx')),
                                         first=self.vector_values(self.reg('rcx')), second=self.vector_values(self.reg('r8'))))
        super().hook(uc, pc, size, unused)
        if self.phase == 'ratio' and pc == self.entries['_Z5Wrmsrjjj']:
            self.stage = 'ratio-set' if self.last_high & 255 == 0x2b else 'ratio-get'
            self.slot, self.slot_reads = (self.last_high >> 8) & 7, 0

    def on_data_read(self, uc, access, address, size, value, unused):
        super().on_data_read(uc, access, address, size, value, unused)
        if self.entry == profile.LOAD and self.phase == 'ratio' and self.profile_buffer+RATIO_START <= address < self.profile_buffer+RATIO_END:
            self.ratio_accesses.append(dict(instruction=self.reg('rip'), offset=address-self.profile_buffer,
                                           size=size, value=int.from_bytes(uc.mem_read(address, size), 'little')))

    def on_data_write(self, uc, access, address, size, value, unused):
        super().on_data_write(uc, access, address, size, value, unused)
        if self.entry == SAVE_RATIO and self.profile_buffer+RATIO_START <= address < self.profile_buffer+RATIO_END:
            self.ratio_accesses.append(dict(instruction=self.reg('rip'), offset=address-self.profile_buffer,
                                           size=size, value=value, widget_offset=self.last_widget))

    def external(self, name):
        if name == 'memmove@plt':
            # Original vector growth runs; only bounded libc memory movement is synthetic.
            size = self.reg('rdx')
            if size > 1024:
                raise ValueError('unexpected ratio vector copy size')
            self.uc.mem_write(self.reg('rdi'), bytes(self.uc.mem_read(self.reg('rsi'), size)))
            self.ret(self.reg('rdi'))
            return
        if self.entry == SAVE_RATIO and name == '_ZNK9QLineEdit4textEv':
            self.last_widget = self.widget_ptrs[self.reg('rsi')]
            ptr = self.descriptor('synthetic')
            self.put32(ptr, self.inputs.get('qt_refcount', -1))
            self.put64(self.reg('rdi'), ptr)
            self.ret(self.reg('rdi'))
            return
        if self.entry == SAVE_RATIO and name == '_ZNK7QString6toUIntEPbi':
            if self.reg('rsi') != 0 or self.reg('rdx') != 10:
                raise ValueError('unexpected ratio QString conversion arguments')
            values = self.inputs.get('save_values', list(range(64)))
            value = values[len(self.qt_parses) % len(values)] & 0xffffffff
            self.qt_parses.append(dict(widget_offset=self.last_widget, ok_pointer=0, base=10, returned_uint=value,
                                       msr_writes_before=len(writes(self))))
            self.ret(value)
            return
        if self.phase != 'ratio' or name not in ('open@plt', 'lseek@plt', 'read@plt', 'write@plt', 'close@plt'):
            return super().external(name)
        if name == 'read@plt':
            self.slot_reads += 1
        previous_mode, previous_reply = self.inputs.get('io_mode', 'success'), self.inputs.get('reply_low', 0x1234)
        matches = (self.inputs.get('fault_slot', self.slot) == self.slot and
                   self.inputs.get('fault_group', self.group) == self.group and
                   (self.inputs.get('fault_read') is None or
                    (name == 'read@plt' and self.slot_reads == self.inputs['fault_read'])))
        self.inputs['io_mode'] = previous_mode if matches else 'success'
        replies = self.inputs.get('poll_replies' if self.slot_reads == 1 else 'final_replies',
                                 [0xdead0000 | ((0x70+i) << 8) | (0x30+i) for i in range(8)])
        self.inputs['reply_low'] = replies[self.slot] & 0xffffffff
        try:
            super().external(name)
            self.io[-1].update(ratio_slot=self.slot, ratio_group=self.group)
        finally:
            self.inputs['io_mode'], self.inputs['reply_low'] = previous_mode, previous_reply


def writes(machine):
    return [r for r in machine.io if r['op'] == 'write']


def observe(machine, result, label, inputs, entry):
    row = cores.observe(machine, result, label, inputs, entry)
    row.update(ratio_calls=machine.ratio_calls, ratio_accesses=machine.ratio_accesses,
               qt_parses=machine.qt_parses, vectors=[machine.vector_values(p) for p in machine.vector_ptrs])
    return row


def investigate(fixture):
    rows = []

    def case(label, inputs, entry=SET, outcome='returned', budget=400000):
        m = RatioMachine(fixture, inputs)
        result = m.run(entry, instruction_limit=budget)
        assert result['outcome'] == outcome, (label, inputs, result)
        rows.append(observe(m, result, label, inputs, entry))
        return m

    for entry, kind, selector, shift in itertools.product((SET, GET), (0, 1), (-1, 0, 1, 2, 255, 256), (0, 1, 18, 19, 31, 32)):
        inputs = dict(kind=kind, selector=selector, shift_e18=shift)
        m = case('command-routing-bits', inputs, entry)
        assert [r['high'] for r in writes(m)] == [command(inputs, i, entry == SET) for i in range(8)]
        assert all(r['path'] == '/dev/cpu/0/msr' and r['requested_msr'] == 0x150 for r in writes(m))
        if entry == GET:
            assert [m.vector_values(p) for p in m.vector_ptrs] == [list(range(0x30, 0x38)), list(range(0x70, 0x78))]
    for first_flag, second_flag, selector in itertools.product((0, 1, 255), (0, 2, 255), (0, 1)):
        inputs = dict(flag_b10=first_flag, flag_b11=second_flag, selector=selector)
        m = case('raw-config-byte', inputs)
        assert [r['high'] for r in writes(m)] == [command(inputs, i, True) for i in range(8)]
    for length in (0, 1, 7, 16):
        m = case('query-replaces-old-vectors', dict(first=[999]*length, second=[888]*length), GET)
        assert [m.vector_values(p) for p in m.vector_ptrs] == [list(range(0x30, 0x38)), list(range(0x70, 0x78))]
    for value in (-2147483648, -1, 0, 255, 256, 257, 2147483647):
        m = case('int-to-two-bytes', dict(first=[value]*9, second=[value]*9))
        assert [r['low'] for r in writes(m)] == [(value & 255)*0x101]*8
    for key, length in itertools.product(('first', 'second'), (0, 1, 7)):
        m = case('late-vector-range-error', {key: [3]*length}, outcome='range-error-boundary')
        assert len(writes(m)) == length and m.range_errors[0]['index'] == length
    for entry, mode in itertools.product((SET, GET), ('open-error', 'read-error', 'read-eof', 'short-read-4', 'short-read-7', 'busy-forever')):
        m = case('poll-stall', dict(io_mode=mode, fault_slot=3), entry, 'instruction-limit', 8000)
        assert len(writes(m)) == 4
        if entry == GET:
            assert [len(m.vector_values(p)) for p in m.vector_ptrs] == [3, 3]
    for entry, mode in itertools.product((SET, GET), ('write-error', 'short-write', 'seek-error', 'close-error')):
        m = case('ignored-io-error', dict(io_mode=mode), entry)
        assert len(writes(m)) == 8
    for entry, mode in itertools.product((SET, GET), ('read-error', 'read-eof', 'short-read-4', 'short-read-7', 'open-error')):
        inputs = dict(io_mode=mode, fault_read=2, poll_replies=[0x1122]*8, final_replies=[0x3344]*8)
        m = case('unchecked-second-read', inputs, entry)
        if entry == GET:
            word = 0x3344 if mode.startswith('short-read') else 0x1122
            assert [m.vector_values(p) for p in m.vector_ptrs] == [[word & 255]*8, [word >> 8]*8]
    for entry in (SET, GET):
        case('nonbusy-status-accepted', dict(reply_high=0x7fffffff), entry)
    for count in (0, 1, 2):
        m = case('profile-four-groups', dict(core_count=count), profile.LOAD, 'ratio-load-complete')
        assert len(writes(m)) == 4+3*count+32
        assert sorted(r['offset'] for r in m.ratio_accesses) == list(range(RATIO_START, RATIO_END))
        assert [(r['kind'], r['selector']) for r in m.ratio_calls] == [(0, 0), (0, 1), (1, 0), (1, 1)]
        for group, call in enumerate(m.ratio_calls):
            expected = groups()[group]
            assert call['first'] == expected['first'] and call['second'] == expected['second']
        assert [r['low'] for r in writes(m)[-32:]] == [g['first'][i] | (g['second'][i] << 8) for g in groups() for i in range(8)]
    for flags in (dict(flag_b10=1, flag_b11=0), dict(shift_e18=1)):
        inputs = dict(core_count=0, **flags)
        m = case('same-profile-current-config', inputs, profile.LOAD, 'ratio-load-complete')
        expected = [command(dict(inputs, kind=kind, selector=selector), i, True)
                    for kind, selector in ((0,0),(0,1),(1,0),(1,1)) for i in range(8)]
        assert [r['high'] for r in writes(m)[-32:]] == expected
    for delivered in (0, RATIO_START, RATIO_START+1, RATIO_START+9, RATIO_END-1):
        m = case('partial-file-read', dict(core_count=0, actual_read=delivered, initial_buffer_byte=0),
                 profile.LOAD, 'ratio-load-complete')
        expected = m.source_payload[:delivered] + bytes(profile.PROFILE_SIZE-delivered)
        assert [r['low'] for r in writes(m)[-32:]] == [expected[RATIO_START+16*g+i] | (expected[RATIO_START+16*g+8+i] << 8) for g in range(4) for i in range(8)]
    for group, slot in ((1, 0), (2, 3), (4, 7)):
        m = case('late-profile-stall', dict(core_count=0, io_mode='read-error', fault_stage='ratio-set',
                                          fault_group=group, fault_slot=slot), profile.LOAD, 'instruction-limit', 20000)
        assert len(writes(m)) == 4+8*(group-1)+slot+1
    for value in (0, 255, 256, 257, 65535, 0xffffffff):
        m = case('save-uint-truncation', dict(save_values=[value]), SAVE_RATIO, 'ratio-save-prefix-complete')
        assert bytes(m.uc.mem_read(m.profile_buffer+RATIO_START, 64)) == bytes([value & 255])*64
        assert len(writes(m)) == 2 and len(m.qt_parses) == 64
    for refcount in (-1, 1):
        inputs = dict(qt_refcount=refcount, save_values=[i*257 for i in range(64)], initial_buffer_byte=0)
        saved = case('save-original-entry', inputs, SAVE_RATIO, 'ratio-save-prefix-complete')
        assert sorted(r['offset'] for r in saved.ratio_accesses) == list(range(RATIO_START, RATIO_END))
        loaded = RatioMachine(fixture, dict(core_count=0))
        loaded.source_payload = bytes(saved.uc.mem_read(saved.profile_buffer, profile.PROFILE_SIZE))
        result = loaded.run(profile.LOAD)
        assert result['outcome'] == 'ratio-load-complete'
        assert [r['low'] for r in writes(loaded)[-32:]] == [(16*g+i) | ((16*g+8+i) << 8) for g in range(4) for i in range(8)]
        rows.append(observe(loaded, result, 'save-to-load-ratio-roundtrip', dict(core_count=0, qt_refcount=refcount), profile.LOAD))
    for mode in ('write-error', 'short-write', 'seek-error', 'close-error'):
        m = case('save-prefix-ignored-error', dict(io_mode=mode), SAVE_RATIO, 'ratio-save-prefix-complete')
        assert len(m.qt_parses) == 64
    for mode in ('read-error', 'busy-forever'):
        m = case('save-stalls-before-fields', dict(io_mode=mode), SAVE_RATIO, 'instruction-limit', 4000)
        assert not m.qt_parses and len(writes(m)) == 1
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                core_fixture_sha256=cores.FIXTURE_SHA, scope=fixture['scope'],
                fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                environment=dict(system=platform.system(), python=platform.python_version(), unicorn=unicorn.__version__),
                cases_characterized=len(rows), outcomes=dict(Counter(r['outcome'] for r in rows)), observations=rows)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--export-fixture', type=Path)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.binary:
        if not args.export_fixture:
            parser.error('--binary requires --export-fixture')
        fixture = extract(args.binary)
        raw = profile.encoded(fixture)
        args.export_fixture.write_bytes(raw)
        print('fixture SHA '+hashlib.sha256(raw).hexdigest(), flush=True)
    else:
        fixture = load_fixture(args.fixture)
    if args.output:
        report = investigate(fixture)
        args.output.write_bytes(profile.encoded(report))
        print(str(report['cases_characterized'])+' original ratio cases; synthetic I/O only')
    elif not args.binary:
        parser.error('provide --output')


if __name__ == '__main__':
    main()
