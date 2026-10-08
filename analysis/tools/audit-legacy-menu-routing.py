#!/usr/bin/env python3
"""Pin original motherboard/timings menu decisions; no hardware execution."""
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
FUNCTIONS=(
    '_ZN10MainWindow25on_actionADL_MB_triggeredEv',
    '_ZN10MainWindow33on_actionMemory_Timings_triggeredEv',
    '_ZN10MainWindow27on_actionAM5_MB_2_triggeredEv',
    '_ZN10MainWindow26on_actionW790_MB_triggeredEv',
    '_Z9is_tr5_esv','_Z12is_it_ayw_ocv',
)
GLOBALS=('GLOBAL_IS_NVL','IS_ARL_GLOBAL','GLOBAL_IS_GNR_SP','GLOBAL_IS_SHIMADA')


def encoded(value):return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def investigate(path):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=SOURCE_SHA:raise ValueError('original ELF SHA differs')
    spec=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('elf-runtime-audit.py'))
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
    report=audit.inspect(path,re.compile('^('+'|'.join(map(re.escape,FUNCTIONS))+')$'),True)
    report.pop('imports')
    elf=ELFFile(io.BytesIO(data))
    symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}

    def read(address,size):
        for segment in elf.iter_segments():
            rel=address-segment['p_vaddr']
            if segment['p_type']=='PT_LOAD' and 0<=rel and rel+size<=segment['p_filesz']:
                return data[segment['p_offset']+rel:segment['p_offset']+rel+size]
        raise ValueError('address outside file-backed ELF')

    fixture={'schema':1,'source_elf_sha256':SOURCE_SHA,
             'scope':'Four original menu slots and two predicates; DMI/PCI/Qt/panel construction remain synthetic',
             'functions':[],'objects':{},'literals':[]}
    decoder=Cs(CS_ARCH_X86,CS_MODE_64);decoder.detail=True
    global_addresses={symbols[n]['st_value'] for n in GLOBALS}
    literals={}
    for name in FUNCTIONS:
        s=symbols[name];code=read(s['st_value'],s['st_size'])
        f=next(f for f in report['selected_functions'] if name in f['symbols'])
        fixture['functions'].append(dict(symbol=name,address=s['st_value'],size=s['st_size'],
                                        code_hex=code.hex(),code_sha256=hashlib.sha256(code).hexdigest(),calls=f['calls']))
        for ins in decoder.disasm(code,s['st_value']):
            if ins.mnemonic!='lea':continue
            for op in ins.operands:
                if op.type!=X86_OP_MEM or op.mem.base!=X86_REG_RIP:continue
                address=ins.address+ins.size+op.mem.disp
                if address in global_addresses:continue
                text=read(address,512).split(b'\0')[0].decode('utf-8')
                if not text or not all(c.isprintable() or c in '\n\r\t' for c in text):
                    raise ValueError('unexpected RIP data')
                literals[address]=text
    fixture['literals']=[dict(address=a,text=t) for a,t in sorted(literals.items())]
    for name in GLOBALS:
        s=symbols[name]
        fixture['objects'][name]=dict(address=s['st_value'],size=s['st_size'],emulator_initialization='synthetic cached flag')
    report.update(fixture_sha256=hashlib.sha256(encoded(fixture)).hexdigest(),
                  fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                  scope=fixture['scope'])
    return report,fixture


def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('binary',type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--export-fixture',type=Path,required=True)
    args=p.parse_args();report,fixture=investigate(args.binary)
    for path,value in ((args.output,report),(args.export_fixture,fixture)):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(encoded(value))
    print('6 functions; {} original bytes; SHA {}'.format(report['fixture_code_bytes'],report['fixture_sha256']))


if __name__=='__main__':main()
