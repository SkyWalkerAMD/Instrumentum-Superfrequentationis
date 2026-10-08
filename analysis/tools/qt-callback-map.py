#!/usr/bin/env python3
"""Recover selected Qt5 method-index dispatch, stopping before callback bodies.

Metadata names and native jump tables are independent evidence. The emulator
executes only qt_static_metacall routines and stops at every outside function.
It never invokes a widget, Qt library, device accessor or ELF entry point.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import platform
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP
from elftools.elf.elffile import ELFFile
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX,
                              UC_X86_REG_RCX, UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_FS_BASE)

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
FIXTURE_SHA = '8db630f5cbcf923af436914314409abf36ef8431e2d267a4b46169b7ea175d45'
TARGETS = (
    'MainWindow', 'cpufunctions', 'intel_ctl', 'intel_ctl2', 'intel_ctl3', 'intel_ctl5', 'intel_ctl6',
    'intel_mainstream', 'intel_client', 'intel_hedt_window', 'intel_memtime', 'adl_timings',
    'arl_memtime', 'gnr_memtime', 'nvl_memtime', 'amd_am3', 'amd_am4', 'amd_am5', 'amd_umc',
    'am5_tuners', 'amdvf', 'intel_vf_curve', 'tr5_mb', 'tr5_mb2', 'w790_mb', 'w890_mb',
    'vrmread', 'vrmread_am5', 'ddr5_spd', 'pmic', 'pmic_amd',
    'rw_msr', 'rw_memory', 'rw_pci', 'rw_ecram', 'rw_ioport', 'rw_ocmb', 'rw_ocmb_gnr', 'rw_biosmb',
)


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2)+'\n').encode('utf-8')


def load_fixture(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:
        raise ValueError('Qt fixture hash mismatch')
    value=json.loads(data)
    if value['source_elf_sha256']!=SOURCE_SHA or value['schema']!=1:
        raise ValueError('Qt fixture source/schema mismatch')
    return value


def extract(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA:
        raise ValueError('original ELF SHA mismatch')
    elf = ELFFile(io.BytesIO(data))
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    byname = {s.name:s for s in symbols}
    names = {}
    for s in symbols:
        if s['st_info']['type']=='STT_FUNC' and s['st_value']:
            names.setdefault(s['st_value'],set()).add(s.name)

    def read(va, size):
        for segment in elf.iter_segments():
            rel = va - segment['p_vaddr']
            if segment['p_type']=='PT_LOAD' and 0<=rel and rel+size<=segment['p_filesz']:
                start = segment['p_offset']+rel
                return data[start:start+size]
        raise ValueError('address outside file-backed segments: '+hex(va))

    spec = importlib.util.spec_from_file_location('qt_meta',Path(__file__).with_name('qt-meta-inventory.py'))
    meta = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(meta)
    decoder = Cs(CS_ARCH_X86,CS_MODE_64)
    decoder.detail = True
    result = {'schema':1, 'source_elf_sha256':SOURCE_SHA,
              'scope':'Selected Qt method-index routers only, not callback bodies or hardware behavior', 'classes':[]}
    for cls in TARGETS:
        meta_name = 'qt_meta_data_'+cls
        string_name = 'qt_meta_stringdata_'+cls
        mt = byname['_ZL'+str(len(meta_name))+meta_name]
        st = byname['_ZL'+str(len(string_name))+string_name]
        section = elf.get_section(st['st_shndx'])
        string_offset = section['sh_offset']+st['st_value']-section['sh_addr']
        strings = meta.strings_at(data,string_offset)
        if not strings or strings['class'] != cls:
            raise ValueError('Qt strings mismatch: '+cls)
        raw_meta = read(mt['st_value'],mt['st_size'])
        words = struct.unpack('<{}I'.format(len(raw_meta)//4),raw_meta)
        if words[0] not in (7,8) or words[1]!=0:
            raise ValueError('unsupported Qt metadata revision/class')
        count, first = words[4:6]
        methods = []
        for index in range(count):
            row = words[first+5*index:first+5*(index+1)]
            if len(row)!=5 or row[0]>=len(strings['strings']):
                raise ValueError('invalid Qt method record')
            methods.append({'index':index,'method':strings['strings'][row[0]],'argc':row[1],
                            'parameter_word_offset':row[2],'flags':row[4]})
        entry = '_ZN'+str(len(cls))+cls+'18qt_static_metacallEP7QObjectN11QMetaObject4CallEiPPv'
        routers = [s for s in symbols if s.name==entry or s.name.startswith(entry+'.')]
        routers = sorted(routers,key=lambda s:s['st_value'])
        if entry not in byname or not routers:
            raise ValueError('missing router: '+cls)
        extents = [(s['st_value'],s['st_value']+s['st_size']) for s in routers]
        record = {'class':cls,'metadata_address':mt['st_value'],
                  'metadata_sha256':hashlib.sha256(raw_meta).hexdigest(), 'method_count':count,
                  'methods':methods,'entry':byname[entry]['st_value'], 'functions':[], 'data':[], 'boundaries':[]}
        targets, tables = set(), {}
        for s in routers:
            code = read(s['st_value'],s['st_size'])
            insns = list(decoder.disasm(code,s['st_value']))
            if sum(i.size for i in insns)!=len(code):
                raise ValueError('incomplete router decode')
            record['functions'].append({'symbol':s.name,'address':s['st_value'],'code_hex':code.hex(),
                                        'code_sha256':hashlib.sha256(code).hexdigest()})
            for ins in insns:
                if ins.mnemonic in ('call','jmp') and len(ins.operands)==1 and ins.operands[0].type==X86_OP_IMM:
                    targets.add(ins.operands[0].imm)
                if ins.mnemonic == 'lea':
                    for op in ins.operands:
                        if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:
                            address = ins.address+ins.size+op.mem.disp
                            if address in names:
                                # IndexOfMethod compares a signal's address.
                                # This reference is code, not a jump table.
                                targets.add(address)
                                continue
                            table = read(address,count*4)
                            dests = [address+n for n in struct.unpack('<{}i'.format(count),table)]
                            if not all(any(lo<=d<hi for lo,hi in extents) for d in dests):
                                raise ValueError('RIP operand is not a bounded local jump table: '+cls+' '+hex(address))
                            tables[address] = table
        vtable = byname['_ZTV'+str(len(cls))+cls]
        table = read(vtable['st_value'],vtable['st_size'])
        record['vptr'] = vtable['st_value']+16
        tables[vtable['st_value']] = table
        targets.update(struct.unpack('<{}Q'.format(len(table)//8),table))
        for target in sorted(targets):
            if any(lo<=target<hi for lo,hi in extents):
                continue
            if target in names:
                record['boundaries'].append({'address':target,'symbols':sorted(names[target])})
        record['data'] = [{'address':va,'bytes_hex':b.hex(),'sha256':hashlib.sha256(b).hexdigest()}
                          for va,b in sorted(tables.items())]
        result['classes'].append(record)
    return result


def route(cls, index, call_kind=0):
    uc = Uc(UC_ARCH_X86,UC_MODE_64)
    pages, pcs, boundaries = set(), set(), {}
    stack, obj, argv, args, stop, tls = 0x60000000,0x61000000,0x62000000,0x63000000,0x64000000,0x65000000

    def map_memory(address,size):
        for page in range(address & ~4095,(address+max(size,1)+4095)&~4095,4096):
            if page not in pages:
                uc.mem_map(page,4096)
                pages.add(page)

    def put64(address,value):
        uc.mem_write(address,struct.pack('<Q',value & ((1<<64)-1)))

    decoder = Cs(CS_ARCH_X86,CS_MODE_64)
    for fn in cls['functions']:
        code = bytes.fromhex(fn['code_hex'])
        if hashlib.sha256(code).hexdigest()!=fn['code_sha256']:
            raise ValueError('router hash mismatch')
        map_memory(fn['address'],len(code))
        uc.mem_write(fn['address'],code)
        decoded = list(decoder.disasm_lite(code,fn['address']))
        if sum(i[1] for i in decoded)!=len(code):
            raise ValueError('router decode incomplete')
        for pc,_,mnemonic,_ in decoded:
            if mnemonic in ('syscall','sysenter','int','in','out','rdmsr','wrmsr','cpuid'):
                raise ValueError('hardware/OS instruction forbidden in router')
            pcs.add(pc)
    for row in cls['data']:
        data = bytes.fromhex(row['bytes_hex'])
        if hashlib.sha256(data).hexdigest()!=row['sha256']:
            raise ValueError('router data hash mismatch')
        map_memory(row['address'],len(data))
        uc.mem_write(row['address'],data)
    for row in cls['boundaries']:
        boundaries[row['address']]=row['symbols']
        map_memory(row['address'],1)
        # A never-executed synthetic RET bounds Unicorn's block translation.
        # Zero-filled pages may otherwise be decoded across an unmapped page
        # before the pre-instruction stop hook is called. No callback bytes
        # are supplied or executed; all router bytes remain original.
        uc.mem_write(row['address'],b'\xc3')
    for address in (stack,obj,argv,args,stop,tls):
        map_memory(address,4096)
    put64(obj,cls['vptr'])
    for n in range(64):
        put64(argv+n*8,args+n*32)
        put64(args+n*32,0x1234+n)
    put64(stack+4088,stop)
    uc.reg_write(UC_X86_REG_RSP,stack+4088)
    uc.reg_write(UC_X86_REG_RDI,obj)
    uc.reg_write(UC_X86_REG_RSI,call_kind)
    uc.reg_write(UC_X86_REG_RDX,index & 0xffffffff)
    uc.reg_write(UC_X86_REG_RCX,argv)
    uc.reg_write(UC_X86_REG_FS_BASE,tls)
    state = {'method_index':index, 'call_kind':call_kind,'steps':0,'trace':[]}

    def hook(emulator,pc,size,unused):
        state['steps']+=1
        if pc==stop:
            state['outcome']='returned-without-external-body'
            emulator.emu_stop()
        elif pc in pcs:
            state['trace'].append(pc)
        elif pc in boundaries:
            state.update(outcome='callback-boundary',target=pc,symbols=boundaries[pc])
            emulator.emu_stop()
        else:
            raise ValueError('router execution escaped: '+cls['class']+' '+hex(pc))
    uc.hook_add(UC_HOOK_CODE,hook)
    try:
        uc.emu_start(cls['entry'],stop+1,timeout=1000000,count=512)
    except unicorn.UcError as error:
        raise ValueError('router fault {} index {} at {}: {}'.format(
            cls['class'],index,hex(uc.reg_read(UC_X86_REG_RIP)),error)) from error
    if 'outcome' not in state:
        raise ValueError('router bound exceeded')
    return state


def investigate(fixture):
    results = []
    mismatches = []
    count = 0
    for cls in fixture['classes']:
        rows = []
        for method in cls['methods']:
            row = route(cls,method['index'])
            name = method['method']
            # Simple QObject class names only; the method suffix remains mangled
            # to retain overload distinctions. Vtable routes use original bytes.
            prefix = '_ZN'+str(len(cls['class']))+cls['class']+str(len(name))+name
            matched = row['outcome']=='callback-boundary' and any(s.startswith(prefix) for s in row['symbols'])
            row.update(metadata_method=name,metadata_argc=method['argc'],metadata_target_match=matched)
            if not matched:
                mismatches.append({'class':cls['class'],**row})
            rows.append(row)
            count+=1
        sentinels=[]
        for index,kind in [(-1,0),(cls['method_count'],0),(0x7fffffff,0),(0,1)]:
            row=route(cls,index,kind)
            if row['outcome']!='returned-without-external-body':
                raise ValueError('unexpected sentinel dispatch')
            sentinels.append(row)
            count+=1
        results.append({'class':cls['class'],'method_count':cls['method_count'],
                        'methods':rows,'sentinels':sentinels})
    return {'schema':1,'source_elf_sha256':SOURCE_SHA,'fixture_sha256':hashlib.sha256(encoded(fixture)).hexdigest(),
            'scope':'Original Qt router instructions with synthetic object/arguments; stop before every callback body',
            'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'classes':results,'cases_executed':count,'metadata_mismatches':mismatches}


def main():
    parser=argparse.ArgumentParser(__doc__)
    source=parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--binary',type=Path)
    source.add_argument('--fixture',type=Path)
    parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.binary:
        fixture=extract(args.binary)
    else:
        fixture=load_fixture(args.fixture)
    if fixture['source_elf_sha256']!=SOURCE_SHA or fixture['schema']!=1:
        raise ValueError('Qt fixture source/schema mismatch')
    if args.export_fixture:
        if not args.binary:
            raise ValueError('export requires the complete original ELF')
        args.export_fixture.parent.mkdir(parents=True,exist_ok=True)
        args.export_fixture.write_bytes(encoded(fixture))
        print('fixture SHA',hashlib.sha256(encoded(fixture)).hexdigest())
    result=investigate(fixture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(encoded(result))
    print('{} classes; {} bounded cases; {} metadata/target mismatches'.format(
        len(result['classes']),result['cases_executed'],len(result['metadata_mismatches'])))
    if result['metadata_mismatches']:
        raise SystemExit('Qt metadata/target mismatch; inspect the written report')


if __name__=='__main__':
    main()
