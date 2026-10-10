#!/usr/bin/env python3
"""Original NVL per-core import and a bounded exporter slice; synthetic I/O only."""
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
from capstone.x86 import X86_OP_MEM
from elftools.elf.elffile import ELFFile
import unicorn

import importlib.util
spec = importlib.util.spec_from_file_location('nvl_prefix', Path(__file__).with_name('legacy-nvl-prefix.py'))
prefix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prefix)
profile, msr, base = prefix.profile, prefix.msr, prefix.base
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-nvl-cores.json'
FIXTURE_SHA = 'be1bb6aeba8051be1a746c3fadbefa89f3f0a83f8be78b9d731d778b4d08726e'
MASK = '_ZN18My_Processor_Cores18get_Processor_MaskEi'
TX_WRITE, TX_READ = '_Z7WrmsrTxjjjm', '_Z7RdmsrTxjPjS_m'
VF = '_Z29Wr_VFPoint_offset_percore_nvliii'
HELPERS = (MASK, TX_WRITE, TX_READ, VF)
SAVE_CORE = 'nvl-save-core-slice'
SLICES = ((SAVE_CORE, 0x6169ea, 0x616dd6),
          ('nvl-save-empty-core-slice', 0x617e32, 0x617e3e),
          ('nvl-save-range-error-slice', 0x617e60, 0x617e92))
LOAD_END, SAVE_END = 0x618aa5, 0x616dd6
READ_SITES = {0x6187f8: 'byte', 0x6188c3: 'word_211', 0x618983: 'word_27',
              0x618a4f: 'vf_value', 0x618a69: 'vf_count'}
WRITE_SITES = {0x6169f7: 'saved_core_count', 0x616a56: 'byte', 0x616b53: 'word_10',
               0x616c23: 'word_210', 0x616ce9: 'word_26', 0x616d28: 'vf_count',
               0x616d53: 'vf_value'}


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    audit = profile.module('core_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(' + '|'.join(map(re.escape, HELPERS + (profile.SAVE,))) + ')$'), True)
    elf = ELFFile(io.BytesIO(data))

    def function(symbol, original, start, end):
        segment = next(s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and
                       s['p_vaddr'] <= start and end <= s['p_vaddr'] + s['p_filesz'])
        offset = segment['p_offset'] + start - segment['p_vaddr']
        code = data[offset:offset + end-start]
        return dict(symbol=symbol, address=start, size=len(code), code_hex=code.hex(),
                    code_sha256=hashlib.sha256(code).hexdigest(),
                    calls=[c for c in original['calls'] if start <= c['instruction'] < end])

    functions, literals = [], {}
    for name in HELPERS + (profile.SAVE,):
        original = next(f for f in report['selected_functions'] if name in f['symbols'])
        spans = SLICES if name == profile.SAVE else ((name, original['address'], original['address'] + original['size']),)
        for symbol, start, end in spans:
            functions.append(function(symbol, original, start, end))
            for row in original['literals']:
                if start <= row['instruction'] < end:
                    literals[row['address']] = row
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA,
                prefix_fixture_sha256=prefix.FIXTURE_SHA, profile_fixture_sha256=profile.FIXTURE_SHA,
                msr_fixture_sha256=msr.FIXTURE_SHA,
                scope='Original Load Profile entry through per-core loop; original Tx leaves, CPU-index getter '
                      'and VF write helper. Original exporter core slice uses synthetic Rd_PC_Ratio and VF '
                      'enumeration results, with an explicitly prepared frame. Stop before later fields. '
                      'All Qt/streams/libc and topology data are synthetic; no host hardware or full Save execution.',
                functions=functions, literals=list(literals.values()))


def load_fixture(path=FIXTURE):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('NVL core fixture hash mismatch')
    fixture = json.loads(raw)
    if (fixture['schema'] != 1 or fixture['source_elf_sha256'] != base.SOURCE_SHA or
            fixture['prefix_fixture_sha256'] != prefix.FIXTURE_SHA or
            fixture['profile_fixture_sha256'] != profile.FIXTURE_SHA or
            fixture['msr_fixture_sha256'] != msr.FIXTURE_SHA or
            tuple(f['symbol'] for f in fixture['functions']) != HELPERS + tuple(s[0] for s in SLICES)):
        raise ValueError('NVL core fixture dependencies differ')
    return fixture


