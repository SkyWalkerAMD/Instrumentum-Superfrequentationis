#!/usr/bin/env python3
"""Trace the complete original NVL profile callers with explicit synthetic boundaries."""
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
from capstone.x86 import X86_OP_MEM, X86_REG_RIP, X86_REG_RDX
from elftools.elf.elffile import ELFFile
import unicorn
import importlib.util

spec = importlib.util.spec_from_file_location('nvl_ratios', Path(__file__).with_name('legacy-nvl-ratios.py'))
ratios = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ratios)
cores, profile, msr, base = ratios.cores, ratios.profile, ratios.msr, ratios.base
ngu = profile.module('apply_ngu', 'legacy-intel-ngu.py')
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/legacy-nvl-apply.json'
FIXTURE_SHA = 'c19d09291320ff3c4affb89a6d3a3692e0c6fc3909e3a6ecc02e0bf23ed2c1ba'
UFS_GET = '_ZN11NVL_MEM_CFG21get_ufs_min_max_ratioEbi'
UFS_SET = '_ZN11NVL_MEM_CFG21set_ufs_min_max_ratioEbiii'
BCLK = '_Z11Wr_ARL_Bclkdb'
VF = '_Z21Wr_VFPoint_offset_nvliii'
OCT_CTOR, OCT_DTOR = '_ZN9OCTVB_NVLC2Ev', '_ZN9OCTVB_NVLD2Ev'
OCT_GET_CORE, OCT_GET_TRL = '_ZN9OCTVB_NVL11get_percoreEv', '_ZN9OCTVB_NVL7get_trlEv'
OCT_SET_CORE, OCT_SET_TRL = '_ZN9OCTVB_NVL11set_percoreEv', '_ZN9OCTVB_NVL7set_trlEv'
CORE_QUERY, VF_QUERY, VF_CORE_QUERY = '_Z11Rd_PC_Ratioi', '_Z14check_vfpt_nvli', '_Z22check_vfpt_percore_nvli'
WR199 = '_Z5Wr199v'
CPU_CLK, PCIE_CLK = '_ZN11clkgen_95959apply_cpuEf', '_ZN11clkgen_959510apply_pcieEf'
HELPERS = (profile.SAVE, UFS_GET, UFS_SET, '_Z14Rd_Ratio_cachem', BCLK, VF,
           OCT_CTOR, OCT_DTOR, OCT_GET_CORE, OCT_GET_TRL, OCT_SET_CORE, OCT_SET_TRL,
           CORE_QUERY, VF_QUERY, VF_CORE_QUERY, WR199)
OCT_DATA = (0x78, 0x90, 0xa8, 0xc0, 0x1b0, 0x1c8, 0x1e0, 0x1f8,
            0xd8, 0xf0, 0x108, 0x120, 0x210, 0x228, 0x240, 0x258)
PHASES = {0x618ee6:'clock', 0x618f95:'domain', 0x61908c:'ngu', 0x619098:'domain',
          0x6190dc:'ufs', 0x61911f:'policy', 0x61926a:'checkbox', 0x619281:'vf-global',
          0x6192b7:'octvb', 0x619466:'wr199', 0x619473:'cleanup'}


