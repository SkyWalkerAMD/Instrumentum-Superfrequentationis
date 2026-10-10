#!/usr/bin/env python3
"""Original NVL timing/CPU subobjects and portable descriptor evidence."""
import argparse
from collections import Counter
import copy
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import platform
import struct

import unicorn

spec=importlib.util.spec_from_file_location('components_apply',Path(__file__).with_name('legacy-nvl-apply.py'))
apply=importlib.util.module_from_spec(spec);spec.loader.exec_module(apply)
profile,cores,base=apply.profile,apply.cores,apply.base
TIMING='_ZN22NVL_MEMTWEAKIT_Timings'
NOVA='_ZN13nova_lake_cpu'
TIMING_CTOR=TIMING+'C2EP7RW_MMIO'
TIMING_DTOR=TIMING+'D2Ev'
GET_REG=TIMING+'7get_regEi'
MEM2=TIMING+'15getval_frommem2Ehhim'
NOVA_CTOR=NOVA+'C2EP7RW_MMIO'
NOVA_METHODS=('18get_sa_perf_statusEv','13get_ipu_ratioEv','11get_sav_vidEv','17get_sa_pll_statusEv',
              '14bandgap_bypassEv','20get_memss_qclk_ratioEv','12get_geardownEv','13get_dll_bw_cbERiS0_S0_',
              '12GetLvrTargetEijj','24get_fused_uncore_voltageERSt6vectorIiSaIiEEj','13get_lvr_railsERSt6vectorIbSaIbEE')
HELPERS=(TIMING_CTOR,TIMING_DTOR,GET_REG,MEM2,NOVA_CTOR)+tuple(NOVA+s for s in NOVA_METHODS)+(
    '_ZN7RW_MMIO9Rd_MMIO64Em',
    '_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEC2IS3_EEPKcRKS3_.constprop.0',
    '_ZNSt12_Vector_baseIjSaIjEED2Ev')
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/legacy-nvl-components.json'
FIXTURE_SHA='061086b4f49168c560538572d541f006ad38c3ceb9ef7b140518f9c3078102b5'
RAIL_REGISTERS=(0x4148,0x4158,0x4110,0x4610,0xa030,0x4930,0x4100,0x492c,0x4928,0x4890)
FUSED=NOVA+'24get_fused_uncore_voltageERSt6vectorIiSaIiEEj'
RAILS=NOVA+'13get_lvr_railsERSt6vectorIbSaIbEE'
MSR_MODES={'success','busy-forever','read-error','read-eof','short-read-4','short-read-7',
           'open-error','seek-error','write-error','short-write','close-error'}
MMIO_MODES={'success','no-completion','encoded-error','write-error-but-done'}


def extract(path):
    previous=apply.HELPERS
    try:
        apply.HELPERS=HELPERS
        result=apply.extract(path,helper_addresses={HELPERS[-2]:0x22eeb0})
    finally:apply.HELPERS=previous
    result['apply_fixture_sha256']=apply.FIXTURE_SHA
    result['scope']='Complete original timing/CPU subconstructors, timing destruction, descriptor getter, bounded MSR query and eleven nova_lake_cpu methods. Original vector/string constructors and growth, MSR wrappers, RW_MMIO member reads and 32-bit mailbox requests. Synthetic libc, PCI lookup, MMIO mailbox/64-bit read leaf and MSR results. Timing stream_regs3/sagv_timings and full NVL configuration constructor are not executed.'
    return result


def load_fixture(path=FIXTURE):
    raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=FIXTURE_SHA:raise ValueError('NVL components fixture hash mismatch')
    value=json.loads(raw)
    if value['source_elf_sha256']!=base.SOURCE_SHA or value['apply_fixture_sha256']!=apply.FIXTURE_SHA:
        raise ValueError('NVL components dependency mismatch')
    return value