def signed(value, bits=32):
    value &= (1 << bits)-1
    return value - (1 << bits) if value & (1 << (bits-1)) else value


def vf_word(offset):
    """Bit-exact reference for the original signed multiply/divide; no unit claim."""
    product = signed(offset * 1024000)
    quotient = (abs(product) // 1000000) * (-1 if product < 0 else 1)
    negative = product < -999999
    # The negative branch masks BEFORE the shared signed `test/setg/sub`.
    # A negative quotient can therefore become positive and be decremented.
    if negative:
        quotient &= 0x3ff
    return (0x80000000 if negative else 0) | (((quotient - int(quotient > 0)) & 0x3ff) << 21)


def payload(inputs):
    data = bytearray(profile.PROFILE_SIZE)
    # Controlled values occupy the 128 rows implied by adjacent array offsets.
    # This is a test layout, not validation implemented by the original loader.
    for core in range(128):
        data[0x26+core] = (0x30+core) & 255
        for offset, tag in ((0xa6, 0x10000000), (0x2a6, 0x20000000), (0x4a6, 0x40000000)):
            struct.pack_into('<I', data, offset+4*core, tag+core)
        for point in range(15):
            struct.pack_into('<h', data, 0x6a6+30*core+2*point, core*32+point-7)
    for core, count in enumerate(inputs.get('vf_counts', [])):
        data[0x15a6+core] = count
    struct.pack_into('<H', data, 0x168c, inputs.get('saved_core_count', 1))
    for row in inputs.get('patches', []):
        offset, value = row['offset'], bytes.fromhex(row['hex'])
        if offset < 0 or offset+len(value) > len(data):
            raise ValueError('synthetic payload patch outside profile')
        data[offset:offset+len(value)] = value
    return bytes(data)


class CoreMachine(prefix.PrefixMachine):
    def __init__(self, fixture, inputs):
        combined = copy.deepcopy(prefix.load_fixture())
        combined['functions'].extend(fixture['functions'])
        super().__init__(combined, copy.deepcopy(inputs))
        for row in fixture['literals']:
            raw = row['text'].encode('utf-8')+b'\0'
            self.map(row['address'], len(raw))
            self.uc.mem_write(row['address'], raw)
        self.source_payload = payload(inputs)
        self.accesses, self.vf_calls, self.range_errors = [], [], []
        self.phase, self.stage, self.wrapper = 'prefix', 'prefix', None
        self.device_path, self.last_high, self.save_core = None, 0, 0
        self.field_operands = {}
        decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        decoder.detail = True
        for pc in set(READ_SITES) | set(WRITE_SITES):
            instruction = next(decoder.disasm(bytes(self.uc.mem_read(pc, 15)), pc, 1))
            operand = next(op for op in instruction.operands if op.type == X86_OP_MEM)
            self.field_operands[pc] = (instruction.reg_name(operand.mem.base),
                                       instruction.reg_name(operand.mem.index),
                                       operand.mem.scale, operand.mem.disp, operand.size)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_READ, self.on_data_read)
        self.uc.hook_add(unicorn.UC_HOOK_MEM_WRITE, self.on_data_write)

    def record_access(self, kind, pc, address, size, value):
        offset = address-self.profile_buffer
        if not 0 <= offset <= profile.PROFILE_SIZE-size:
            self.events.append(dict(kind=kind, instruction=pc, offset=offset, size=size))
            self.stop('profile-access-boundary')
            return
        row = dict(kind=kind, instruction=pc, offset=offset, size=size, value=value)
        if kind == 'vf_value':
            if self.entry == profile.LOAD:
                point = self.u32(self.frame-0x20a4)
                core = self.u64(self.frame-0x20b8)
            else:
                point = self.reg('rdx')+1
                core = self.save_core
            row.update(core=core, point=point, beyond_row=point > 15)
        self.accesses.append(row)

    def on_data_read(self, uc, access, address, size, value, unused):
        pc = self.reg('rip')
        if self.entry == profile.LOAD and pc in READ_SITES:
            self.record_access(READ_SITES[pc], pc, address, size, int.from_bytes(uc.mem_read(address, size), 'little'))

    def on_data_write(self, uc, access, address, size, value, unused):
        pc = self.reg('rip')
        if self.entry == SAVE_CORE and pc in WRITE_SITES:
            self.record_access(WRITE_SITES[pc], pc, address, size, value & ((1 << (8*size))-1))

    def u32(self, ptr):
        return struct.unpack('<I', self.uc.mem_read(ptr, 4))[0]

    def prepare(self, symbol):
        self.entry = symbol
        self.frame = base.STACK+0xfff0
        count = self.inputs.get('core_count', 1)
        ids = self.inputs.get('core_ids', list(range(min(max(count, 0), 129))))
        cpus = self.inputs.get('cpu_indices', [3+2*i for i in range(min(max(count, 0), 129))])
        control, mapping, config = self.alloc(0x200), self.alloc(0xa0), self.alloc(0xe28)
        self.put32(control+0x19c, count)
        self.put64(control+0xe8, mapping)
        for obj, start, end, values in ((mapping, 0x80, 0x88, cpus), (config, 0xdd8, 0xde0, ids)):
            values_ptr = self.alloc(4*len(values))
            for index, value in enumerate(values):
                self.put32(values_ptr+4*index, value)
            self.put64(obj+start, values_ptr)
            self.put64(obj+end, values_ptr+4*len(values))
        self.put64(self.objects['GLOBAL_NVL_MEM_CFG'], config)
        if symbol == profile.LOAD:
            self.reg('rdi', control)
        elif symbol == SAVE_CORE:
            self.phase = 'save'
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_RBP, self.frame)
            self.reg('rsp', self.frame-0x2000)
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_R14, self.frame-0x1e20)
            self.put64(self.frame-0x1ec8, control)
        elif symbol == VF:
            self.phase = 'vf'
            self.reg('rdi', self.inputs.get('point', 1))
            self.reg('rsi', self.inputs.get('offset', 0))
            self.reg('rdx', self.inputs.get('core', 0))
        else:
            raise ValueError('unsupported core experiment entry')

    def hook(self, uc, pc, size, unused):
        if (self.entry == profile.LOAD and pc in READ_SITES) or (self.entry == SAVE_CORE and pc in WRITE_SITES):
            base_reg, index_reg, scale, displacement, width = self.field_operands[pc]
            def register(name):
                return uc.reg_read(getattr(unicorn.x86_const, 'UC_X86_REG_'+name.upper())) if name else 0
            offset = register(base_reg)+register(index_reg)*scale+displacement-self.profile_buffer
            if not 0 <= offset <= profile.PROFILE_SIZE-width:
                self.events.append(dict(instruction=pc, offset=offset, size=width))
                self.stop('profile-access-boundary')
                return
        if self.entry == profile.LOAD and pc == prefix.END:
            self.phase = 'core'
        if (self.entry == profile.LOAD and pc == LOAD_END) or (self.entry == SAVE_CORE and pc == SAVE_END):
            self.stop('core-loop-complete')
            return
        if pc == 0x616a30:
            self.save_core = self.uc.reg_read(unicorn.x86_const.UC_X86_REG_R12)
        if pc == self.entries[VF]:
            self.vf_calls.append(dict(point=self.reg('rdi') & 0xffffffff,
                                      offset=signed(self.reg('rsi')), core=signed(self.reg('rdx'))))
        if pc in (self.entries[TX_WRITE], self.entries[TX_READ], self.entries['_Z5Wrmsrjjj'], self.entries['_Z5RdmsrjPjS_']):
            self.wrapper = 'tx' if pc in (self.entries[TX_WRITE], self.entries[TX_READ]) else 'cpu0'
            if pc in (self.entries[TX_WRITE], self.entries['_Z5Wrmsrjjj']):
                self.last_high = self.reg('rdx') & 0xffffffff
                self.stage = self.phase+'-'+hex(self.last_high & 0xfff)
        # Bypass PrefixMachine's earlier stop; keep the same instruction allowlist.
        base.Machine.hook(self, uc, pc, size, unused)

    def external(self, name):
        if name == profile.READ:
            super().external(name)
            if self.read_request and 'synthetic_bytes_delivered' in self.read_request:
                count = self.read_request['synthetic_bytes_delivered']
                if count:
                    self.uc.mem_write(self.profile_buffer, self.source_payload[:count])
            return
        if name == '_Z11Rd_PC_Ratioi':
            self.ret(0x130 + (self.reg('rdi') & 0xffffffff))
            return
        if name == '_Z22check_vfpt_percore_nvli':
            count = self.inputs.get('save_vf_count', 0)
            value = self.inputs.get('save_vf_value', -7)
            rows = [[i+1, (value+i) & 0xffffffff][:self.inputs.get('save_vf_width', 2)] for i in range(count)]
            self.vector(self.reg('rdi'), rows)
            self.ret(self.reg('rdi'))
            return
        if name == '_ZdlPvm@plt':
            self.ret()
            return
        if name.startswith('_ZSt24__throw_out_of_range_fmt'):
            self.range_errors.append(dict(instruction_return=self.u64(self.reg('rsp')),
                                          index=self.reg('rsi'), length=self.reg('rdx')))
            self.stop('range-error-boundary')
            return
        if name == '__sprintf_chk@plt':
            if self.cstring(self.reg('rcx')) != '/dev/cpu/%d/msr' or self.reg('rdx') != 64:
                raise ValueError('unexpected MSR path format')
            self.device_path = '/dev/cpu/' + str(signed(self.reg('r8'))) + '/msr'
            raw = self.device_path.encode('ascii')
            self.uc.mem_write(self.reg('rdi'), raw+b'\0')
            self.ret(len(raw))
            return
        if name not in ('open@plt', 'lseek@plt', 'read@plt', 'write@plt', 'close@plt'):
            return super().external(name)
        previous_mode = self.mode
        previous_reply = self.inputs.get('reply_low', 0x1234)
        active = self.phase != 'prefix' and (not self.inputs.get('fault_stage') or self.stage == self.inputs['fault_stage'])
        if self.inputs.get('fault_read_ordinal') is not None:
            active = active and name == 'read@plt' and self.reads+1 == self.inputs['fault_read_ordinal']
        self.mode = self.inputs.get('io_mode', 'success') if active else 'success'
        if self.entry == SAVE_CORE:
            replies = self.inputs.get('save_replies', [0x12345678, 0x87654321, 0x0abcdef0])
            self.inputs['reply_low'] = dict(zip((0x10, 0x210, 0x26), replies)).get(self.last_high & 0xfff, previous_reply)
        try:
            if name == 'open@plt':
                if self.cstring(self.reg('rdi')) != self.device_path or self.reg('rsi') not in (0, 1):
                    raise ValueError('unexpected synthetic device open')
                result = -1 if self.mode == 'open-error' else 700
                self.io.append(dict(op='open', path=self.device_path, flags=self.reg('rsi'), result=result))
                self.ret(result)
            else:
                prefix.PrefixMachine.external(self, name)
            self.io[-1].update(path=self.device_path, stage=self.stage, wrapper=self.wrapper)
        finally:
            self.mode = previous_mode
            self.inputs['reply_low'] = previous_reply

    def run(self, symbol, instruction_limit=400000, allow_instruction_bound=True):
        self.reg('rsp', base.STACK+0xfff8)
        self.put64(self.reg('rsp'), base.STOP)
        self.output = self.alloc(256)
        self.prepare(symbol)
        self.uc.emu_start(self.entries[symbol], base.STOP+1, timeout=15000000, count=instruction_limit)
        if self.outcome is None:
            if not allow_instruction_bound or self.steps != instruction_limit:
                raise ValueError('unexpected emulation timeout')
            self.outcome = 'instruction-limit'
        return dict(outcome=self.outcome, steps=self.steps)