def extract(path, helper_addresses=None):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != base.SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    elf = ELFFile(io.BytesIO(raw))
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    named = {s.name:s for s in symbols if s['st_size']}
    addressed = {s['st_value']:s for s in symbols if s['st_size']}

    def read(address, size):
        for segment in elf.iter_segments():
            offset = address-segment['p_vaddr']
            if segment['p_type'] == 'PT_LOAD' and 0 <= offset and offset+size <= segment['p_memsz']:
                if offset >= segment['p_filesz']:
                    return bytes(size)
                start = segment['p_offset']+offset
                return raw[start:start+size]
        raise ValueError('unmapped source data '+hex(address))

    # Include the original standard-vector helper closure used by Save, VF
    # enumeration and OCTVB. Exceptions remain explicit external boundaries.
    overrides = helper_addresses or {}
    selected = [(name,overrides.get(name,named[name]['st_value'])) for name in HELPERS]
    for name,address in selected:
        if not any(s.name==name and s['st_value']==address and s['st_size'] for s in symbols):
            raise ValueError('helper address does not match original symbol '+name)
    for name,address in selected:
        sym = addressed[address]
        for instruction in Cs(CS_ARCH_X86, CS_MODE_64).disasm(read(address,sym['st_size']),address):
            if instruction.mnemonic not in ('call','jmp') or not instruction.op_str.startswith('0x'):
                continue
            target = addressed.get(int(instruction.op_str,16))
            if target is not None and target.name.startswith(('_ZNSt6vector','_Z10invertBits')) and '.cold' not in target.name and target['st_value'] not in {a for _,a in selected}:
                selected.append((target.name,target['st_value']))
    audit = profile.module('apply_audit', 'elf-runtime-audit.py')
    report = audit.inspect(path, re.compile('^(?:'+'|'.join(re.escape(n) for n,_ in selected)+')$'), True)
    fixture = dict(schema=1, source_elf_sha256=base.SOURCE_SHA, ratio_fixture_sha256=ratios.FIXTURE_SHA,
                   ngu_fixture_sha256=ngu.FIXTURE_SHA, functions=[], objects={}, literals=[], data=[], relocations=[],
                   scope='Complete original Save/Load callers and original UFS, VF queries/writes, OCTVB construction/query/destruction, '
                         'Wr199 and ARL clock helpers. Original NGU/MMIO request chain reused. Qt/streams/libc/config are synthetic; '
                         '9595 clock calls and final refresh are explicitly recorded boundaries, not recovered bodies.')
    relocations = {}
    for section in elf.iter_sections():
        if section['sh_type'] in ('SHT_RELA','SHT_REL'):
            table = elf.get_section(section['sh_link'])
            for row in section.iter_relocations():
                if row['r_info_sym']:
                    relocations[row['r_offset']] = dict(type=row['r_info_type'],symbol=table.get_symbol(row['r_info_sym']).name)
    literals, seen = {}, set()
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.detail = True
    for name,address in selected:
        # Local .isra clones may share a mangled name: match call addresses.
        fn = next(f for f in report['selected_functions'] if f['address']==address)
        code = read(fn['address'],fn['size'])
        symbol = name if name in HELPERS else name+'@'+hex(address)
        fixture['functions'].append(dict(symbol=symbol,address=fn['address'],size=len(code),code_hex=code.hex(),
                                         code_sha256=hashlib.sha256(code).hexdigest(),calls=fn['calls']))
        for row in fn['literals']:
            literals[row['address']] = row
        for instruction in decoder.disasm(code,fn['address']):
            for operand in instruction.operands:
                if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                    continue
                address = instruction.address+instruction.size+operand.mem.disp
                if address in seen:
                    continue
                seen.add(address)
                symbol = addressed.get(address)
                if address in relocations:
                    fixture['relocations'].append(dict(address=address,**relocations[address]))
                elif symbol is not None and symbol['st_info']['type'] == 'STT_OBJECT':
                    fixture['objects'][symbol.name] = dict(address=address,size=symbol['st_size'],source_initial_hex=read(address,symbol['st_size']).hex())
                elif address not in literals:
                    fixture['data'].append(dict(address=address,bytes_hex=read(address,max(operand.size,16)).hex()))
    fixture['literals'] = list(literals.values())
    return fixture


def load_fixture(path=FIXTURE):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != FIXTURE_SHA:
        raise ValueError('NVL apply fixture hash mismatch')
    fixture = json.loads(raw)
    if (fixture['source_elf_sha256'] != base.SOURCE_SHA or fixture['ratio_fixture_sha256'] != ratios.FIXTURE_SHA or
            fixture['ngu_fixture_sha256'] != ngu.FIXTURE_SHA):
        raise ValueError('NVL apply dependency mismatch')
    return fixture


