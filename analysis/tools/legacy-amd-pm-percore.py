#!/usr/bin/env python3
"""Trace original core/CCD storage and audit ranges, without guessing a layout."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct

spec=importlib.util.spec_from_file_location('percore_limits',Path(__file__).with_name('legacy-amd-pm-limits.py'))
limits=importlib.util.module_from_spec(spec);spec.loader.exec_module(limits)
LENGTHS=(2844,4100,5356,6612,7868,9124,10380,11636)
CORE_LABELS=('Power','Voltage','Temp','Freq Ghz','Eff Freq','C0 Residency','CC1 Residency',
             'CC6 Residency','OS PState','CPPC Max Freq','CPPC Min Freq','EDC CAC','UOPs','Mem Band','Core Bound')
CCD_LABELS=('Logic Power','VDDM Power','VDDM Voltage','Temp','Freq Ghz','Eff Freq',
            'CCA Limit','CCA CAC','CCA Residency','EDC CAC','FLL BTC')


class Machine(limits.Machine):
    def __init__(self,*args):
        super().__init__(*args);self.max_steps=120000;self.core_end=None

    def hook(self,uc,address,size,context):
        if self.phase=='core_ccd' and address==0x2ea4a8:
            uc.emu_stop();return
        if self.phase=='core_ccd' and address==0x2e7da6:self.core_end=len(self.reads)
        if address==0x14c6b0:self.ret(self.alloc(self.reg('rdi')));return
        if address==0x14a460:
            p,s,n=self.reg('rdi'),self.reg('rsi'),self.reg('rdx');assert n<=65536
            uc.mem_write(p,bytes(uc.mem_read(s,n)));self.ret(p);return
        if address==0x2902f0:
            p,s=self.reg('rdi'),self.reg('rsi')
            assert p in (limits.OBJ+0x928,limits.OBJ+0x940,limits.OBJ+0x958)
            assert self.u64(p)==self.u64(p+8)==s;self.ret();return
        if self.phase=='core_ccd' and address==0x28cb30:
            p,s=self.reg('rdi'),self.reg('rsi');assert p==limits.OBJ+0x940
            begin,last=self.u64(s),self.u64(s+8);assert 0<=last-begin<=4096
            data=bytes(uc.mem_read(begin,last-begin));dest=self.alloc(len(data));uc.mem_write(dest,data)
            row=self.u64(p+8);assert row+24<=self.u64(p+16)
            for off,v in ((0,dest),(8,dest+len(data)),(16,dest+len(data))):self.p64(row+off,v)
            self.p64(p+8,row+24);self.ret();return
        super().hook(uc,address,size,context)

    def trace(self):
        self.run();start=len(self.reads);self.phase='core_ccd';self.steps=0
        for member in (0x928,0x940):
            storage=self.alloc(32*24)
            for off,v in ((0,storage),(8,storage),(16,storage+32*24)):self.p64(limits.OBJ+member+off,v)
        self.reg('rsp',limits.STACK+0xfff8);self.p64(limits.STACK+0xfff8,limits.STOP)
        self.reg('rdi',limits.OBJ);self.reg('rsi',4)
        self.uc.emu_start(0x2e7360,limits.STOP+1)
        assert self.reg('rip')==0x2ea4a8 and self.core_end is not None
        groups=[];length=LENGTHS[limits.VERSIONS.index(self.version)]
        for name,names_member,values_member,labels,reads in (
            ('core',0x910,0x928,CORE_LABELS,self.reads[start:self.core_end]),
            ('ccd',0x970,0x940,CCD_LABELS,self.reads[self.core_end:])):
            first,last=self.u64(limits.OBJ+names_member),self.u64(limits.OBJ+names_member+8)
            assert tuple(self.cppstr(p) for p in range(first,last,32))==labels
            rows=[];index=0
            for row in range(self.u64(limits.OBJ+values_member),self.u64(limits.OBJ+values_member+8),24):
                begin,end=self.u64(row),self.u64(row+8);assert (end-begin)%4==0
                words=list(struct.unpack('<'+str((end-begin)//4)+'I',self.uc.mem_read(begin,end-begin)))
                offsets=reads[index:index+len(words)];assert len(offsets)==len(words)
                assert words==[self.bits(p) for p in offsets]
                rows.append(dict(label=labels[len(rows)],offsets=offsets,raw_words=words));index+=len(words)
            assert index==len(reads) and len(rows)==len(labels)
            groups.append(dict(name=name,rows=rows,slots=len(rows[0]['offsets']),
                               outside_public_table=[p for p in reads if p+4>length]))
        core=set(p for row in groups[0]['rows'] for p in row['offsets'])
        ccd=set(p for row in groups[1]['rows'] for p in row['offsets'])
        return dict(version=hex(self.version),synthetic_ccds=self.ccds,pattern=self.pattern,
                    public_table_bytes=length,groups=groups,core_ccd_overlap=sorted(core&ccd),
                    common_field_conflicts=sorted(core&{0x5cc,0x714,0x71c}),instructions=self.steps)


def investigate():
    fixture=limits.load_fixture()
    rows=[Machine(fixture,v,c,p).trace() for v in limits.VERSIONS for c in (1,2,4,6,8,10,12,14,16) for p in (0,1)]
    return dict(schema=1,source_sha256=limits.SOURCE_SHA,fixture_sha256=limits.FIXTURE_SHA,
                hardware_execution=False,production_decoder_enabled=False,mode=4,cases=rows,
                scope='Original constructor and mode-4 core/CCD loops, with synthetic topology, transport and C++ boundaries; calibration assumed complete. CCD inputs are deliberately not asserted to correspond to the table version. Storage labels/ranges do not prove physical mapping, units, or a valid firmware layout.')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    report=investigate();args.output.write_bytes((json.dumps(report,indent=2)+'\n').encode())
    print(json.dumps(dict(cases=len(report['cases']),range_conflicts=sum(any(g['outside_public_table'] for g in x['groups']) for x in report['cases']),
                          overlapping_groups=sum(bool(x['core_ccd_overlap']) for x in report['cases']))))
