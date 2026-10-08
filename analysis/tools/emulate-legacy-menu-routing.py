#!/usr/bin/env python3
"""Original menu decisions, synthetic PCI/DMI and empty panel boundaries only."""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import platform
import re

import unicorn

spec=importlib.util.spec_from_file_location('dispatch',Path(__file__).with_name('emulate-legacy-dispatch.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-menu-routing.json'
FIXTURE_SHA='3f937b9bd8f5cd2d6fc4fed40227ee7a26924134d4f258648e71c121e92f9719'
ADL='_ZN10MainWindow25on_actionADL_MB_triggeredEv'
TIMINGS='_ZN10MainWindow33on_actionMemory_Timings_triggeredEv'
AMD='_ZN10MainWindow27on_actionAM5_MB_2_triggeredEv'
W790='_ZN10MainWindow26on_actionW790_MB_triggeredEv'
PANELS={'adl_mb','arl_mb','nvl_mb','nvl_mb2','nvl_tuners','gnr_memtime','arl_memtime','nvl_memtime',
        'intel_memtime','adl_timings','tr5_mb2','tr5_mb','am5_vrm_module','am5_mb3','am5_mb4',
        'w790_mb','w890_mb','w790_vrm_module','w890_vrm_module'}


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:raise ValueError('menu fixture hash mismatch')
    value=json.loads(data)
    if value['source_elf_sha256']!=base.SOURCE_SHA or value['schema']!=1:raise ValueError('menu source/schema mismatch')
    common=base.load_fixture()
    # Reuse the already pinned original PCI predicates. Do not replace their
    # branch logic with claimed CPU models or booleans in the external stubs.
    for key in ('functions','literals'):value[key].extend(common[key])
    for name,row in common['objects'].items():
        if name in value['objects']:
            if value['objects'][name]['address']!=row['address']:raise ValueError('global address mismatch')
        else:value['objects'][name]=row
    return value


class MenuMachine(base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.uc.mem_write(self.objects['GLOBAL_IS_SHIMADA'],bytes([bool(inputs.get('shimada'))]))
        self.panels=[];self.widgets={};self.qstrings={}

    def external(self,name):
        match=re.fullmatch(r'_ZN(\d+)(\w+)C[12]EP7QWidget',name)
        if match and match[2] in PANELS and len(match[2])==int(match[1]):
            self.calls.append(name)
            if self.reg('rsi') or self.reg('rdi') not in self.allocations:raise ValueError('panel args changed')
            row={'panel':match[2],'allocation':self.reg('rdi'),'allocation_size':self.allocations[self.reg('rdi')],
                 'call_site':self.u64(self.reg('rsp'))-5,'constructor_body_executed':False}
            self.panels.append(row);self.widgets[self.reg('rdi')]=row
            self.ret();return
        if name=='_Z18FindPciDeviceById2jjj':
            self.calls.append(name)
            if [self.reg('rdi'),self.reg('rsi'),self.reg('rdx')]!=[0x1022,0x14a4,0]:raise ValueError('TR5 PCI args changed')
            self.events.append({'pci_find_sysv':[0x1022,0x14a4,0]})
            self.ret(self.inputs.get('tr5_bdf',-1));return
        if name in ('_ZN7QWidget4showEv','_ZN7QWidget12setAttributeEN2Qt15WidgetAttributeEb',
                    '_ZN7QWidget14setWindowTitleERK7QString'):
            self.calls.append(name);row=self.widgets[self.reg('rdi')]
            if name=='_ZN7QWidget4showEv':
                row['show_calls']=row.get('show_calls',0)+1
                if row['panel']=='am5_vrm_module':row['member_0x38_at_show']=self.uc.mem_read(self.reg('rdi')+0x38,1)[0]
            elif name=='_ZN7QWidget14setWindowTitleERK7QString':
                row['window_title']=self.qstrings[self.u64(self.reg('rsi'))]
            else:row.setdefault('attributes',[]).append([self.reg('rsi'),self.reg('rdx')])
            self.ret();return
        super().external(name)
        if name=='_ZN7QString16fromAscii_helperEPKci':self.qstrings[self.reg('rax')]=self.qt_texts[-1]


def investigate(path=FIXTURE):
    fixture=load_fixture(path);rows=[]

    def run(symbol,inputs,expected,warning=False):
        m=MenuMachine(fixture,inputs);r=m.run(symbol)
        observed=[p['panel'] for p in m.panels]
        if r['outcome']!='returned' or observed!=expected:raise AssertionError((symbol,inputs,expected,observed,r))
        if ('Not Supported!' in r['qt_texts'])!=warning:raise AssertionError('warning branch differs')
        if any(p.get('show_calls')!=1 for p in m.panels):raise AssertionError('show boundary missing')
        for p in m.panels:
            if p.get('attributes')!=[[0x4c,1],[0x37,1]]:raise AssertionError('panel attributes differ')
        if 'am5_vrm_module' in observed:
            vrm=[p for p in m.panels if p['panel']=='am5_vrm_module']
            if [p.get('member_0x38_at_show') for p in vrm]!=[0x84,0x90]:raise AssertionError('TR5 member values differ')
            if [p.get('window_title') for p in vrm]!=['TR5 VCore0 Tuners','TR5 VCore1 Tuners']:raise AssertionError('TR5 titles differ')
        rows.append({'symbol':symbol,'inputs':inputs,'panels':m.panels,**r})

    # W790 action does not consult DMI or require Intel vendor after the AMD
    # rejection. Cached GNR flag alone chooses W790 vs W890 panel pairs.
    for vendor,gnr,nvl,arl,board in itertools.product((0x8086,0x1022,0x1002,0x1af4,0xffff),
                                                    (False,True),(False,True),(False,True),
                                                    ('','W790 ACE','Pro WS W890E-SAGE SE')):
        amd=vendor in (0x1022,0x1002)
        expected=[] if amd else ['w890_mb','w890_vrm_module'] if gnr else ['w790_mb','w790_vrm_module']
        run(W790,dict(vendor=vendor,gnr=gnr,nvl=nvl,arl=arl,board=board),expected,amd)
        if rows[-1]['searches']:raise AssertionError('W790 action unexpectedly read DMI')
    boards=('','TRX50','Pro WS TRX50-SAGE WIFI','WRX90E','WRX90E TRX50','trx50','wrx90e',
            '850 AYW OC','AYW 850','850 OC','AYW OC','850 ayw OC','850 AYW oc','prefix TRX50 suffix')
    for vendor,board,tr5,shimada in itertools.product((0x8086,0x1022,0x1002,0xffff),boards,(-1,0,0x100),(False,True)):
        amd=vendor in (0x1022,0x1002)
        if not amd:expected=[]
        elif 'WRX90E' in board or 'TRX50' in board:expected=['tr5_mb2']
        elif tr5!=-1 or shimada:expected=['tr5_mb','am5_vrm_module','am5_vrm_module']
        elif all(s in board for s in ('850','AYW','OC')):expected=['am5_mb4']
        else:expected=['am5_mb3']
        run(AMD,dict(vendor=vendor,board=board,tr5_bdf=tr5,shimada=shimada),expected,not amd)
    for nvl,arl,vendor,board in itertools.product((False,True),(False,True),(0x8086,0x1022),
                                                  ('','VZEDC','prefix VZEDC suffix','vzedc','W790 ACE')):
        expected=(['nvl_mb2'] if 'VZEDC' in board else ['nvl_mb','nvl_tuners']) if nvl else ['arl_mb'] if arl else ['adl_mb']
        run(ADL,dict(nvl=nvl,arl=arl,vendor=vendor,board=board),expected)
    for nvl,arl,gnr,rkl,vendor,device in itertools.product((False,True),(False,True),(False,True),
                                                         (False,True),(0x8086,0x1022),(0,0x4648,0xa700)):
        adl=vendor==0x8086 and device in (0x4648,0xa700)
        if nvl:expected=['nvl_memtime']
        elif arl:expected=['arl_memtime']
        elif gnr:expected=['gnr_memtime']
        else:expected=(['intel_memtime'] if rkl or adl else [])+(['adl_timings'] if adl else [])
        run(TIMINGS,dict(nvl=nvl,arl=arl,gnr=gnr,vendor=vendor,device=device,rkl_matches={0x4c43:0} if rkl else {}),
            expected,not expected)
    return {'schema':1,'source_elf_sha256':base.SOURCE_SHA,'fixture_sha256':FIXTURE_SHA,
            'reused_dispatch_fixture_sha256':base.FIXTURE_SHA,
            'scope':'Original menu/PCI predicate instructions; synthetic DMI, PCI, flags, Qt and no-op panel constructors; no real windows or hardware',
            'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'cases_executed':len(rows),'observations':rows}


def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--fixture',type=Path,default=FIXTURE)
    p.add_argument('--output',type=Path,required=True);args=p.parse_args();result=investigate(args.fixture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    print('{} bounded menu scenarios passed; no original panel body or hardware executed'.format(result['cases_executed']))


if __name__=='__main__':main()