class ApplyMachine(ratios.RatioMachine):
    def __init__(self, fixture, inputs, extra_fixtures=()):
        combined = copy.deepcopy(ratios.load_fixture())
        additions = [fixture, ngu.combined(ngu.load_fixture())]+list(extra_fixtures)
        dispatched = base.load_fixture()
        additions.append(dict(functions=[f for f in dispatched['functions'] if f['symbol'] in
                                          ('_ZN10proc_classC2Ev','_Z8isit_adlv')],literals=dispatched['literals']))
        known = {f['address'] for f in combined['functions']} | {f['address'] for f in cores.load_fixture()['functions']}
        known |= {f['address'] for f in profile.load_fixture()['functions']} | {f['address'] for f in msr.load_fixture()['functions']}
        for extra in additions:
            for fn in extra['functions']:
                if fn['address'] not in known or fn['symbol'] == profile.SAVE:
                    combined['functions'].append(fn)
                    known.add(fn['address'])
            combined['literals'].extend(extra['literals'])
        super().__init__(combined, inputs)
        for extra in additions:
            for name, row in extra.get('objects',{}).items():
                if name not in self.objects:
                    self.objects[name] = row['address']
                    self.map(row['address'],row['size'])
                    self.uc.mem_write(row['address'],bytes.fromhex(row['source_initial_hex']))
        for extra in [fixture]+list(extra_fixtures):
            for row in extra.get('data',[]):
                raw = bytes.fromhex(row['bytes_hex'])
                self.map(row['address'],len(raw))
                self.uc.mem_write(row['address'],raw)
        for row in fixture['relocations']:
            if row['symbol'] not in self.virtual_tables:
                self.virtual_tables[row['symbol']] = self.alloc(0x200)
                if not row['symbol'].startswith(('_ZTV','_ZTT')) or row['type'] != 6:
                    raise ValueError('unexpected Save relocation')
            self.map(row['address'],8)
            self.put64(row['address'],self.virtual_tables[row['symbol']])
        vt = self.virtual_tables['_ZTVSt14basic_ofstreamIcSt11char_traitsIcEE']
        self.put64(vt,0x100)
        vtt = self.virtual_tables['_ZTTSt14basic_ofstreamIcSt11char_traitsIcEE']
        self.put64(vtt+8,vt+0x18)
        self.put64(vtt+0x10,vt+0x40)
        self.mail = self.alloc(4096)
        self.put64(self.objects['kernel_address'],self.mail)
        self.put32(self.objects['kernel_fd'],900)
        self.put64(self.objects['user_request']+8,71)
        self.put64(self.mail,1)
        self.uc.mem_write(self.objects['MY_KMOD_LOADED'],b'\1')
        self.uc.mem_write(self.objects['GLOBAL_IS_NVL'],b'\1')
        self.mail_io, self.boundary_calls, self.field_events, self.oct_snapshots, self.function_entries = [], [], [], [], []
        self.mmio_reads = {'pre':0,'post':0}
        self.oct_object = None
        self.helper_addresses = {self.entries[name]:name for name in HELPERS if name in self.entries}
        self.saved_bytes = None
        self.saved_defined = set()
        self.global_vf_calls = []
        self.profile_guards = {}
        decoder = Cs(CS_ARCH_X86,CS_MODE_64)
        decoder.detail = True
        for fn in (next(f for f in profile.load_fixture()['functions'] if f['symbol']==profile.LOAD),
                   next(f for f in fixture['functions'] if f['symbol']==profile.SAVE)):
            for instruction in decoder.disasm(bytes.fromhex(fn['code_hex']),fn['address']):
                pc = instruction.address
                if 0x6192cf <= pc < 0x619466 or 0x617390 <= pc < 0x617530 or pc == 0x619290:
                    for operand in instruction.operands:
                        if operand.type==X86_OP_MEM and operand.mem.base==X86_REG_RDX and operand.size<=2:
                            self.profile_guards[pc] = (operand.mem.disp,operand.size)
        self.post_ratio_inputs = copy.deepcopy(inputs)
        data = bytearray(self.source_payload)
        for offset,value in zip((0x20,0x22,0x24),inputs.get('clocks',[0,0,0])):
            struct.pack_into('<H',data,offset,value)
        for offset in (0x1626,0x162a,0x162e,0x1632,0x1678,0x167c,0x1680,0x1684,0x1688):
            struct.pack_into('<I',data,offset,0x10000000+offset)
        data[0x1637] = inputs.get('ngu_ratio',37)
        data[0x168e:0x1692] = bytes(inputs.get('ufs_bytes',[11,12,41,42]))
        data[0x1692] = inputs.get('checkbox',1)
        data[0x1693] = inputs.get('global_vf_count',0)
        for point in range(15):
            struct.pack_into('<h',data,0x1694+2*point,point-7)
        for offset in range(0x16b2,0x18f2):
            data[offset] = (offset-0x16b2+inputs.get('oct_seed',1)) & 255
        for patch in inputs.get('tail_patches',[]):
            offset,raw = patch['offset'],bytes.fromhex(patch['hex'])
            if not 0 <= offset <= profile.PROFILE_SIZE-len(raw):
                raise ValueError('profile patch outside buffer')
            data[offset:offset+len(raw)] = raw
        self.source_payload = bytes(data)

    def put_vector(self, obj, values, width=4):
        data = self.alloc(len(values)*width)
        if values:
            self.uc.mem_write(data,b''.join((v & ((1<<(width*8))-1)).to_bytes(width,'little') for v in values))
        self.uc.mem_write(obj,struct.pack('<3Q',data,data+width*len(values),data+width*len(values)))

    def prepare(self, symbol):
        super().prepare(ratios.SAVE_RATIO if symbol == profile.SAVE else profile.LOAD)
        self.entry = symbol
        original = self.reg('rdi')
        self.control = self.alloc(0x400)
        self.uc.mem_write(self.control,bytes(self.uc.mem_read(original,0x200)))
        self.reg('rdi',self.control)
        self.config = self.u64(self.objects['GLOBAL_NVL_MEM_CFG'])
        self.put64(self.config+0x18,self.inputs.get('mmio_base',0x123450000))
        self.put64(self.control+0x300,self.alloc(0x80))
        self.put64(self.control+0x60,self.alloc(32))
        self.uc.mem_write(self.control+0x41,bytes([int(self.inputs.get('external_clock',False))]))
        count = self.inputs.get('topology_count',max(self.inputs.get('core_count',1),1))
        types = self.inputs.get('core_types',[i%2 for i in range(count)])
        self.put_vector(self.config+0x70,self.inputs.get('active_cores',[1]*count),1)
        self.put_vector(self.config+0xb0,self.inputs.get('ufs_cpus',list(range(count))))
        self.put_vector(self.config+0x180,list(range(count)))
        self.put_vector(self.config+0xc28,types)
        self.put32(self.config+0xb14,self.inputs.get('ufs_index0',0))
        self.put32(self.config+0xb18,self.inputs.get('ufs_index1',min(count-1,1)))
        bitmask = self.alloc(8*((count+63)//64))
        self.put64(self.config+0x150,bitmask)
        self.put64(self.config+0x160,bitmask+8*(count//64))
        self.put32(self.config+0x168,count%64)
        if symbol == profile.SAVE:
            self.phase = 'save'
            ui = self.u64(self.u64(self.control+0x30)+0x30)
            for offset in (0x1b0,0x4a8,0x168,0x490):
                ptr = self.alloc(16)
                self.put64(ui+offset,ptr)
                self.widget_ptrs[ptr] = offset
        elif symbol == UFS_SET:
            self.phase = 'ufs'
            self.reg('rdi',self.config)
            self.reg('rsi',self.inputs.get('ufs_kind',1))
            self.reg('rdx',self.inputs.get('ufs_selector',0))
            self.reg('rcx',self.inputs.get('minimum',11))
            self.reg('r8',self.inputs.get('maximum',41))
        elif symbol == UFS_GET:
            self.phase = 'ufs'
            self.reg('rdi',self.output)
            self.reg('rsi',self.config)
            self.reg('rdx',self.inputs.get('ufs_kind',1))
            self.reg('rcx',self.inputs.get('ufs_selector',0))
        elif symbol == BCLK:
            self.phase = 'clock'
            self.reg('rdi',self.inputs.get('clock_kind',0))
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_XMM0,int.from_bytes(struct.pack('<d',self.inputs.get('clock_value',100.0)),'little'))
        elif symbol == VF:
            self.phase = 'vf-global'
            self.reg('rdi',self.inputs.get('point',1))
            self.reg('rsi',self.inputs.get('offset',0))
            self.reg('rdx',self.inputs.get('domain',2))
        elif symbol in (VF_QUERY,VF_CORE_QUERY):
            self.phase = 'vf-query'
            self.reg('rdi',self.output)
            self.reg('rsi',self.inputs.get('domain',2) if symbol==VF_QUERY else self.inputs.get('core',0))
        elif symbol != profile.LOAD:
            raise ValueError('unsupported application experiment')

    def snapshot(self, label):
        if self.oct_object is not None:
            self.oct_snapshots.append(dict(label=label, fields={hex(off):self.vector_values(self.oct_object+off) for off in OCT_DATA}))

    def hook(self, uc, pc, size, unused):
        if pc in self.profile_guards:
            displacement,width = self.profile_guards[pc]
            offset = self.reg('rdx')+displacement-self.profile_buffer
            if not 0 <= offset <= profile.PROFILE_SIZE-width:
                self.events.append(dict(pc=pc,offset=offset,size=width,scope='harness boundary; original has no bound here'))
                self.stop('profile-access-boundary')
                return
        if self.entry == profile.LOAD and pc in PHASES:
            self.phase = PHASES[pc]
        if self.entry == profile.LOAD and pc == ratios.LOAD_END:
            base.Machine.hook(self,uc,pc,size,unused)
            return
        if pc in self.helper_addresses:
            name = self.helper_addresses[pc]
            self.function_entries.append(dict(symbol=name,phase=self.phase,msr_writes=len(ratios.writes(self))))
            if name == OCT_CTOR:
                self.oct_object = self.reg('rdi')
            elif name == OCT_DTOR:
                self.snapshot('before-destruction')
            elif name == VF:
                self.global_vf_calls.append(dict(point=cores.signed(self.reg('rdi')),
                    offset=cores.signed(self.reg('rsi')), domain=cores.signed(self.reg('rdx'))))
        if self.entry == profile.LOAD and pc == 0x6192cf:
            self.snapshot('before-profile-copy')
        super().hook(uc,pc,size,unused)
        if self.phase == 'ufs' and pc in (self.entries[cores.TX_READ],self.entries[cores.TX_WRITE]):
            self.stage = 'ufs-read' if pc==self.entries[cores.TX_READ] else 'ufs-write'

    def on_data_read(self, uc, access, address, size, value, unused):
        super().on_data_read(uc,access,address,size,value,unused)
        if self.entry == profile.LOAD and self.read_request and self.profile_buffer <= address < self.profile_buffer+profile.PROFILE_SIZE:
            self.field_events.append(dict(operation='read',pc=self.reg('rip'),offset=address-self.profile_buffer,size=size,
                                          value=int.from_bytes(uc.mem_read(address,size),'little')))

    def on_data_write(self, uc, access, address, size, value, unused):
        super().on_data_write(uc,access,address,size,value,unused)
        if self.entry == profile.SAVE and self.reg('rip') != 0x615648 and self.profile_buffer <= address < self.profile_buffer+profile.PROFILE_SIZE:
            offset = address-self.profile_buffer
            self.saved_defined.update(range(offset,offset+size))
            self.field_events.append(dict(operation='write',pc=self.reg('rip'),offset=offset,size=size,value=value))

    def external(self, name):
        # Phase selection is a harness fault-injection control, never an
        # emulated return value or a replacement for the original wrappers.
        if name in ('open@plt','lseek@plt','read@plt','write@plt','close@plt') and self.inputs.get('fault_phase'):
            selected = self.inputs.pop('fault_phase')
            mode = self.inputs.get('io_mode','success')
            if selected != self.phase:
                self.inputs['io_mode'] = 'success'
            try:
                return self.external(name)
            finally:
                self.inputs['io_mode'] = mode
                self.inputs['fault_phase'] = selected
        if name == 'lseek@plt':
            self.requested_msr = self.reg('rsi')
            if self.requested_msr not in (0x150,0x1ad,0x650,0x607,0x608,0x620,0x199) or self.reg('rdx'):
                raise ValueError('unexpected MSR '+hex(self.requested_msr))
            active = self.phase != 'prefix' and (not self.inputs.get('fault_stage') or self.inputs['fault_stage']==self.stage)
            mode = self.inputs.get('io_mode','success') if active else 'success'
            value = -1 if mode in ('seek-error','open-error') else self.requested_msr
            self.io.append(dict(op='lseek',offset=self.requested_msr,result=value,phase=self.phase,path=self.device_path))
            self.ret(value)
            return
        if name == 'write@plt' and self.reg('rdi') == 900:
            return ngu.NguMachine.external(self,name)
        if name in (CPU_CLK,PCIE_CLK,'_ZN10intel_ctl67refreshEv'):
            row = dict(symbol=name,phase=self.phase,msr_writes=len(ratios.writes(self)),scope='synthetic callee boundary')
            if name in (CPU_CLK,PCIE_CLK):
                bits = self.uc.reg_read(unicorn.x86_const.UC_X86_REG_XMM0) & 0xffffffff
                row.update(binary32_hex=hex(bits),value=struct.unpack('<f',bits.to_bytes(4,'little'))[0])
            self.boundary_calls.append(row)
            self.ret()
            return
        if name == '_ZN15QAbstractButton10setCheckedEb':
            self.boundary_calls.append(dict(symbol=name,checked=bool(self.reg('rsi')),scope='synthetic Qt; signals not emitted'))
            self.ret()
            return
        if name == '_ZNK15QAbstractButton9isCheckedEv':
            self.ret(int(self.inputs.get('save_checked',True)))
            return
        if self.entry == profile.SAVE and name in ('_ZNK9QLineEdit4textEv','_ZNK7QString6toUIntEPbi'):
            self.entry = ratios.SAVE_RATIO
            try:
                return super().external(name)
            finally:
                self.entry = profile.SAVE
        if name == '_ZNK7QString8toDoubleEPb':
            if self.reg('rsi'):
                raise ValueError('Save float ok pointer changed')
            value = self.inputs.get('save_clocks',[100.25,101.5,99.75])[sum(r.get('symbol')==name for r in self.boundary_calls)]
            self.boundary_calls.append(dict(symbol=name,widget_offset=self.last_widget,value=value))
            self.uc.reg_write(unicorn.x86_const.UC_X86_REG_XMM0,int.from_bytes(struct.pack('<d',value),'little'))
            self.ret()
            return
        if self.entry == profile.SAVE and name.startswith('_ZN11QFileDialog15getSaveFileName'):
            self.put64(self.reg('rdi'),self.descriptor('' if self.inputs.get('cancel') else 'fixture.pro'))
            self.boundary_calls.append(dict(symbol=name,msr_writes=len(ratios.writes(self))))
            self.ret(self.reg('rdi'))
            return
        if self.entry == profile.SAVE and name == '_ZN5QFile4openE6QFlagsIN9QIODevice12OpenModeFlagEE':
            if self.reg('rsi') != 0x12:
                raise ValueError('unexpected save open mode')
            self.ret(int(self.inputs.get('qt_open',not self.inputs.get('cancel',False))))
            return
        if self.entry == profile.SAVE and name == profile.FILEBUF_OPEN:
            if self.reg('rdx') != 0x14:
                raise ValueError('unexpected save stream open mode')
            value = self.reg('rdi') if self.inputs.get('save_open',True) else 0
            self.file_events.append(dict(operation='filebuf.open',flags=0x14,result=value))
            self.ret(value)
            return
        if self.entry == profile.SAVE and name == '_ZNKSt12__basic_fileIcE7is_openEv@plt':
            self.ret(int(self.inputs.get('save_open',True)))
            return
        if name == profile.WRITE:
            self.save_request = dict(count=self.reg('rdx'),buffer=self.reg('rsi'))
            if self.save_request != dict(count=profile.PROFILE_SIZE,buffer=self.profile_buffer):
                raise ValueError('original Save output moved')
            self.saved_bytes = bytes(self.uc.mem_read(self.profile_buffer,profile.PROFILE_SIZE))
            self.file_events.append(dict(operation='ostream.write',requested=profile.PROFILE_SIZE,synthetic_failure=self.inputs.get('save_write_error',False)))
            if self.inputs.get('save_write_error'):
                self.put32(self.reg('rdi')+0x100+0x20,1)
            self.ret(self.reg('rdi'))
            return
        if name in ('_ZN11QMessageBoxC1EP7QWidget','_ZN11QMessageBoxC2EP7QWidget','_ZN11QMessageBoxD1Ev','_ZN11QMessageBoxD2Ev'):
            self.ret()
            return
        if name == 'memmove@plt' or name == 'memset@plt':
            size = self.reg('rdx')
            if size > 65536:
                raise ValueError('unbounded synthetic memory operation')
            if size:
                raw = bytes([self.reg('rsi') & 255])*size if name=='memset@plt' else bytes(self.uc.mem_read(self.reg('rsi'),size))
                self.uc.mem_write(self.reg('rdi'),raw)
                if self.entry == profile.SAVE:
                    start = max(self.reg('rdi'),self.profile_buffer)-self.profile_buffer
                    end = min(self.reg('rdi')+size,self.profile_buffer+profile.PROFILE_SIZE)-self.profile_buffer
                    self.saved_defined.update(range(start,end))
            self.ret(self.reg('rdi'))
            return
        if name == 'read@plt' and self.phase not in ('prefix','core','ratio'):
            previous_low,previous_high = self.inputs.get('reply_low',0x1234),self.inputs.get('reply_high',0)
            command = self.last_high & 0xfff
            low,high = self.inputs.get('replies',{}).get(hex(command),0x12345678),0
            if self.requested_msr == 0x620:
                low,high = self.inputs.get('ufs_reply',[0xaabb2933,0x11223344])
            elif self.requested_msr == 0x608:
                low,high = self.inputs.get('nvl_low',0x12345678),self.inputs.get('nvl_high',0x11223344)
            elif self.last_high & 255 == 0x32:
                point = (self.last_high >> 27) & 15
                core_id = (self.last_high >> 13) & 0x1fff
                low = self.inputs.get('query_vf_words',{}).get(str(core_id),self.inputs.get('query_vf_word',0))
                high = int(point >= self.inputs.get('query_vf_count',2))
            self.inputs['reply_low'],self.inputs['reply_high'] = low,high
            try:
                return super().external(name)
            finally:
                self.inputs['reply_low'],self.inputs['reply_high'] = previous_low,previous_high
        return super().external(name)


def ranges(offsets):
    """Half-open byte ranges; preserve every hole in the saved profile."""
    result = []
    for value in sorted(offsets):
        if result and result[-1][1] == value:
            result[-1][1] += 1
        else:
            result.append([value,value+1])
    return result


def vf_rows(machine):
    start,end = machine.u64(machine.output),machine.u64(machine.output+8)
    if end < start or (end-start)%24 or end-start > 16*24:
        raise ValueError('unexpected VF result descriptor')
    return [machine.vector_values(p) for p in range(start,end,24)]


def observe(machine, result, label, inputs, entry):
    row = cores.observe(machine,result,label,inputs,entry)
    row.update(function_entries=machine.function_entries, callee_boundaries=machine.boundary_calls,
               file_events=machine.file_events, texts=machine.qt_texts, mmio_requests=machine.mail_io,
               field_accesses=machine.field_events, octvb=machine.oct_snapshots,
               global_vf_calls=machine.global_vf_calls)
    if entry == UFS_GET:
        row['result_vector'] = machine.vector_values(machine.output)
    elif entry in (VF_QUERY,VF_CORE_QUERY) and result['outcome']=='returned':
        row['result_vectors'] = vf_rows(machine)
    if machine.saved_bytes is not None:
        raw = machine.saved_bytes
        row['export'] = dict(size=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
            defined_byte_count=len(machine.saved_defined),defined_ranges=ranges(machine.saved_defined),
            unwritten_ranges=ranges(set(range(profile.PROFILE_SIZE))-machine.saved_defined),
            header_hex=raw[:32].hex(),clock_words=list(struct.unpack_from('<3H',raw,0x20)),
            global_vf_count=raw[0x1693],global_vf_sixteenth=struct.unpack_from('<h',raw,0x16b2)[0])
    return row


def effect_sequence(machine):
    return ([(r['path'],r['requested_msr'],r['low'],r['high'],r['result']) for r in ratios.writes(machine)],
            machine.mail_io)


def investigate(fixture):
    rows = []

    def case(label, inputs, entry=profile.LOAD, outcome='returned', budget=400000, payload=None):
        m = ApplyMachine(fixture,copy.deepcopy(inputs))
        if payload is not None:
            m.source_payload = payload
        result = m.run(entry,instruction_limit=budget)
        assert result['outcome'] == outcome, (label,inputs,result)
        rows.append(observe(m,result,label,inputs,entry))
        return m

    for entry,count in itertools.product((profile.SAVE,profile.LOAD),(1,2,8)):
        m = case('complete-original-caller',dict(core_count=count,logical=2),entry)
        assert ('Saved!' if entry==profile.SAVE else 'Applied!') in m.qt_texts
    for seed in (0,0x5a,0xa5):
        m = case('save-untouched-stack-bytes',dict(initial_buffer_byte=seed,core_count=2),profile.SAVE)
        assert m.saved_bytes[:32] == bytes([seed])*32
        assert m.saved_bytes[0xa6+8:0x2a6] == bytes([seed])*(0x200-8)
        assert len(m.saved_defined) == 172
    for inputs in (dict(cancel=True),dict(qt_open=False),dict(save_open=False),
                   dict(save_write_error=True),dict(close_error=True)):
        m = case('save-file-failure',inputs,profile.SAVE)
        assert len(ratios.writes(m))==28
        if inputs.get('save_open') is False:
            assert m.saved_bytes is None and 'Saved!' not in m.qt_texts and 'File not saved' not in m.qt_texts
        else:
            expected = 'Saved!' if inputs.get('save_write_error') or inputs.get('close_error') else 'File not saved'
            assert expected in m.qt_texts, (inputs,m.qt_texts)
    for delivered in (0,32,0x1637,0x1693,0x18f1):
        m = case('unchecked-short-file-read',dict(actual_read=delivered,initial_buffer_byte=0,logical=2))
        assert 'Applied!' in m.qt_texts and m.read_request['unchecked_short_read']
    pair = [case('octvb-profile-copy-not-submitted',dict(oct_seed=seed,logical=2)) for seed in (1,113)]
    assert effect_sequence(pair[0]) == effect_sequence(pair[1])
    assert pair[0].oct_snapshots[0] == pair[1].oct_snapshots[0]
    assert pair[0].oct_snapshots[-1] != pair[1].oct_snapshots[-1]
    for m in pair:
        entered = [r['symbol'] for r in m.function_entries]
        assert OCT_CTOR in entered and OCT_DTOR in entered
        assert OCT_SET_CORE not in entered and OCT_SET_TRL not in entered
    case('octvb-current-topology-exceeds-file',dict(core_count=0,core_ids=list(range(130)),
         topology_count=130,logical=0),outcome='profile-access-boundary')
    for count in (0,1,15,16,17):
        for entry in (VF_QUERY,VF_CORE_QUERY):
            m = case('original-vf-query-cardinality',dict(query_vf_count=count),entry)
            assert len(vf_rows(m)) == min(count,16)
    saved = case('original-query-sixteen-save',dict(query_vf_count=16,core_count=2,
                 query_vf_words={'0':0,'1':0x400000}),profile.SAVE)
    loaded = case('complete-save-load-vf-overlap',dict(core_count=2,logical=2),payload=saved.saved_bytes)
    assert loaded.vf_calls[15]['offset']==2
    assert len(loaded.global_vf_calls)==16 and loaded.global_vf_calls[-1]['offset']==86
    assert saved.saved_bytes[0x1693]==16 and struct.unpack_from('<h',saved.saved_bytes,0x16b2)[0]==86
    for count in (0,1,15,16,255):
        m = case('unchecked-global-vf-count',dict(global_vf_count=count,logical=0))
        assert len(m.global_vf_calls)==count
    for kind,selector in itertools.product((0,1),(-1,0,1,2)):
        inputs = dict(ufs_kind=kind,ufs_selector=selector,topology_count=2,ufs_cpus=[3,9])
        m = case('ufs-cpu-selection',inputs,UFS_SET)
        assert [(r['path'],r['low'],r['high']) for r in ratios.writes(m)] == [
            ('/dev/cpu/{}/msr'.format(3 if selector<=0 else 9),0xaabb0b29,0x11223344)]
        m = case('ufs-read-two-bytes',inputs,UFS_GET)
        assert m.vector_values(m.output)==[0x33,0x29]
    for minimum,maximum in ((-1,256),(256,-1),(257,511),(42,11),(0x7fffffff,-2147483648)):
        m = case('ufs-no-range-or-order-check',dict(minimum=minimum,maximum=maximum),UFS_SET)
        assert ratios.writes(m)[0]['low']==0xaabb0000|((minimum&255)<<8)|(maximum&255)
    for mode in ('read-error','read-eof','short-read-4','seek-error','write-error','short-write','close-error'):
        m = case('ufs-ignored-io-result',dict(io_mode=mode),UFS_SET)
        assert len(ratios.writes(m))==1
        if mode in ('read-error','read-eof'):
            assert (ratios.writes(m)[0]['low'],ratios.writes(m)[0]['high'])==(0xa5a50b29,0xa5a5a5a5)
    for external,clocks in itertools.product((False,True),([0,0,0],[10001,10002,10003],[65535,1,65535])):
        m = case('load-clock-routing',dict(external_clock=external,clocks=clocks,logical=2))
        clock_calls=[r for r in m.boundary_calls if r['symbol'] in (CPU_CLK,PCIE_CLK)]
        assert len(clock_calls)==(2 if external and any(clocks) else 0)
    for values,expected in (([100.25,101.5,99.75],[10025,10150,9975]),([655.36,-1,1e12],[0,65436,0])):
        m = case('save-clock-cvtt-truncation',dict(save_clocks=values),profile.SAVE)
        assert list(struct.unpack_from('<3H',m.saved_bytes,0x20))==expected
    for kind,value in itertools.product((0,1),(-1,0,100.01,655.35,2097.152,1e12)):
        m = case('arl-clock-low-21-bits',dict(clock_kind=kind,clock_value=value),BCLK)
        word = int(value*1000) if -2147483648 <= value*1000 < 2147483648 else -2147483648
        assert [(r['low'],r['high']) for r in ratios.writes(m)] == [
            (0,0x80000022 if kind else 0x80000222),
            (0x12200000|(word&0x1fffff),0x80000023 if kind else 0x80000223)]
    for mode in ('read-error','read-eof','busy-forever','short-read-4','short-read-7'):
        case('arl-clock-poll-stall',dict(io_mode=mode),BCLK,'instruction-limit',5000)
    for phase in ('ufs','policy','vf-global','wr199'):
        m = case('full-load-ignored-write-failure',dict(io_mode='write-error',fault_phase=phase,logical=2,global_vf_count=2))
        assert 'Applied!' in m.qt_texts
        assert any(r['result']==-1 for r in ratios.writes(m))
    return dict(schema=1,source_elf_sha256=base.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
        ratio_fixture_sha256=ratios.FIXTURE_SHA,ngu_fixture_sha256=ngu.FIXTURE_SHA,
        fixture_code_bytes=sum(f['size'] for f in fixture['functions']),scope=fixture['scope'],
        environment=dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
        cases_characterized=len(rows),outcomes=dict(Counter(r['outcome'] for r in rows)),observations=rows)


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
        print('fixture SHA '+hashlib.sha256(raw).hexdigest())
    else:
        fixture = load_fixture(args.fixture)
    if args.output:
        report = investigate(fixture)
        args.output.write_bytes(profile.encoded(report))
        print(str(report['cases_characterized'])+' complete NVL caller/helper cases; synthetic I/O only')
    elif not args.binary:
        parser.error('provide --output')


if __name__=='__main__':
    main()
