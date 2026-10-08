#!/usr/bin/env python3
"""Replay UI name/translation/connect instructions with synthetic Qt boundaries."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform

import unicorn
from unicorn.x86_const import UC_X86_REG_RBP, UC_X86_REG_R12, UC_X86_REG_R14, UC_X86_REG_R15

spec=importlib.util.spec_from_file_location('dispatch',Path(__file__).with_name('emulate-legacy-dispatch.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-ui-connections.json'
FIXTURE_SHA='21863e0ef92b04b9a6f021fa2c993e597908560788810439629c703ad8203222'
TRANSLATE='_ZN18Ui_intel_ctl_left213retranslateUiEP7QWidget'
CONNECT='intel_ctl6.constructor.connect-region'
XOC=('_ZN10intel_ctl624on_pushButton_14_clickedEv','_ZN10intel_ctl614on_xoc_clickedEv')


def load_fixture(path=FIXTURE):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=FIXTURE_SHA:raise ValueError('UI fixture hash mismatch')
    value=json.loads(data)
    if value['source_elf_sha256']!=base.SOURCE_SHA or value['schema']!=1:
        raise ValueError('UI source/schema mismatch')
    return value


class UiMachine(base.Machine):
    def __init__(self,fixture,inputs):
        super().__init__(fixture,inputs)
        self.functions={f['symbol']:f for f in fixture['functions']}
        self.fields={};self.strings={};self.operations=[]
        self.partial_stop=None

    def cstring(self,address):
        data=bytearray()
        for n in range(512):
            value=bytes(self.uc.mem_read(address+n,1))
            if value==b'\0':return data.decode('utf-8')
            data.extend(value)
        raise ValueError('unterminated UI string')

    def prepare(self,symbol):
        self.output=self.alloc(0x1000)
        self.ui=self.alloc(0x1000)
        self.left=self.alloc(0x100)
        self.put64(self.output+0x30,self.left)
        self.put64(self.left+0x30,self.ui)
        self.fields[self.left]={'object':'left-parent'}
        self.fields[self.output]={'object':'intel_ctl6'}
        for offset in range(0,0x4e8,8):
            widget=self.alloc(64)
            self.put64(self.ui+offset,widget)
            self.fields[widget]={'object':'Ui_intel_ctl_left2 member','offset':offset}
        for offset in (0x58,0x68,0x70,0x78,0x80,0x88,0x2c0,0x2c8):
            widget=self.alloc(64)
            self.put64(self.output+offset,widget)
            self.fields[widget]={'object':'intel_ctl6 member','offset':offset}
        if self.inputs.get('optional_null'):self.put64(self.output+0x78,0)
        self.reg('rdi',self.output)
        if symbol==TRANSLATE:
            self.reg('rdi',self.ui);self.reg('rsi',self.left)
        elif symbol==CONNECT:
            self.uc.reg_write(UC_X86_REG_RBP,self.output)
            self.reg('rsp',base.STACK+0x8000)
        elif symbol.startswith('setupUi.member.'):
            p=self.functions[symbol]['preconditions']
            self.uc.reg_write(UC_X86_REG_RBP,self.ui)
            self.uc.reg_write(UC_X86_REG_R12,self.alloc(32))
            register={'r14':UC_X86_REG_R14,'r15':UC_X86_REG_R15}[p['widget_register']]
            self.uc.reg_write(register,self.u64(self.ui+p['member_offset']))
        elif symbol not in XOC:raise ValueError('unknown UI experiment')
        self.partial_stop=self.functions[symbol].get('stop_address')

    def hook(self,uc,pc,size,unused):
        if pc==self.partial_stop:
            self.steps+=1;self.stop('slice-end')
        else:super().hook(uc,pc,size,unused)

    def qstring(self,output,text):
        descriptor=self.alloc(32)
        self.put32(descriptor,self.inputs.get('refcount',-1))
        self.put32(descriptor+4,len(text))
        self.strings[descriptor]=text
        self.put64(output,descriptor)
        return output

    def external(self,name):
        self.calls.append(name);value=0
        site=self.u64(self.reg('rsp'))-5
        if name=='_ZN16QCoreApplication9translateEPKcS1_S1_i':
            if self.cstring(self.reg('rsi'))!='intel_ctl_left2' or self.reg('rcx') or self.reg('r8')!=0xffffffff:
                raise ValueError('translation arguments changed')
            value=self.qstring(self.reg('rdi'),self.cstring(self.reg('rdx')))
        elif name=='_ZN7QString15fromUtf8_helperEPKci':
            text=bytes(self.uc.mem_read(self.reg('rsi'),self.reg('rdx'))).decode('utf-8')
            if len(text)>512:raise ValueError('oversized QString')
            value=self.qstring(self.reg('rdi'),text)
        elif name in ('_ZN7QObject13setObjectNameERK7QString','_ZN15QAbstractButton7setTextERK7QString',
                      '_ZN6QLabel7setTextERK7QString','_ZN7QWidget14setWindowTitleERK7QString'):
            field=self.fields[self.reg('rdi')]
            text=self.strings[self.u64(self.reg('rsi'))]
            self.operations.append({'call_site':site,'operation':name,'field':field,'text':text})
        elif name=='_ZN7QObject7connectEPKS_PKcS1_S3_N2Qt14ConnectionTypeE':
            if self.reg('rcx')!=self.output or self.reg('r9')!=0:raise ValueError('connect receiver/type changed')
            self.operations.append({'call_site':site,'operation':'connect','sender':self.fields[self.reg('rsi')],
                                    'signal':self.cstring(self.reg('rdx')),'slot':self.cstring(self.reg('r8')),
                                    'connection_type':self.reg('r9')})
            self.put64(self.reg('rdi'),0)
            value=self.reg('rdi')
        elif name in ('_ZN11QMetaObject10ConnectionD1Ev','_ZN11QMetaObject10ConnectionD2Ev'):
            if self.u64(self.reg('rdi'))!=0:raise ValueError('unexpected connection descriptor')
        elif name=='_ZN10QArrayData10deallocateEPS_mm':
            descriptor=self.reg('rdi')
            if descriptor not in self.strings or descriptor in self.deleted:raise ValueError('bad QString free')
            self.deleted.append(descriptor)
        elif name in ('_ZN7nvl_xocC1EP7QWidget','_ZN7nvl_xocC2EP7QWidget'):
            if self.reg('rsi')!=0 or self.allocations.get(self.reg('rdi'))!=0x370:
                raise ValueError('XOC constructor arguments changed')
            self.operations.append({'call_site':site,'operation':'nvl_xoc-constructor-boundary',
                                    'allocation':self.reg('rdi'),'parent':0})
        elif name=='_ZN7QWidget4showEv':
            self.operations.append({'operation':'show-boundary','allocation':self.reg('rdi')})
        elif name=='_ZN7QWidget12setAttributeEN2Qt15WidgetAttributeEb':
            self.operations.append({'operation':'setAttribute-boundary','allocation':self.reg('rdi'),
                                    'attribute':self.reg('rsi'),'value':self.reg('rdx')})
        else:
            self.calls.pop();return super().external(name)
        self.ret(value)


def investigate(path=FIXTURE):
    fixture=load_fixture(path)
    names={};names_evidence=[];runs=[]
    for f in fixture['functions']:
        if not f['symbol'].startswith('setupUi.member.'):continue
        m=UiMachine(fixture,{})
        r=m.run(f['symbol'])
        if r['outcome']!='slice-end' or len(m.operations)!=1:raise AssertionError('member slice escaped')
        op=m.operations[0]
        offset=f['preconditions']['member_offset']
        if op['field']!={'object':'Ui_intel_ctl_left2 member','offset':offset}:raise AssertionError('member binding differs')
        names[offset]=op['text'];names_evidence.append({'symbol':f['symbol'],**op})
        runs.append({'symbol':f['symbol'],'steps':r['steps'],'outcome':r['outcome']})
    if len(names)!=157:raise AssertionError('member count changed')
    translations=[]
    for refcount in (-1,1):
        m=UiMachine(fixture,{'refcount':refcount});r=m.run(TRANSLATE)
        if r['outcome']!='returned' or len(m.operations)!=67 or len(m.deleted)!=(67 if refcount==1 else 0):
            raise AssertionError('translation/cleanup mismatch')
        translations.append(m.operations)
        runs.append({'symbol':TRANSLATE,'refcount':refcount,'frees':len(m.deleted),'steps':r['steps'],'outcome':r['outcome']})
    if translations[0]!=translations[1]:raise AssertionError('translation depends on synthetic QString ownership')
    texts={op['field']['offset']:op['text'] for op in translations[0] if 'offset' in op['field']}
    connections=[]
    for optional_null in (False,True):
        m=UiMachine(fixture,{'optional_null':optional_null});r=m.run(CONNECT)
        rows=m.operations
        if r['outcome']!='slice-end' or len(rows)!=(27 if optional_null else 28):
            raise AssertionError(('connection count/escape',optional_null,r,len(rows)))
        for row in rows:
            if row['sender']['object']=='Ui_intel_ctl_left2 member':
                offset=row['sender']['offset']
                row.update(object_name=names[offset],source_text=texts.get(offset))
        connections.append({'optional_null':optional_null,'rows':rows})
        runs.append({'symbol':CONNECT,'optional_null':optional_null,'steps':r['steps'],'outcome':r['outcome']})
    xoc=[]
    for symbol in XOC:
        m=UiMachine(fixture,{});r=m.run(symbol)
        if r['outcome']!='returned' or len([x for x in m.operations if x['operation']=='nvl_xoc-constructor-boundary'])!=1:
            raise AssertionError('XOC slot did not reach expected boundaries')
        xoc.append({'symbol':symbol,'operations':m.operations})
        runs.append({'symbol':symbol,'steps':r['steps'],'outcome':r['outcome']})
    # Independent Qt metadata + original router evidence from the previous gate.
    qs=importlib.util.spec_from_file_location('qt_callbacks',Path(__file__).with_name('qt-callback-map.py'))
    qt=importlib.util.module_from_spec(qs);qs.loader.exec_module(qt)
    cls=next(c for c in qt.load_fixture(FIXTURE.with_name('legacy-qt-callbacks.json'))['classes'] if c['class']=='intel_ctl6')
    for collection in connections:
        for row in collection['rows']:
            method=row['slot'][1:].split('(')[0]
            matches=[m for m in cls['methods'] if m['method']==method]
            if len(matches)!=1:raise AssertionError('connected slot missing from metadata')
            routed=qt.route(cls,matches[0]['index'])
            if routed['outcome']!='callback-boundary':raise AssertionError('connected slot failed router')
            row['method_index']=matches[0]['index'];row['slot_target']=routed['target'];row['slot_symbols']=routed['symbols']
    return {'schema':1,'source_elf_sha256':base.SOURCE_SHA,'fixture_sha256':FIXTURE_SHA,
            'scope':'Explicitly conditioned original UI slices with synthetic objects/Qt; no full constructor, real signal, window or hardware',
            'environment':dict(system=platform.system(),python=platform.python_version(),unicorn=unicorn.__version__),
            'cases_executed':len(runs),'runs':runs,'member_names':names_evidence,
            'source_text_assignments':translations[0],'connections':connections,'xoc_slots':xoc}


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=investigate(args.fixture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    print('{} bounded UI experiments; 157 names, 67 translations, 28/27 conditional connections; no hardware'.format(result['cases_executed']))


if __name__=='__main__':main()
