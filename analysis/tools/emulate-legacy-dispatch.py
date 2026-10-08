#!/usr/bin/env python3
"""Bounded original platform decisions with synthetic inputs, never hardware.

This is a characterization harness, not a launcher, PCI/DMI override, recovered
GUI, or register implementation. It stops BEFORE every panel constructor. No
ELF entry point, Qt library, device I/O or kernel code can execute here.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX,
    UC_X86_REG_RDX, UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_R8, UC_X86_REG_R9,
    UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_FS_BASE)

SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FIXTURE_SHA = '517a81ab352df1c257cf74146a6cb5c47a2be640abf7c5215caf060f25e62366'
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'analysis/fixtures/legacy-platform-dispatch.json'
STACK, HEAP, TLS, STOP = 0x60000000, 0x61000000, 0x62000000, 0x63000000
LIMIT = 10000
MASK = (1 << 64) - 1
CONTROLS = '_ZN10MainWindow27on_actionControls_triggeredEv'
REGS = dict(rax=UC_X86_REG_RAX, rbx=UC_X86_REG_RBX, rcx=UC_X86_REG_RCX,
            rdx=UC_X86_REG_RDX, rdi=UC_X86_REG_RDI, rsi=UC_X86_REG_RSI,
            r8=UC_X86_REG_R8, r9=UC_X86_REG_R9, rsp=UC_X86_REG_RSP, rip=UC_X86_REG_RIP)


def load_fixture(path=FIXTURE):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != FIXTURE_SHA:
        raise ValueError('dispatch fixture hash mismatch')
    fixture = json.loads(data)
    if fixture['source_elf_sha256'] != SOURCE_SHA or fixture['schema'] != 1:
        raise ValueError('dispatch fixture source/schema mismatch')
    return fixture


class Machine:
    def __init__(self, fixture, inputs):
        self.inputs = inputs
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.pages, self.instructions, self.entries = set(), {}, {}
        self.calls, self.searches, self.events, self.qt_texts = [], [], [], []
        self.allocations, self.deleted = {}, []
        self.heap = HEAP
        self.steps, self.outcome, self.panel, self.panel_flag = 0, None, None, None
        self.symbols = {}
        for fn in fixture['functions']:
            code = bytes.fromhex(fn['code_hex'])
            if len(code) != fn['size'] or hashlib.sha256(code).hexdigest() != fn['code_sha256']:
                raise ValueError('function bytes mismatch')
            self.map(fn['address'], len(code))
            self.uc.mem_write(fn['address'], code)
            decoded = list(Cs(CS_ARCH_X86, CS_MODE_64).disasm_lite(code, fn['address']))
            if sum(x[1] for x in decoded) != len(code):
                raise ValueError('incomplete instruction decode')
            self.instructions.update({pc: mnemonic for pc, _, mnemonic, _ in decoded})
            self.entries[fn['symbol']] = fn['address']
            for call in fn['calls']:
                if call['symbols']:
                    previous = self.symbols.get(call['target'])
                    if previous and not set(previous) & set(call['symbols']):
                        raise ValueError('conflicting call binding')
                    self.symbols[call['target']] = call['symbols']
        for target in self.symbols:
            self.map(target, 1)
            if target not in self.instructions:
                self.uc.mem_write(target, b'\xc3')  # stop translation before any synthetic boundary body
        for row in fixture['literals']:
            value = row['text'].encode('utf-8') + b'\0'
            self.map(row['address'], len(value))
            self.uc.mem_write(row['address'], value)
        self.objects = {name: row['address'] for name, row in fixture['objects'].items()}
        for name, row in fixture['objects'].items():
            self.map(row['address'], row['size'])
            self.uc.mem_write(row['address'], bytes(row['size']))
        for name, key in [('GLOBAL_IS_NVL', 'nvl'), ('GLOBAL_IS_GNR_SP', 'gnr'), ('IS_ARL_GLOBAL', 'arl')]:
            if name in self.objects:
                self.uc.mem_write(self.objects[name], bytes([bool(inputs.get(key, False))]))
        # Synthetic Qt shared-null descriptor. No original Qt instructions run.
        if '_ZN10QArrayData11shared_nullE' in self.objects:
            self.put32(self.objects['_ZN10QArrayData11shared_nullE'], -1)
        self.map(STACK, 0x10000)
        self.map(HEAP, 0x100000)
        self.map(TLS, 4096)
        self.map(STOP, 4096)
        self.uc.reg_write(UC_X86_REG_FS_BASE, TLS)
        self.put64(TLS + 0x28, 0xaabbccddeeff0000)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def map(self, address, size):
        for page in range(address & ~4095, (address + max(size, 1) + 4095) & ~4095, 4096):
            if page not in self.pages:
                self.uc.mem_map(page, 4096)
                self.pages.add(page)

    def reg(self, name, value=None):
        if value is None:
            return self.uc.reg_read(REGS[name])
        self.uc.reg_write(REGS[name], value & MASK)

    def put32(self, ptr, value):
        self.uc.mem_write(ptr, struct.pack('<I', value & 0xffffffff))

    def put64(self, ptr, value):
        self.uc.mem_write(ptr, struct.pack('<Q', value & MASK))

    def u64(self, ptr):
        return struct.unpack('<Q', self.uc.mem_read(ptr, 8))[0]

    def alloc(self, size):
        if not 0 <= size <= 65536:
            raise ValueError('synthetic allocation out of range')
        ptr = self.heap
        self.heap += (max(size, 1) + 15) & ~15
        if self.heap >= HEAP + 0x100000:
            raise ValueError('synthetic heap exhausted')
        self.allocations[ptr] = size
        return ptr

    def cpp_string(self, obj, text):
        data = text.encode('utf-8')
        ptr = obj + 16 if len(data) <= 15 else self.alloc(len(data) + 1)
        self.put64(obj, ptr)
        self.put64(obj + 8, len(data))
        if ptr != obj + 16:
            self.put64(obj + 16, len(data))
        self.uc.mem_write(ptr, data + b'\0')

    def vector(self, obj, rows):
        start = self.alloc(24 * len(rows))
        for n, row in enumerate(rows):
            item = self.alloc(4 * len(row))
            if row:
                self.uc.mem_write(item, struct.pack('<{}I'.format(len(row)), *row))
            self.uc.mem_write(start + n * 24, struct.pack('<3Q', item, item + len(row)*4, item + len(row)*4))
        self.uc.mem_write(obj, struct.pack('<3Q', start, start + len(rows)*24, start + len(rows)*24))

    def ret(self, value=0):
        sp = self.reg('rsp')
        dest = self.u64(sp)
        self.reg('rsp', sp + 8)
        self.reg('rax', value)
        self.reg('rip', dest)

    def stop(self, outcome):
        self.outcome = outcome
        self.uc.emu_stop()

    def external(self, name):
        p = self.inputs
        self.calls.append(name)
        value = 0
        if name == '_Z17ReadPciConfigWordjj':
            if self.reg('rcx') != 0 or self.reg('rdx') not in (0, 2):
                raise ValueError('unexpected PCI arguments')
            key = 'vendor' if self.reg('rdx') == 0 else 'device'
            value = p.get(key, 0xffff)
            self.events.append({'pci_word_offset': self.reg('rdx'), 'synthetic_value': value})
        elif name == 'usleep@plt':
            if self.reg('rdi') != 1000:
                raise ValueError('unexpected delay argument')
        elif name in ('_Z17FindPciDeviceByIdtth', '_Z22FindPciDeviceById_realtth'):
            query = [self.reg('rcx'), self.reg('rdx'), self.reg('r8')]
            self.events.append({'pci_find': query})
            if name == '_Z22FindPciDeviceById_realtth':
                if query != [0x8086, 0x3251, 0]:
                    raise ValueError('unexpected HEDT query')
                value = p.get('hedt_bdf', -1)
            else:
                if query[0] != 0x8086 or query[2] != 0:
                    raise ValueError('unexpected RKL query')
                value = p.get('rkl_matches', {}).get(query[1], -1)
        elif name == '_Z16GetPciDeviceListv':
            self.vector(self.reg('rcx'), p.get('pci_rows', []))
            value = self.reg('rcx')
        elif name in ('_Z7getmoboB5cxx11v', '_Z13getmobo_brandB5cxx11v'):
            self.cpp_string(self.reg('rdi'), p.get('board' if name == '_Z7getmoboB5cxx11v' else 'brand', ''))
            value = self.reg('rdi')
        elif name == '_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4findEPKcmm@plt':
            obj = self.reg('rdi')
            data = bytes(self.uc.mem_read(self.u64(obj), self.u64(obj+8)))
            needle = bytes(self.uc.mem_read(self.reg('rsi'), self.reg('rcx')))
            value = data.find(needle, self.reg('rdx'))
            self.searches.append(needle.decode('ascii'))
        elif name == '_Z8checkvrmv':
            value = p.get('vrm_result', 0)
        elif name in ('_ZdlPvm@plt', '_ZN10QArrayData10deallocateEPS_mm'):
            ptr = self.reg('rdi')
            if ptr not in self.allocations or ptr in self.deleted:
                raise ValueError('invalid synthetic deallocation')
            self.deleted.append(ptr)
        elif name == 'memcpy@plt':
            size = self.reg('rdx')
            if size > 65536:
                raise ValueError('unexpected memcpy size')
            self.uc.mem_write(self.reg('rdi'), bytes(self.uc.mem_read(self.reg('rsi'), size)))
            value = self.reg('rdi')
        elif name == '_Z13getlogicalcpuv':
            value = p.get('logical', 128)
        elif name == 'hwloc_topology_init@plt':
            value = p.get('topology_init', 0)
            if value == 0:
                self.put64(self.reg('rdi'), self.alloc(64))
        elif name == 'hwloc_topology_load@plt':
            value = p.get('topology_load', 0)
        elif name == 'hwloc_get_type_depth@plt':
            if self.reg('rsi') != 2:
                raise ValueError('unexpected hwloc object type')
            value = p.get('depth', 3)
        elif name == 'hwloc_get_nbobjs_by_depth@plt':
            if self.reg('rsi') & 0xffffffff != p.get('depth', 3):
                raise ValueError('unexpected hwloc depth')
            value = p.get('cores', 0)
        elif name == 'hwloc_topology_destroy@plt':
            self.events.append({'topology_destroy': True})
        elif name == '_Z14get_oc_supportv':
            obj = self.reg('rdi')
            data = self.alloc(8)
            self.put32(data, p.get('oc_lock', 0))
            self.put32(data + 4, 7)
            self.uc.mem_write(obj, struct.pack('<3Q', data, data + (0 if p.get('oc_empty') else 8), data + 8))
            value = obj
        elif name == '_Z12rd_oc_enablev':
            value = p.get('oc_enable', 1)
        elif name == '_Z30get_overclocking_optin_supportv':
            obj = self.reg('rdi')
            data = self.alloc(8)
            self.put64(data, p.get('optin_bits', 0))
            self.uc.mem_write(obj, struct.pack('<5Q', data, 0, data, 3, data + 8))
            value = obj
        elif name == '_ZSt24__throw_out_of_range_fmtPKcz@plt':
            self.events.append({'range_error_index': self.reg('rsi'), 'range_error_size': self.reg('rdx')})
            self.stop('range-error-boundary')
            return
        elif name == '_Znwm@plt':
            value = self.alloc(self.reg('rdi'))
        elif name == '_ZN7QString16fromAscii_helperEPKci':
            text = bytes(self.uc.mem_read(self.reg('rdi'), self.reg('rsi'))).decode('utf-8')
            self.qt_texts.append(text)
            value = self.alloc(32)
            self.put32(value, -1)
        elif name == '_ZN7QString15fromUtf8_helperEPKci':
            text = bytes(self.uc.mem_read(self.reg('rsi'), self.reg('rdx'))).decode('utf-8')
            self.qt_texts.append(text)
            value = self.alloc(32)
            self.put32(value, -1)
            self.put64(self.reg('rdi'), value)
        elif name in ('_ZN7QString6appendERKS_', '_ZN7QStringD1Ev',
                      '_ZN11QMessageBoxC1EP7QWidget', '_ZN11QMessageBoxD1Ev',
                      '_ZN7QWidget11setBaseSizeEii'):
            pass
        elif name == '_ZN11QMessageBox7warningEP7QWidgetRK7QStringS4_6QFlagsINS_14StandardButtonEES6_':
            self.events.append({'warning_boundary': True})
        elif name.startswith(('_ZN9intel_ctlC', '_ZN10intel_ctl')) and 'C' in name:
            self.panel = name
            if 'intel_ctl6C' in name:
                self.panel_flag = self.reg('rdx') & 0xff
            self.stop('panel-constructor-boundary')
            return
        else:
            raise ValueError('unexpected external call: ' + name)
        self.ret(value)

    def hook(self, uc, pc, size, unused):
        self.steps += 1
        if pc == STOP:
            self.stop('returned')
        elif pc in self.instructions:
            if self.instructions[pc] in ('syscall', 'sysenter', 'int', 'in', 'out', 'rdmsr', 'wrmsr', 'cpuid'):
                raise ValueError('hardware/OS instruction is forbidden')
        elif pc in self.symbols:
            self.external(self.symbols[pc][0])
        else:
            raise ValueError('execution escaped allowlisted instructions: ' + hex(pc))

    def prepare(self, symbol):
        pass

    def run(self, symbol, instruction_limit=LIMIT, allow_instruction_bound=False):
        sp = STACK + 0xfff8
        self.reg('rsp', sp)
        self.put64(sp, STOP)
        self.output = self.alloc(256)
        self.reg('rdi', self.output)
        if symbol == '_Z9get_oc_okRbS_S_':
            self.reg('rsi', self.output+1)
            self.reg('rdx', self.output+2)
        self.prepare(symbol)
        self.uc.emu_start(self.entries[symbol], STOP+1, timeout=1000000, count=instruction_limit)
        if self.outcome is None:
            if not allow_instruction_bound or self.steps != instruction_limit:
                raise ValueError('instruction/time limit reached without a known boundary')
            self.outcome = 'instruction-limit'
        return {'outcome': self.outcome, 'return_al': self.reg('rax') & 255 if self.outcome == 'returned' else None,
                'steps': self.steps, 'panel': self.panel, 'panel_flag': self.panel_flag,
                'calls': self.calls, 'searches': self.searches, 'events': self.events,
                'qt_texts': self.qt_texts}


def investigate(path=FIXTURE):
    fixture = load_fixture(path)
    rows = []

    def case(name, symbol, inputs, expect):
        machine = Machine(fixture, inputs)
        observed = machine.run(symbol)
        expect(machine, observed)
        rows.append({'case': name, 'function': symbol, 'inputs': inputs, **observed})
        return observed

    def require(condition, message='characterization mismatch'):
        if not condition:
            raise AssertionError(message)

    def boolean(value):
        return lambda m, r: require(r['outcome'] == 'returned' and r['return_al'] == int(value))

    # Boundary values and missing/wrong vendors, rather than mapping device IDs
    # to marketing CPU names. Deliberately test the two vendor-blind routines.
    devices = [0, 0xffff, 0x4647, 0x4648, 0x4660, 0x4668, 0x4669,
               0xa6ff, 0xa700, 0xa707, 0xa708, 0xa780, 0xa781,
               0x7cff, 0x7d00, 0x7fff, 0x8000, 0xd6ff, 0xd700, 0xd740, 0xd741]
    for name, rule in [('_Z8isit_adlv', lambda v,d: v==0x8086 and ((d & 0xffdf)==0x4648 or d==0x4660 or 0xa700<=d<=0xa780)),
                       ('_Z8isit_rplv', lambda v,d: v==0x8086 and 0xa700<=d<=0xa707),
                       ('_Z8isit_arlv', lambda v,d: 0x7d00<=d<=0x7fff),
                       ('_Z8isit_nvlv', lambda v,d: 0xd700<=d<=0xd740)]:
        for vendor, device in itertools.product([0x8086, 0x1022, 0xffff], devices):
            r = case('{}-{:04x}-{:04x}'.format(name, vendor, device), name,
                     dict(vendor=vendor, device=device), boolean(rule(vendor, device)))
            offsets = [e['pci_word_offset'] for e in r['events'] if 'pci_word_offset' in e]
            expected_offsets = [2] if name in ('_Z8isit_nvlv','_Z8isit_arlv') else ([0,2] if vendor==0x8086 else [0])
            require(offsets == expected_offsets, 'PCI access sequence differs')
    for vendor in (0, 0x1001, 0x1002, 0x1022, 0x1023, 0x8086, 0xffff):
        case('amd-vendor-{:04x}'.format(vendor), '_Z12check_if_amdv', dict(vendor=vendor), boolean(vendor in (0x1002, 0x1022)))
    rkl_ids = [0x4c43, 0x4c53, 0x4c63, 0x4c33]
    for match in [None] + rkl_ids:
        r = case('rkl-first-' + str(match), '_Z8isit_rklv', {'rkl_matches': {} if match is None else {match: 0}}, boolean(match is not None))
        require([e['pci_find'][1] for e in r['events']] == rkl_ids[:4 if match is None else rkl_ids.index(match)+1])
    for label, pci_rows in [
        ('empty', []), ('one', [[0,0,0,0x8086,0x3258]]),
        ('two', [[0,0,0,0x8086,0x3258], [128,0,0,0x8086,0x3258]]),
        ('wrong-vendor', [[0,0,0,0x1022,0x3258]]),
        ('wrong-device', [[0,0,0,0x8086,0x3251]]),
        ('mixed', [[0,0,0,0x8086,0x3258], [0,1,0,0x8086,0x3251]]),
        ('duplicates', [[0,0,0,0x8086,0x3258]]*2),
        ('wide-vendor', [[0,0,0,0x18086,0x3258]])]:
        count = sum(r[3:5] == [0x8086,0x3258] for r in pci_rows)
        for fn in ('_Z8isit_sprv', '_Z11isit_gnr_spv'):
            case(fn+'-'+label, fn, {'pci_rows': pci_rows}, boolean(count==1 if fn=='_Z8isit_sprv' else count>1))
    for pci_rows in ([[]], [[0,0,0]], [[0,0,0,0x8086]]):
        for fn in ('_Z8isit_sprv', '_Z11isit_gnr_spv'):
            case(fn+'-short-'+str(len(pci_rows[0])), fn, {'pci_rows': pci_rows},
                 lambda m,r: require(r['outcome']=='range-error-boundary'))
    boards = ['', 'ASUS', 'ROG', 'STRIX', 'MANGO', 'OHTANI', 'GNR', 'asus',
              'ROG MANGO', 'MANGO GNR', 'GNR MANGO', 'XXASUSYY',
              'Pro WS W790-ACE', 'Pro WS W890E-SAGE SE', 'Pro WS TRX50-SAGE WIFI', 'unidentified']
    for board, brand, vrm in itertools.product(boards, ['', 'ASUSTeK COMPUTER INC.', 'ROG', 'STRIX', 'asus'], [0, 1]):
        early = any(s in board for s in ('ASUS','ROG','STRIX'))
        reject = not early and 'MANGO' in board
        accept = early or (not reject and any(s in board for s in ('OHTANI','GNR')))
        needs_vrm = not accept and not reject and any(s in brand for s in ('ASUS','ROG','STRIX'))
        r = case('board-{}-{}-{}'.format(board,brand,vrm), '_Z10is_it_asusv',
                 dict(board=board,brand=brand,vrm_result=vrm), boolean(accept or (needs_vrm and vrm!=0)))
        require(r['calls'].count('_Z8checkvrmv') == int(needs_vrm), 'VRM branch differs')
        require(r['calls'].count('_Z13getmobo_brandB5cxx11v') == int(not accept and not reject))
    for vrm in (-1, 2, 0x100):
        case('vrm-nonzero-'+str(vrm), '_Z10is_it_asusv',
             dict(board='unknown',brand='ASUSTeK',vrm_result=vrm), boolean(True))
    for config, expected_count in [({'cores':18},18), ({'cores':24},24), ({'cores':25},25),
                                  ({'cores':128},128), ({'depth':-1},0), ({'depth':-2},-1),
                                  ({'topology_init':-1},0), ({'topology_load':-1},0)]:
        def verify_proc(m,r):
            observed = struct.unpack('<i',m.uc.mem_read(m.output+0x2c,4))[0]
            r['hwloc_count_field'] = observed
            require(r['outcome']=='returned' and observed==expected_count)
            require(r['calls'].count('hwloc_topology_destroy@plt') == int('topology_init' not in config and 'topology_load' not in config))
        case('proc-'+str(config), '_ZN10proc_classC2Ev', config, verify_proc)
    for lock, enabled, optin_bits in itertools.product([0,1,-1], [0,1], range(8)):
        def verify_oc(m,r):
            flags = list(m.uc.mem_read(m.output,3)); r['out_flags'] = flags
            require(r['outcome']=='returned' and flags == [int(lock!=0),enabled,1])
            require(r['return_al']==int(lock==0 and enabled!=0))
        case('oc-{}-{}-{}'.format(lock,enabled,optin_bits), '_Z9get_oc_okRbS_S_',
             dict(oc_lock=lock,oc_enable=enabled,optin_bits=optin_bits), verify_oc)
    case('oc-empty-vector', '_Z9get_oc_okRbS_S_', {'oc_empty':True},
         lambda m,r: require(r['outcome']=='range-error-boundary'))
    # All combinations of cached flags establish the precedence. Other guards
    # execute their unchanged routines; only PCI/hwloc/library results are fake.
    for amd, nvl, gnr, arl in itertools.product([False,True], repeat=4):
        expected = None if amd else ('intel_ctl6' if nvl else 'intel_ctl5' if gnr else 'intel_ctl3' if arl else 'intel_ctl')
        def verify_route(m,r):
            if expected is None:
                require(r['outcome']=='returned' and r['panel'] is None and 'Not Supported!' in r['qt_texts'])
            else:
                require(r['outcome']=='panel-constructor-boundary' and expected+'C' in r['panel'])
        case('flags-{}{}{}{}'.format(int(amd),int(nvl),int(gnr),int(arl)), CONTROLS,
             dict(vendor=0x1022 if amd else 0x8086,device=0,nvl=nvl,gnr=gnr,arl=arl), verify_route)
    for count, adl, hedt in itertools.product([0,18,24,25,128,-1], [False,True], [False,True]):
        expected = 'intel_ctl2' if ((count>24 and not adl) or hedt) else 'intel_ctl'
        case('fallback-{}-{}-{}'.format(count,adl,hedt), CONTROLS,
             dict(vendor=0x8086,device=0xa700 if adl else 0,cores=count,hedt_bdf=0 if hedt else -1),
             lambda m,r: require(r['outcome']=='panel-constructor-boundary' and expected+'C' in r['panel']))
    for lock, enabled in itertools.product([0,1], repeat=2):
        case('nvl-oc-{}-{}'.format(lock,enabled), CONTROLS,
             dict(vendor=0x8086,nvl=True,oc_lock=lock,oc_enable=enabled),
             lambda m,r: require(r['outcome']=='panel-constructor-boundary' and 'intel_ctl6C' in r['panel']
                                 and r['panel_flag']==int(lock!=0 or enabled==0)
                                 and not any('OC OptIn' in t for t in r['qt_texts'])))
    return {'schema':1, 'source_elf_sha256':SOURCE_SHA, 'fixture_sha256':FIXTURE_SHA,
            'scope':'Bounded original decision instructions; synthetic PCI/DMI/hwloc/Qt boundaries; no OS/hardware/GUI execution',
            'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'instruction_limit_per_case':LIMIT, 'fixture_functions':len(fixture['functions']),
            'cases_passed':len(rows), 'observations':rows}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = investigate(args.fixture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    print('{} bounded decision cases passed; no hardware or GUI executed'.format(result['cases_passed']))


if __name__ == '__main__':
    main()