def observe(machine, result, label, inputs, entry):
    reads = [r for r in machine.io if r['op'] == 'read']
    return dict(label=label, entry=entry, inputs=inputs, **result,
                writes=[r for r in machine.io if r['op'] == 'write'],
                read_results=dict(Counter(str(r['result']) for r in reads)),
                read_samples=reads[:2]+reads[-2:] if len(reads)>4 else reads,
                io_count=len(machine.io), io_sha256=hashlib.sha256(profile.encoded(machine.io)).hexdigest(),
                profile_accesses=machine.accesses, vf_calls=machine.vf_calls, range_errors=machine.range_errors,
                file_read=machine.read_request, boundaries=machine.events,
                input_payload_sha256=hashlib.sha256(machine.source_payload).hexdigest() if entry == profile.LOAD else None,
                profile_sha256=hashlib.sha256(machine.uc.mem_read(machine.profile_buffer, profile.PROFILE_SIZE)).hexdigest())


def investigate(fixture):
    rows = []

    def case(label, inputs, expected='core-loop-complete', entry=profile.LOAD, budget=400000):
        machine = CoreMachine(fixture, inputs)
        result = machine.run(entry, instruction_limit=budget)
        if result['outcome'] != expected:
            raise AssertionError((label, inputs, expected, result))
        if entry == profile.LOAD:
            assert all(r['instruction'] in READ_SITES for r in machine.profile_reads), 'unclassified profile read'
        rows.append(observe(machine, result, label, inputs, entry))
        return machine

    for count in (-1, 0, 1, 2, 128, 129):
        m = case('current-core-count', dict(core_count=count, saved_core_count=0))
        assert len([r for r in m.io if r['op']=='write']) == 4+3*max(count, 0)
        if count == 129:
            assert [r['offset'] for r in m.accesses[-4:]] == [0xa6,0x6a6,0x4a6,0x1626]
    for count in (0, 1, 15, 16, 17, 31, 255):
        m = case('file-vf-count', dict(vf_counts=[count]))
        assert len(m.vf_calls) == count
        assert [r['point'] for r in m.vf_calls] == list(range(1, count+1))
        assert sum(r.get('beyond_row', False) for r in m.accesses) == max(count-15, 0)
        programmed = [r for r in m.io if r['op']=='write' and r['stage']=='core-0x33']
        assert [(r['low'],r['high']) for r in programmed] == [
            (vf_word(call['offset']), 0x84000033 | ((call['point'] & 15)<<27)) for call in m.vf_calls]
    m = case('two-topology-maps', dict(core_count=2, core_ids=[0x2001,0x123], cpu_indices=[7,11], vf_counts=[1,1]))
    tx = [r for r in m.io if r['op']=='write' and r['wrapper']=='tx']
    assert [r['path'] for r in tx] == ['/dev/cpu/7/msr']*3+['/dev/cpu/11/msr']*3
    assert [r['high'] for r in tx] == [cmd | (core << 13) for core in (1,0x123) for cmd in (0x80000011,0x80000211,0x80000027)]
    assert all(r['path']=='/dev/cpu/0/msr' for r in m.io if r['op']=='write' and r['wrapper']=='cpu0')
    for cpu in (0x80000000, 0xffffffff):
        m = case('signed-cpu-path', dict(cpu_indices=[cpu]))
        assert any(r.get('path')=='/dev/cpu/'+str(signed(cpu))+'/msr' for r in m.io)
    for inputs in (dict(core_ids=[]), dict(cpu_indices=[]), dict(core_count=2,core_ids=[7]), dict(core_count=2,cpu_indices=[3])):
        case('topology-range-error', inputs, 'range-error-boundary')
    for delivered in (0, 0x26, 0x27, 0x4a8, profile.PROFILE_SIZE-1):
        m = case('short-read-still-applies', dict(actual_read=delivered,initial_buffer_byte=0))
        assert len([r for r in m.io if r['op']=='write']) == 7
    for mode in ('write-error','short-write','seek-error','close-error'):
        m = case('ignored-core-io-failure',dict(io_mode=mode))
        assert len([r for r in m.io if r['op']=='write']) == 7
    for command, mode in itertools.product(('core-0x11','core-0x211','core-0x27'),
                                           ('busy-forever','read-error','read-eof','short-read-4','short-read-7','open-error')):
        case('core-poll-failure',dict(io_mode=mode,fault_stage=command),'instruction-limit',budget=5000)
    for offset, point in itertools.product((-32768,-4195,-2098,-2097,-1000,-977,-1,0,1,977,1000,2097,2098,4195,32767), (1,15,16,255)):
        m = case('vf-integer-encoding',dict(offset=offset,point=point,core_ids=[0x2001]),'returned',VF)
        writes = [r for r in m.io if r['op']=='write']
        assert len(writes)==3 and writes[-1]['low']==vf_word(offset)
        assert writes[-1]['high']==(0x84000033 | (1<<13) | ((point&15)<<27))
    for command, mode in itertools.product(('vf-0x14','vf-0x15','vf-0x33'), ('busy-forever','read-error')):
        case('vf-poll-failure',dict(io_mode=mode,fault_stage=command),'instruction-limit',VF,budget=5000)
    for mode in ('write-error','short-write','seek-error','close-error'):
        m = case('ignored-vf-io-failure',dict(io_mode=mode),'returned',VF)
        assert len([r for r in m.io if r['op']=='write']) == 3
    m = case('unchecked-final-vf-read',dict(io_mode='read-error',fault_stage='vf-0x33',fault_read_ordinal=5),'returned',VF)
    assert [r['result'] for r in m.io if r['op']=='read'] == [8,8,8,8,-1]
    for index in (-1,1):
        m = case('vf-late-topology-error',dict(core=index),'range-error-boundary',VF)
        assert len([r for r in m.io if r['op']=='write']) == 2
    for count in (-1,0,1,2,128,129):
        m = case('export-core-count',dict(core_count=count),entry=SAVE_CORE)
        assert m.u32(m.profile_buffer+0x168c)&0xffff == count&0xffff
    for count in (0,1,15,16,255,256):
        m = case('export-vf-row-width',dict(save_vf_count=count),entry=SAVE_CORE)
        assert m.uc.mem_read(m.profile_buffer+0x15a6,1)[0] == count&255
        assert len([r for r in m.accesses if r['kind']=='vf_value'])==count
    for value in (-32769,-32768,32767,32768,0x12345):
        m = case('export-vf-truncation',dict(save_vf_count=1,save_vf_value=value),entry=SAVE_CORE)
        assert int.from_bytes(m.uc.mem_read(m.profile_buffer+0x6a6,2),'little') == value&0xffff
    case('export-short-vf-row',dict(save_vf_count=1,save_vf_width=1),'range-error-boundary',SAVE_CORE)
    for inputs in (dict(core_ids=[]),dict(cpu_indices=[])):
        case('export-topology-range-error',inputs,'range-error-boundary',SAVE_CORE)
    for count in (1, 15, 16):
        inputs = dict(core_count=2,save_vf_count=count)
        saved = case('roundtrip-export',inputs,entry=SAVE_CORE)
        loaded = CoreMachine(fixture,dict(core_count=2))
        loaded.source_payload = bytes(saved.uc.mem_read(saved.profile_buffer,profile.PROFILE_SIZE))
        result = loaded.run(profile.LOAD)
        assert result['outcome']=='core-loop-complete'
        # The exporter writes -7..8 for each row. At 16 points the second
        # core overwrites the first core's final value with its own first.
        expected = [-7+i for i in range(count)]
        if count == 16:
            expected[-1] = -7
        assert [c['offset'] for c in loaded.vf_calls[:count]] == expected
        rows.append(observe(loaded,result,'roundtrip-import',
                            dict(core_count=2,source_profile_sha256=hashlib.sha256(loaded.source_payload).hexdigest()),profile.LOAD))
    return dict(schema=1, source_elf_sha256=base.SOURCE_SHA, fixture_sha256=FIXTURE_SHA,
                prefix_fixture_sha256=prefix.FIXTURE_SHA, scope=fixture['scope'],
                environment=dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
                cases_characterized=len(rows), outcomes=dict(Counter(r['outcome'] for r in rows)), observations=rows)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    if args.binary:
        if not args.export_fixture:
            parser.error('--binary requires --export-fixture')
        fixture = extract(args.binary)
        raw = profile.encoded(fixture)
        args.export_fixture.write_bytes(raw)
        print('fixture SHA '+hashlib.sha256(raw).hexdigest(),flush=True)
    else:
        fixture = load_fixture(args.fixture)
    if args.output:
        report = investigate(fixture)
        args.output.write_bytes(profile.encoded(report))
        print(str(report['cases_characterized'])+' original per-core cases; synthetic I/O only')
    elif not args.binary:
        parser.error('provide --output')


if __name__ == '__main__':
    main()