class ComponentMachine(apply.ApplyMachine):
    def __init__(self,fixture,inputs):
        if inputs.get('io_mode','success') not in MSR_MODES:raise ValueError('unsupported MSR mode')
        if inputs.get('mmio_mode','success') not in MMIO_MODES:raise ValueError('unsupported MMIO mode')
        super().__init__(apply.load_fixture(),copy.deepcopy(inputs),extra_fixtures=(fixture,))
        self.component_entries=[];self.component_reads=[];self.pci_queries=[];self.frees=[]
        self.component_addresses={self.entries[n]:n for n in HELPERS}
        self.vector_constructors={f['address']:f['symbol'] for f in fixture['functions']
                                  if f['symbol'].startswith('_ZNSt6vector') and 'initializer_list' in f['symbol']}
        self.built_vectors={}
        self.read_counts=Counter();self.subcommand_reads=Counter()

    def prepare(self,symbol):
        super().prepare(profile.LOAD)
        self.phase='components';self.entry=symbol
        self.rw=self.alloc(0x40);self.put64(self.rw+0x18,self.inputs.get('base',0x123450000))
        self.timing_object=self.alloc(0x400);self.nova_object=self.alloc(0x300)
        self.uc.mem_write(self.timing_object,bytes([self.inputs.get('object_seed',0xa5)])*0x400)
        self.uc.mem_write(self.nova_object,bytes([self.inputs.get('object_seed',0xa5)])*0x300)
        self.outputs=self.alloc(64)
        if symbol in (TIMING_CTOR,NOVA_CTOR):
            self.reg('rdi',self.timing_object if symbol==TIMING_CTOR else self.nova_object)
            self.reg('rsi',self.rw)
        elif symbol==MEM2:
            self.arguments(symbol)
        else:raise ValueError('construct subobject before invoking its method')

    def arguments(self,symbol):
        p=self.inputs
        self.reg('rdi',self.timing_object if symbol.startswith(TIMING) else self.nova_object)
        if symbol==GET_REG:self.reg('rsi',p.get('index',0))
        elif symbol==MEM2:
            self.reg('rsi',p.get('byte1',1));self.reg('rdx',p.get('byte2',2))
            self.reg('rcx',p.get('shift',0));self.reg('r8',p.get('mask',0xffffffff))
        elif symbol==NOVA+'13get_dll_bw_cbERiS0_S0_':
            self.reg('rsi',self.outputs);self.reg('rdx',self.outputs+4);self.reg('rcx',self.outputs+8)
        elif symbol==NOVA+'12GetLvrTargetEijj':
            self.reg('rsi',p.get('unused',0));self.reg('rdx',p.get('raw',512));self.reg('rcx',p.get('rail',0))
        elif symbol==FUSED:
            self.put_vector(self.outputs+24,p.get('previous_selected',[77]))
            self.reg('rdi',self.outputs);self.reg('rsi',self.nova_object);self.reg('rdx',self.outputs+24)
            self.reg('rcx',p.get('selector',3200))
        elif symbol==RAILS:
            if not p.get('reuse_rail_flags',False) or not hasattr(self,'rail_bits'):
                self.rail_bits=self.alloc(40)
                flags=p.get('previous_flags',[])
                if flags:
                    capacity=(len(flags)+63)//64*8
                    data=self.alloc(capacity)
                    self.uc.mem_write(data,sum(int(bool(v))<<i for i,v in enumerate(flags)).to_bytes(capacity,'little'))
                    self.put64(self.rail_bits,data);self.put64(self.rail_bits+8,0)
                    self.put64(self.rail_bits+16,data+len(flags)//64*8)
                    self.put64(self.rail_bits+24,len(flags)%64);self.put64(self.rail_bits+32,data+capacity)
            self.reg('rdi',self.outputs);self.reg('rsi',self.nova_object);self.reg('rdx',self.rail_bits)

    def invoke(self,symbol,inputs=None,budget=100000):
        if inputs:self.inputs.update(inputs)
        if self.inputs.get('io_mode','success') not in MSR_MODES:raise ValueError('unsupported MSR mode')
        if self.inputs.get('mmio_mode','success') not in MMIO_MODES:raise ValueError('unsupported MMIO mode')
        self.entry=symbol;self.outcome=None;self.arguments(symbol)
        self.reg('rsp',base.STACK+0xfff8);self.put64(self.reg('rsp'),base.STOP)
        before=self.steps
        self.uc.emu_start(self.entries[symbol],base.STOP+1,timeout=15000000,count=budget)
        if self.outcome is None:
            if self.steps-before!=budget:raise ValueError('unexpected component timeout')
            self.outcome='instruction-limit'
        return dict(outcome=self.outcome,steps=self.steps-before,return_int=self.reg('rax')&0xffffffff,
                    return_u64=self.reg('rax'))

    def hook(self,uc,pc,size,unused):
        if pc in self.component_addresses:self.component_entries.append(self.component_addresses[pc])
        if pc in self.vector_constructors and self.timing_object<=self.reg('rdi')<self.timing_object+0x400:
            self.built_vectors[self.reg('rdi')-self.timing_object]=self.vector_constructors[pc]
        return super().hook(uc,pc,size,unused)

    def mmio_reply(self,address,width):
        relative=address-self.u64(self.rw+0x18)
        table=self.inputs.get('mmio',{})
        value=table.get(hex(address),table.get(hex(relative),self.inputs.get('mmio_default',0)))
        ordinal=self.read_counts[address];self.read_counts[address]+=1
        if isinstance(value,list):
            if not value:raise ValueError('empty MMIO reply sequence')
            value=value[min(ordinal,len(value)-1)]
        value &= (1<<(width*8))-1
        self.component_reads.append(dict(address=address,relative=relative,width=width,reply=value))
        return value

    def external(self,name):
        if name=='strlen@plt':
            self.ret(len(self.cstring(self.reg('rdi')).encode('utf-8')));return
        if name=='_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE9_M_createERmm@plt':
            self.ret(self.alloc(self.u64(self.reg('rsi'))+1));return
        if name=='_ZdlPvm@plt':
            ptr=self.reg('rdi')
            if ptr not in self.allocations or ptr in self.deleted:raise ValueError('invalid component deallocation')
            self.deleted.append(ptr);self.frees.append(dict(pointer=ptr,size=self.reg('rsi')));self.ret();return
        if name=='_Z22FindPciDeviceById_realtth':
            request=[self.reg('rcx'),self.reg('rdx'),self.reg('r8')]
            if request!=[0x8086,0x461d,0]:raise ValueError('unexpected NVL PCI lookup')
            self.pci_queries.append(request);self.ret(self.inputs.get('pci_bdf',-1));return
        if name=='_Z11Read_MMIO64m':
            self.ret(self.mmio_reply(self.reg('rcx'),8));return
        if name=='write@plt' and self.reg('rdi')==900:
            if self.reg('rdx')!=96:raise ValueError('unexpected MMIO request size')
            raw=bytes(self.uc.mem_read(self.reg('rsi'),96))
            opcode,token,address=struct.unpack('<12Q',raw)[:3]
            if opcode!=12 or token!=71:raise ValueError('unexpected component MMIO operation')
            reply=self.mmio_reply(address,4)
            mode=self.inputs.get('mmio_mode','success')
            if mode not in MMIO_MODES:raise ValueError('unsupported MMIO mode')
            done=0 if mode=='no-completion' else (0xfffffff400000001 if mode=='encoded-error' else 1)
            self.put64(self.mail,done);self.put64(self.mail+8,reply)
            result=-1 if mode=='write-error-but-done' else 96
            self.mail_io.append(dict(opcode=opcode,token=token,address=address,reply=reply,done=done,result=result))
            self.ret(result);return
        if name=='read@plt' and self.requested_msr==0x150:
            command=self.last_high&0xffffffff
            ordinal=self.subcommand_reads[command];self.subcommand_reads[command]+=1
            low=self.inputs.get('msr_values',{}).get(hex(command),self.inputs.get('reply_low',0))
            high=self.inputs.get('reply_high',0)
            if ordinal<self.inputs.get('busy_reads',0):high |= 0x80000000
            before=self.inputs.get('reply_low',0),self.inputs.get('reply_high',0)
            self.inputs['reply_low'],self.inputs['reply_high']=low,high
            try:return cores.CoreMachine.external(self,name)
            finally:self.inputs['reply_low'],self.inputs['reply_high']=before
        return super().external(name)

    def flat(self,address,width=8):
        first,last=self.u64(address),self.u64(address+8)
        if not 0<=last-first<=16384 or (last-first)%width:raise ValueError('invalid component vector')
        raw=bytes(self.uc.mem_read(first,last-first)) if last>first else b''
        return [int.from_bytes(raw[i:i+width],'little') for i in range(0,len(raw),width)]

    def strings(self,address):
        first,last=self.u64(address),self.u64(address+8)
        if not 0<=last-first<=8192 or (last-first)%32:raise ValueError('invalid component string vector')
        return [bytes(self.uc.mem_read(self.u64(p),self.u64(p+8))).decode('utf-8') for p in range(first,last,32)]

    def nested(self,address,width=8):
        first,last=self.u64(address),self.u64(address+8)
        if not 0<=last-first<=16384 or (last-first)%24:raise ValueError('invalid nested component vector')
        return [self.flat(p,width) for p in range(first,last,24)]

    def bools(self,address):
        first,last=self.u64(address),self.u64(address+16)
        first_bit,last_bit=self.u64(address+8),self.u64(address+24)
        count=(last-first)*8+last_bit-first_bit
        if not 0<=count<=8192 or first_bit>=64 or last_bit>=64:raise ValueError('invalid bool vector')
        size=(first_bit+count+7)//8
        bits=int.from_bytes(self.uc.mem_read(first,size),'little') if size else 0
        return [bool(bits & (1<<(first_bit+i))) for i in range(count)]

    def timing_state(self):
        obj=self.timing_object
        vectors={}
        for offset,name in sorted(self.built_vectors.items()):
            if name.startswith('_ZNSt6vectorINSt'):value=self.strings(obj+offset)
            elif name.startswith('_ZNSt6vectorIS_Ij'):value=self.nested(obj+offset,4)
            elif name.startswith('_ZNSt6vectorIS_Im'):value=self.nested(obj+offset,8)
            else:value=self.flat(obj+offset,8)
            vectors[hex(offset)]=dict(type=name,values=value)
        return dict(mode=int.from_bytes(self.uc.mem_read(obj,1),'little'),
                    flags=list(self.uc.mem_read(obj+0x1e4,0x16)),
                    base_offsets=list(struct.unpack('<2I',self.uc.mem_read(obj+0x1dc,8))),
                    vectors=vectors,descriptors=self.nested(obj+0x198))

    def nova_state(self):
        obj=self.nova_object
        return dict(status_register=int.from_bytes(self.uc.mem_read(obj,4),'little'),
                    pll_register=int.from_bytes(self.uc.mem_read(obj+4,4),'little'),
                    pci_bdf=int.from_bytes(self.uc.mem_read(obj+0x18,4),'little'),
                    sampled_config=int.from_bytes(self.uc.mem_read(obj+0x1c,4),'little'),
                    flags=list(self.uc.mem_read(obj+0x24,3)),bandgap_base=self.u64(obj+0x28),
                    scales=list(struct.unpack('<4I',self.uc.mem_read(obj+0x64,16))))


def lvr_reference(raw,rail,scale=800):
    numerator,denominator=((1,1),(4,5),(3,5),(2,5))[rail]
    value=(scale*(raw&0xffffffff)+256)>>9
    return (value*denominator+numerator//2)//numerator


def fused_reference(values,selector,scale=800):
    selector &= 0xffffffff
    def convert(raw):return (((raw+256)*scale)&0xffffffff)>>9
    selected=[]
    for a,b,c in (values[:3],values[3:]):
        if selector<=3200:raw=a
        elif selector>=9600:raw=c
        else:
            lo,hi,start=(a,b,3200) if selector<6400 else (b,c,6400)
            raw=(lo+(((hi-lo)*(selector-start))&0xffffffff)//3200)&0xffffffff
        selected.append(convert(raw))
    return [convert(raw) for raw in values],selected


def portable_contract(fixture):
    """Evidence, not a hardware specification or enabled production feature."""
    m=ComponentMachine(fixture,{})
    assert m.run(TIMING_CTOR)['outcome']=='returned'
    timing=m.timing_state()
    n=ComponentMachine(fixture,{})
    assert n.run(NOVA_CTOR)['outcome']=='returned'
    functions=[dict(symbol=f['symbol'],address=f['address'],size=f['size'],sha256=f['code_sha256'])
               for f in fixture['functions'] if f['symbol'] in HELPERS]
    return dict(schema=1,source_elf_sha256=base.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
        status='Recovered legacy arithmetic and tables; no hardware units or production support inferred.',
        evidence=functions,
        timing=dict(descriptor_fields=['relative_register','shift','stored_width','mask'],
                    descriptors=timing['descriptors'],constant_vectors=timing['vectors'],
                    default_observation=dict(inputs={'MMIO replies':0},mode=timing['mode'],
                                             flags=timing['flags'],base_offsets=timing['base_offsets']),
                    register_read=dict(width_bytes=4,address='rw_base + object_u32[0x1dc] + object_u32[0x1e0] + descriptor[0]',
                                       value='(u32(reply) & u32(descriptor[3])) >> (descriptor[1] & 31)',
                                       stored_width_used=False),
                    label_bindings='Unresolved: label collections and descriptors are separate; do not zip by position.'),
        nova=dict(default_observation=n.nova_state(),
                  constructor_queries=[dict(msr=0x150,low=0,high=0x80000205),dict(msr=0x150,low=0,high=0x80000005)],
                  query_deadline='Absent in legacy constructor; new implementation needs a bounded transaction.',
                  methods=dict(sa_perf_status=dict(relative_register=0x5918,width_bytes=8),
                               ipu_ratio='(read64(0x5918) >> 27) & 255',
                               sav_vid='0 if low byte is 0; otherwise ((r & 255) + ((r >> 9) & 255)) * (10 if bit8 else 5)',
                               sa_pll_status=dict(relative_register=0x5848,width_bytes=8),
                               bandgap_bypass=dict(absolute_register=0xfe001f64,value='(read32 >> 1) & 1',
                                                   legacy_effect='Temporarily replaces shared RW_MMIO base; restores only after read returns.'),
                               memss_qclk_ratio='read32(0x12908) & 255',geardown='read32(0xe088) & 1',
                               dll_bw_cb=dict(relative_register=0x2804,shifts=[23,27,21],masks=[15,3,3])),
                  lvr=dict(raw_bits=32,default_scale=800,rail_pairs=[[1,1],[4,5],[3,5],[2,5]],
                           formula='base = (u64(scale) * u32(raw) + 256) >> 9; result = (base * denominator + numerator // 2) // numerator',
                           direct_rail_bounds_checked=False,units='unconfirmed'),
                  fused=dict(relative_registers=[0x40b0,0x40b4],width_bytes=4,raw_shifts=[0,9,18],raw_mask=511,
                             selector_knots=[3200,6400,9600],conversion='u32((raw + 256) * scale) >> 9',
                             interpolation='lo + u32((hi - lo) * (selector - knot)) // 3200, then conversion',
                             effect='Clears the supplied selected vector, returns all six converted endpoints.',units='unconfirmed'),
                  rails=[dict(relative_register=reg,raw_mask=511,rail_shift=9 if i==4 else 10,rail_mask=3,
                              flag_bit=21 if i==4 else 12) for i,reg in enumerate(RAIL_REGISTERS)],
                  rails_flag_effect='Appends ten flags; supplied vector is not cleared.'),
        mem2=dict(msr=0x150,low=0,command='0x8000001e | (u8(byte1) << 8) | (u8(byte2) << 16)',
                  maximum_reads=101,value='(reply_low & u32(mask)) >> (shift & 31)',
                  exhausted='Returns buffer-derived value without a separate timeout status.'),
        porting_requirements=['Separate pure object construction from hardware probing.',
                              'Serialize each complete command/poll/read sequence, not only individual HAL calls.',
                              'Use structured error/unavailable results and deadlines; do not treat a returned legacy value as success.',
                              'Use explicit immutable MMIO bases and typed descriptor bounds.',
                              'Preserve raw widths and evidence; confirm units and label consumers before GUI binding.',
                              'Use checked interpolation for a new backend and retain legacy wraparound only as a counterexample.'],
        unresolved=['Timing stream_regs3/sagv_timings, unit transformations and UI label binding.',
                    'Complete NVL configuration constructor, refresh/Qt effects and remaining core-table consumers.',
                    'Real MMIO/MSR/PCI behavior, platform legality, concurrency and exception unwinding.'])


def investigate(fixture):
    rows=[]

    def record(m,result,label,inputs,start_io=0,start_mmio=0):
        events=m.io[start_io:];reads=[e for e in events if e['op']=='read']
        row=dict(label=label,entry=m.entry,inputs=copy.deepcopy(inputs),**result,
                 mmio=m.component_reads[start_mmio:],writes=[e for e in events if e['op']=='write'],
                 msr_reads=len(reads),msr_read_results=dict(Counter(str(e['result']) for e in reads)),
                 io_sha256=hashlib.sha256(profile.encoded(events)).hexdigest())
        rows.append(row);return row

    def ctor(symbol,inputs,label,expected='returned',budget=100000):
        m=ComponentMachine(fixture,inputs);result=m.run(symbol,instruction_limit=budget)
        assert result['outcome']==expected,(label,inputs,result)
        row=record(m,result,label,inputs)
        if result['outcome']=='returned' and symbol==TIMING_CTOR:
            s=m.timing_state();row['state']={k:s[k] for k in ('mode','flags','base_offsets')}
        elif result['outcome']=='returned' and symbol==NOVA_CTOR:row['state']=m.nova_state()
        return m,row

    def invoke(m,symbol,inputs,label,expected='returned',budget=100000):
        start_io,start_mmio=len(m.io),len(m.component_reads)
        result=m.invoke(symbol,inputs,budget=budget)
        assert result['outcome']==expected,(label,inputs,result)
        return record(m,result,label,inputs,start_io,start_mmio)

    for enabled,ranks,seed in itertools.product(range(4),(0,1,2,15),(0,0xa5)):
        inputs=dict(object_seed=seed,mmio={'0x12904':enabled,'0xd9c0':ranks,'0xd9c4':ranks,
                                        '0x1d9c0':ranks,'0x1d9c4':ranks})
        m,row=ctor(TIMING_CTOR,inputs,'timing-channel-and-rank-construction')
        state=row['state'];flags=state['flags']
        assert flags[0]==bool(enabled&1) and flags[11]==bool(enabled&2)
        assert state['mode']==0
        assert state['base_offsets'][0]==(0 if enabled&1 else 0x10000)
        # The second-controller-only fallback sets flags but leaves +0x800 selected.
        assert state['base_offsets'][1]==(0x800 if enabled==0 or (enabled==2 and ranks==0) else 0)
        for channel in range(2):
            group=flags[channel*11:(channel+1)*11]
            expected=([1,1,1]+[int(bool(ranks&(1<<bit))) for bit in range(4)]*2) if enabled&(1<<channel) else [0]*11
            if enabled&(1<<channel) and not ranks:expected=[1,1,1,1,0,0,0,0,0,0,0]
            assert group==expected,(enabled,ranks,group,expected)
    for registers in ({'0xe088':0x10},{'0xe088':0x20},{'0x1e088':0x10},{'0x1e088':0x20},
                      {'0xe088':[0x10,0]},{'0xe088':[0,0x20]},{'0xe088':8}):
        m,row=ctor(TIMING_CTOR,dict(mmio=registers),'timing-mode-uses-repeated-samples')
        assert row['state']['mode']==(0 if registers=={'0xe088':8} else 1)
    m,row=ctor(TIMING_CTOR,{},'timing-default-constant-tables')
    descriptors=m.timing_state()['descriptors']
    assert len(descriptors)==125
    for index,descriptor in enumerate(descriptors):
        for raw in (0,0xffffffff,0xa596d35b):
            r=invoke(m,GET_REG,dict(index=index,mmio_default=raw),'timing-descriptor-original-read')
            assert r['return_int']==((raw&descriptor[3])>>(descriptor[1]&31))&0xffffffff
            assert r['mmio']==[dict(address=0x123450000+0x10800+descriptor[0],relative=0x10800+descriptor[0],width=4,reply=raw)]
    for index in (-1,125,0x7fffffff):
        r=invoke(m,GET_REG,dict(index=index),'timing-index-boundary','range-error-boundary')
        assert not r['mmio']
    first=m.u64(m.timing_object+0x198);old_end=m.u64(first+8)
    for length in (0,1,2,3):
        m.put64(first+8,m.u64(first)+8*length)
        r=invoke(m,GET_REG,dict(index=0),'timing-short-descriptor','range-error-boundary')
        r['descriptor_length']=length
        assert len(r['mmio'])==int(length>0)
    m.put64(first+8,old_end)
    before=len(m.frees)
    r=invoke(m,TIMING_DTOR,{},'timing-original-destruction');r['deallocations']=len(m.frees)-before
    assert r['deallocations']>125
    for mode in ('encoded-error','write-error-but-done','no-completion'):
        ctor(TIMING_CTOR,dict(mmio_mode=mode),'timing-mailbox-error',
             'instruction-limit' if mode in ('no-completion','encoded-error') else 'returned')

    for bits,reply,busy in itertools.product(range(4),(0,1),(0,2)):
        inputs=dict(mmio={'0x5f58':bits,'0x12908':0x1122334455667788},reply_low=reply,busy_reads=busy)
        m,r=ctor(NOVA_CTOR,inputs,'nova-constructor-query-and-overwritten-result')
        assert r['state']['flags']==[bits&1,int(not bits&2),int(not bits&2)]
        assert r['state']['sampled_config']==0x55667788 and r['state']['pci_bdf']==0xffffffff
        assert [w['high'] for w in r['writes']]==[0x80000205,0x80000005]
        assert r['msr_reads']==2*(busy+2)
    for mode in sorted(MSR_MODES-{'success'}):
        ctor(NOVA_CTOR,dict(io_mode=mode),'nova-constructor-failure-path',
             'instruction-limit' if mode in {'busy-forever','read-error','read-eof','short-read-4','short-read-7','open-error'} else 'returned',20000)

    for busy in (0,1,99,100,101,1000):
        inputs=dict(busy_reads=busy,reply_low=0x89abcdef,byte1=0x123,byte2=0x234,mask=0x1fffff0ff,shift=36)
        m,r=ctor(MEM2,inputs,'mem2-bounded-poll')
        assert m.reg('rax')==((0x89abcdef&0xfffff0ff)>>4)
        assert r['writes'][0]['high']==0x8034231e
        assert r['msr_reads']==min(busy+1,101)
        r['return_int']=m.reg('rax')
    for mode in sorted(MSR_MODES-{'success'}):
        m,r=ctor(MEM2,dict(io_mode=mode,reply_low=0x89abcdef),'mem2-error-still-returns')
        r['return_int']=m.reg('rax')
        assert r['msr_reads']<=101

    n,r=ctor(NOVA_CTOR,{},'nova-method-object')
    for raw in (0,1,0x100,0x200,0xabcdef12,0xffffffff,0x780000000,0x123456789abcdef0):
        params=dict(mmio_default=raw)
        for suffix in ('18get_sa_perf_statusEv','13get_ipu_ratioEv','11get_sav_vidEv','17get_sa_pll_statusEv',
                       '20get_memss_qclk_ratioEv','12get_geardownEv','13get_dll_bw_cbERiS0_S0_'):
            r=invoke(n,NOVA+suffix,params,'nova-register-bit-boundaries')
            if 'get_sa_perf' in suffix or 'get_sa_pll' in suffix:assert r['return_u64']==raw
            elif 'get_ipu' in suffix:assert r['return_int']==(raw>>27)&255
            elif 'get_sav' in suffix:assert r['return_int']==(0 if raw&255==0 else ((raw&255)+((raw>>9)&255))*(10 if raw&256 else 5))
            elif 'qclk' in suffix:assert r['return_int']==raw&255
            elif 'geardown' in suffix:assert r['return_int']==raw&1
            else:
                r['outputs']=list(struct.unpack('<3I',n.uc.mem_read(n.outputs,12)))
                assert r['outputs']==[(raw>>23)&15,(raw>>27)&3,(raw>>21)&3]
    for raw in (0,1,2,3,0xffffffff):
        before=n.u64(n.rw+0x18)
        r=invoke(n,NOVA+'14bandgap_bypassEv',dict(mmio_default=raw),'bandgap-temporary-base')
        assert r['return_int']==(raw>>1)&1 and n.u64(n.rw+0x18)==before
        assert r['mmio'][0]['address']==0xfe001f64
    for raw,rail in itertools.product((0,1,255,256,511,512,0xffffffff),range(4)):
        r=invoke(n,NOVA+'12GetLvrTargetEijj',dict(raw=raw,rail=rail,unused=17),'lvr-raw-rounding')
        assert r['return_u64']==lvr_reference(raw,rail)
    for values,selector in itertools.product(([0,100,200,10,200,511],[511,200,0,400,100,0],[0]*6,[511]*6),
                                              (0,3199,3200,3201,6399,6400,6401,9599,9600,9601,0xffffffff)):
        words=[sum(v<<(9*i) for i,v in enumerate(values[j:j+3])) for j in (0,3)]
        r=invoke(n,FUSED,dict(selector=selector,mmio={'0x40b0':words[0],'0x40b4':words[1]},previous_selected=[99,88,77]),'fused-selector-boundaries')
        r['raw_fields']=values;r['values']=n.flat(n.outputs,4);r['selected']=n.flat(n.outputs+24,4)
        expected,selected=fused_reference(values,selector)
        assert r['values']==expected and r['selected']==selected,(values,selector,r['selected'],selected)
    for bit in range(32):
        raw=1<<bit
        r=invoke(n,RAILS,dict(mmio={},mmio_default=raw,previous_flags=[True,False]),'rails-register-bit-walk')
        r['values']=n.flat(n.outputs,4);r['flags']=n.bools(n.rail_bits)
        assert [e['relative'] for e in r['mmio']]==list(RAIL_REGISTERS)
        assert r['flags']==[True,False]+[bool(raw&(1<<(21 if i==4 else 12))) for i in range(10)]
        assert r['values']==[lvr_reference(raw&511,(raw>>(9 if i==4 else 10))&3) for i in range(10)]
    for previous in ([True]*63,[False]*64):
        r=invoke(n,RAILS,dict(mmio_default=0x201000,previous_flags=previous,reuse_rail_flags=False),'rails-bool-growth-boundary')
        r['flags']=n.bools(n.rail_bits);assert r['flags']==previous+[True]*10
        r=invoke(n,RAILS,dict(reuse_rail_flags=True),'rails-append-on-repeated-call')
        r['flags']=n.bools(n.rail_bits);assert r['flags']==previous+[True]*20
    r=invoke(n,NOVA+'14bandgap_bypassEv',dict(mmio_mode='no-completion'),'bandgap-base-remains-changed-while-stalled','instruction-limit',20000)
    r['rw_base_while_stalled']=n.u64(n.rw+0x18);assert r['rw_base_while_stalled']==0xfe000000
    contract=portable_contract(fixture)
    return dict(schema=1,source_elf_sha256=base.SOURCE_SHA,fixture_sha256=FIXTURE_SHA,
                environment=dict(system=platform.system(),python=platform.python_version()),
                fixture_functions=len(fixture['functions']),fixture_code_bytes=sum(f['size'] for f in fixture['functions']),
                cases_characterized=len(rows),outcomes=dict(Counter(r['outcome'] for r in rows)),
                contract_sha256=hashlib.sha256(profile.encoded(contract)).hexdigest(),scope=fixture['scope'],
                limitations=contract['unresolved']+['All peripheral/libc outcomes are synthetic; original ELF and production code are unchanged.',
                                                   'Exception throws stop at a recorded boundary; exception unwinding and real concurrent threads are not executed.'],cases=rows)


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--binary',type=Path);parser.add_argument('--export-fixture',type=Path)
    parser.add_argument('--fixture',type=Path,default=FIXTURE);parser.add_argument('--output',type=Path)
    parser.add_argument('--export-contract',type=Path)
    args=parser.parse_args()
    if args.binary:
        if not args.export_fixture:parser.error('--binary requires --export-fixture')
        fixture=extract(args.binary);raw=profile.encoded(fixture);args.export_fixture.write_bytes(raw)
        print('fixture SHA '+hashlib.sha256(raw).hexdigest(),flush=True)
    else:fixture=load_fixture(args.fixture)
    if args.export_contract:args.export_contract.write_bytes(profile.encoded(portable_contract(fixture)))
    if args.output:
        report=investigate(fixture);args.output.write_bytes(profile.encoded(report))
        print(str(report['cases_characterized'])+' original NVL component cases; synthetic I/O only')
    elif not args.binary and not args.export_contract:parser.error('provide --output or --export-contract')


if __name__=='__main__':main()
