#!/usr/bin/env python3
"""Extract bounded original NVL UI bindings, never run an ELF/Qt/hardware."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
from elftools.elf.elffile import ELFFile

SOURCE_SHA='44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
CTOR='_ZN10intel_ctl6C2EP7QWidgetb'
SETUP='_ZN18Ui_intel_ctl_left27setupUiEP7QWidget'
TRANSLATE='_ZN18Ui_intel_ctl_left213retranslateUiEP7QWidget'
XOC=('_ZN10intel_ctl624on_pushButton_14_clickedEv','_ZN10intel_ctl614on_xoc_clickedEv')
CONNECT='intel_ctl6.constructor.connect-region'


def encoded(value):
    return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def investigate(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=SOURCE_SHA:
        raise ValueError('original ELF hash mismatch')
    elf=ELFFile(io.BytesIO(data))
    byname={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    spec=importlib.util.spec_from_file_location('runtime_audit',Path(__file__).with_name('elf-runtime-audit.py'))
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
    selected=(CTOR,SETUP,TRANSLATE)+XOC
    audited=audit.inspect(path,re.compile('^('+'|'.join(map(re.escape,selected))+')$'),True)
    functions={name:next(f for f in audited['selected_functions'] if name in f['symbols']) for name in selected}
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True

    def read(address,size):
        for segment in elf.iter_segments():
            rel=address-segment['p_vaddr']
            if segment['p_type']=='PT_LOAD' and 0<=rel and rel+size<=segment['p_filesz']:
                start=segment['p_offset']+rel
                return data[start:start+size]
        raise ValueError('outside file-backed ELF')

    fixture={'schema':1,'source_elf_sha256':SOURCE_SHA,
             'scope':'Original UI name/translation/connection slices and two XOC slots; Qt boundaries synthetic; no hardware',
             'functions':[],'objects':{},'literals':[]}
    literals={}; proofs=[]

    def add(parent,label,start,end,preconditions=None):
        f=functions[parent]
        if not f['address']<=start<end<=f['address']+f['size']:
            raise ValueError('slice outside parent function')
        code=read(start,end-start)
        ins=list(decoder.disasm(code,start))
        if sum(i.size for i in ins)!=len(code):raise ValueError('incomplete slice decode')
        calls=[c for c in f['calls'] if start<=c['instruction']<end]
        record={'symbol':label,'address':start,'size':len(code),'code_hex':code.hex(),
                'code_sha256':hashlib.sha256(code).hexdigest(),'calls':calls,
                'parent_symbol':parent,'parent_function_sha256':f['code_sha256']}
        if preconditions is not None:
            record.update(stop_address=end,preconditions=preconditions)
        fixture['functions'].append(record)
        proofs.append({k:v for k,v in record.items() if k!='code_hex'})
        proofs[-1]['instructions']=[dict(address=i.address,mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
        for i in ins:
            if i.mnemonic!='lea':continue
            for op in i.operands:
                if op.type!=X86_OP_MEM or op.mem.base!=X86_REG_RIP:continue
                addr=i.address+i.size+op.mem.disp
                raw=read(addr,512).split(b'\0')[0]
                try:text=raw.decode('utf-8')
                except UnicodeDecodeError:raise ValueError('unexpected non-string RIP reference')
                if not all(c.isprintable() or c in '\n\r\t' for c in text):raise ValueError('unexpected RIP data')
                literals[addr]=text

    for name in (TRANSLATE,)+XOC:
        s=byname[name]
        add(name,name,s['st_value'],s['st_value']+s['st_size'])
    add(CTOR,CONNECT,0x603f55,0x60451d,
        {'rbp':'synthetic intel_ctl6 this','rsp':'synthetic constructor frame; preceding construction is not executed',
         'object_graph':'this+0x30 -> left widget; left+0x30 -> Ui_intel_ctl_left2',
         'optional_widget':'this+0x78 tested with null and nonnull'})

    # Recognize only the repeated generated straight-line assignment pattern.
    # The parent widget's conditional naming is outside these member slices.
    setup=functions[SETUP];code=read(setup['address'],setup['size'])
    ins=list(decoder.disasm(code,setup['address']))
    name_calls=[n for n,i in enumerate(ins) if i.mnemonic=='call' and i.op_str=='0x1532bc0']
    recognized=[]
    for n in name_calls:
        if ins[n-2].mnemonic!='mov' or ins[n-2].op_str!='rsi, r12' or ins[n-1].mnemonic!='mov':
            raise ValueError('unexpected setObjectName arguments')
        target=ins[n-1].op_str.split(', ')[1]
        choices=[]
        for k in range(max(0,n-10),n):
            match=re.fullmatch(r'qword ptr \[rbp(?: \+ (0x[0-9a-f]+|\d+))?\], '+target,ins[k].op_str)
            if ins[k].mnemonic=='mov' and match:choices.append((k,int(match[1] or '0',0)))
        if not choices:
            if ins[n].address!=0x62234a:
                raise ValueError('unrecognized member naming call: '+str([(hex(i.address),i.mnemonic,i.op_str) for i in ins[n-12:n+1]]))
            continue
        if len(choices)!=1:raise ValueError('ambiguous member store')
        k,offset=choices[0]
        if any(i.mnemonic not in ('mov','lea','call') for i in ins[k:n+1]):
            raise ValueError('branch or arithmetic in member naming slice')
        if [i.op_str for i in ins[k:n+1] if i.mnemonic=='call']!=['0x142ee50','0x1532bc0']:
            raise ValueError('unexpected member naming call chain')
        add(SETUP,'setupUi.member.{:x}'.format(offset),ins[k].address,ins[n].address+ins[n].size,
            {'rbp':'synthetic Ui_intel_ctl_left2','r12':'synthetic QString storage',
             'widget_register':target,'member_offset':offset})
        recognized.append(offset)
    if len(name_calls)!=158 or len(recognized)!=157 or len(set(recognized))!=157:
        raise ValueError('changed UI member coverage')
    fixture['literals']=[{'address':a,'text':t} for a,t in sorted(literals.items())]
    report={'schema':1,'source_elf_sha256':SOURCE_SHA,'fixture_sha256':hashlib.sha256(encoded(fixture)).hexdigest(),
            'scope':fixture['scope'],'fixture_code_bytes':sum(f['size'] for f in fixture['functions']),
            'setup_name_calls':158,'member_name_slices':157,'excluded_parent_name_call':0x62234a,
            'proofs':proofs}
    return report,fixture


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--export-fixture',type=Path,required=True)
    args=parser.parse_args()
    report,fixture=investigate(args.binary)
    for path,value in ((args.output,report),(args.export_fixture,fixture)):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(encoded(value))
    print('157 member slices; {} code bytes; SHA {}'.format(report['fixture_code_bytes'],report['fixture_sha256']))


if __name__=='__main__':main()
