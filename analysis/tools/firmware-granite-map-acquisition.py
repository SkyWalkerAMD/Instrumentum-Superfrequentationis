#!/usr/bin/env python3
"""Byte-pinned UEFI presence/fuse query routed through a reference SMN reader.

Protocol selection, socket descriptors and peripheral replies are fixtures.
Only original instruction bytes execute; memory-mapped PCI is synthetic RAM.
This does not select a live provider or equate its fuse mask to SMU core masks.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import runpy
import struct
import uuid

import unicorn as u
from unicorn import x86_const as x

H = runpy.run_path(str(Path(__file__).with_name('firmware-granite-topology-protocol.py')))
SERVICE_SHA = '61a6f2850ea3cd207264d4c831dbaff253a53137eefcc5a3890e254698c10963'
GUID = uuid.UUID('264272b3-81d1-453a-8503-8989f4382e68').bytes_le
QUERY_GUID = uuid.UUID('5c278ea8-cf6b-4626-a7d6-7cfd23f30719').bytes_le
SERVICE_BASE, DATA, STACK, STOP, LOCATE = 0x200000, 0x300000, 0x110008, 0xf000, 0xe000
BOOT, SERVICE, DESCRIPTOR, OUT = DATA, DATA+0x1000, DATA+0x2000, DATA+0x3000
SENTINEL, BAD, DEVICE_ERROR = 0xa5a5a5a5, H['BAD'], H['DEVICE_ERROR']


class Replay:
    def __init__(self, provider, service):
        self.provider, self.service = provider, service
        self.trace = hashlib.sha256()
        self.cases = self.pci_reads = self.index_writes = 0
        self.seen = set()

    def run(self, presence_word, target_words, output_mask=15, locate_status=0,
            bus=0, previous_index=0xfeedcafe, descriptor_status=0):
        vm = u.Uc(u.UC_ARCH_X86,u.UC_MODE_64)
        for address,size in ((0,0x10000),(0x100000,0x20000),
                             (SERVICE_BASE,0x10000),(DATA,0x10000)):
            vm.mem_map(address,size)
        pci_base=0xe0000000+(bus<<20)
        vm.mem_map(pci_base,0x1000)
        vm.mem_write(0,self.provider); vm.mem_write(SERVICE_BASE,self.service)
        vm.mem_write(0x36e8,struct.pack('<Q',BOOT))
        vm.mem_write(BOOT+0x140,struct.pack('<Q',LOCATE))
        vm.mem_write(SERVICE+0x20,struct.pack('<Q',SERVICE_BASE+0x428c))
        # Deliberate descriptor fixture, including low bits the reader discards.
        vm.mem_write(DESCRIPTOR+0xe,struct.pack('<I',(bus<<20)|0x98765))
        vm.mem_write(OUT,struct.pack('<4I',*([SENTINEL]*4)))
        vm.mem_write(STACK,struct.pack('<Q',STOP))
        ptrs=[OUT+4*i if output_mask&(1<<i) else 0 for i in range(4)]
        vm.reg_write(x.UC_X86_REG_RSP,STACK)
        for register,value in zip((x.UC_X86_REG_RCX,x.UC_X86_REG_RDX,x.UC_X86_REG_R8,x.UC_X86_REG_R9),
                                  (0x3498,*ptrs[:3])): vm.reg_write(register,value)
        vm.mem_write(STACK+40,struct.pack('<Q',ptrs[3]))
        register_words={0x5d3bc:presence_word,0x304a03dc:target_words[0],0x324a03dc:target_words[1]}
        selected=previous_index; reads=[]; writes=[]; services=[]; locates=[]

        def return_from_stub(uc,value):
            sp=uc.reg_read(x.UC_X86_REG_RSP)
            ret,=struct.unpack('<Q',uc.mem_read(sp,8))
            uc.reg_write(x.UC_X86_REG_RAX,value)
            uc.reg_write(x.UC_X86_REG_RSP,sp+8);uc.reg_write(x.UC_X86_REG_RIP,ret)

        def instruction(uc,address,size,_):
            if address==LOCATE:
                assert bytes(uc.mem_read(uc.reg_read(x.UC_X86_REG_RCX),16))==GUID
                assert uc.reg_read(x.UC_X86_REG_RDX)==0
                ptr=uc.reg_read(x.UC_X86_REG_R8); assert 0x100000<=ptr<0x120000
                locates.append(locate_status)
                if not locate_status:uc.mem_write(ptr,struct.pack('<Q',SERVICE))
                return_from_stub(uc,locate_status);return
            if address==SERVICE_BASE+0x411c:
                # Socket->descriptor lookup is not claimed to execute. The
                # caller's socket is recorded and the supplied descriptor used.
                assert uc.reg_read(x.UC_X86_REG_ECX)==0
                if not descriptor_status:
                    uc.mem_write(uc.reg_read(x.UC_X86_REG_RDX),struct.pack('<Q',DESCRIPTOR))
                return_from_stub(uc,descriptor_status);return
            if address==SERVICE_BASE+0x428c:
                assert uc.reg_read(x.UC_X86_REG_RCX)==SERVICE and uc.reg_read(x.UC_X86_REG_EDX)==0
                services.append(uc.reg_read(x.UC_X86_REG_R8D))
            if 0x1250<=address<0x13b3:
                body,relative=self.provider,address
            else:
                relative=address-SERVICE_BASE;body=self.service
                assert any(lo<=relative<hi for lo,hi in
                    ((0x428c,0x42e6),(0x5198,0x52ca),(0x52cc,0x5339),(0x53f0,0x544a))),hex(address)
            assert bytes(uc.mem_read(address,size))==body[relative:relative+size]
            self.seen.add((0 if address<SERVICE_BASE else 1,relative))

        def read(uc,access,address,size,value,_):
            if address==pci_base+0xb8:
                assert size==4;uc.mem_write(address,struct.pack('<I',selected))
            elif address==pci_base+0xbc:
                assert size==4 and selected in register_words
                raw=register_words[selected];reads.append((selected,raw))
                uc.mem_write(address,struct.pack('<I',raw))
            else:
                assert not pci_base<=address<pci_base+0x1000,hex(address)

        def write(uc,access,address,size,value,_):
            nonlocal selected
            if address==pci_base+0xb8:
                assert size==4;selected=value;writes.append(value)
            else:
                # In particular, no PCI data-register/SMN value write is allowed.
                assert 0x100000<=address and address+size<=0x120000 or OUT<=address and address+size<=OUT+16,hex(address)

        vm.hook_add(u.UC_HOOK_CODE,instruction)
        vm.hook_add(u.UC_HOOK_MEM_READ,read)
        vm.hook_add(u.UC_HOOK_MEM_WRITE,write)
        vm.emu_start(0x1250,STOP,count=10000)
        assert vm.reg_read(x.UC_X86_REG_RIP)==STOP
        status=vm.reg_read(x.UC_X86_REG_RAX)
        results=list(struct.unpack('<4I',vm.mem_read(OUT,16)))
        allowed=bool(output_mask&0xb)  # Capacity-only pointer does not satisfy validation.
        if not allowed:
            assert status==BAD and not locates and not reads and not services
        elif locate_status:
            assert status==DEVICE_ERROR and locates==[locate_status] and not reads and not services
        elif descriptor_status:
            # The query ignores read-service failures. Zero is deliberately
            # seeded scratch RAM, not a legitimate fallback hardware reading.
            assert status==0 and services==[0x5d3bc,0x324a03dc] and not reads and not writes
            expected=[0,0,8,SENTINEL&0xffffff00]
            for n in range(4):assert results[n]==(expected[n] if output_mask&(1<<n) else SENTINEL)
        else:
            presence=presence_word>>22 & 3
            active=sum(1<<t for t in range(2) if presence&(1<<t) and target_words[t]&255!=255)
            first=0 if active&1 else 1
            expected_reads=[0x5d3bc]+[0x304a03dc|(t<<25) for t in range(2) if presence&(1<<t)]+[0x304a03dc|(first<<25)]
            assert status==0 and services==expected_reads and [r[0] for r in reads]==expected_reads
            assert writes==[v for register in expected_reads for v in (register,previous_index)]
            assert selected==previous_index
            expected=[active,target_words[first]&255,8,(SENTINEL&0xffffff00)|(target_words[first]>>8&1)]
            for n in range(4):assert results[n]==(expected[n] if output_mask&(1<<n) else SENTINEL)
        self.cases+=1;self.pci_reads+=len(reads);self.index_writes+=len(writes)
        row=dict(presence_word=presence_word,target_words=target_words,output_mask=output_mask,
            locate_status=locate_status,descriptor_status=descriptor_status,bus=bus,previous_index=previous_index,
            status=status,outputs=results,register_reads=reads,index_writes=writes)
        self.trace.update(json.dumps(row,sort_keys=True,separators=(',',':')).encode())
        return row


def check(replay):
    examples=[]
    for presence,first,second,extras in itertools.product(range(4),(0,1,0xfe,0xff,0x100,0x1ff),
            (0,0x81,0xff,0x100,0x1ff),(0,0xff3fffff)):
        row=replay.run((presence<<22)|extras,[first,second])
        if not extras and (presence,first,second) in ((0,0,0),(3,0xff,0x81),(3,1,0x1ff),(2,0,0x100)):
            examples.append(row)
    for outputs,locate in itertools.product(range(16),(0,1,DEVICE_ERROR)):
        replay.run(3<<22,[0x80,0x101],outputs,locate)
    for bus,index in itertools.product((0,1,0x7f,0xff),(0,0x5d3bc,0xffffffff)):
        replay.run(3<<22,[0x81,0x101],bus=bus,previous_index=index)
    for failure in (1,BAD,DEVICE_ERROR,0x800000000000000e):
        replay.run(3<<22,[0x81,0x101],descriptor_status=failure)
    return dict(cases=replay.cases,register_reads=replay.pci_reads,index_writes=replay.index_writes,
        source_instructions=len(replay.seen),trace_sha256=replay.trace.hexdigest(),examples=examples,
        caller_entry='provider-a:0x1250',query_protocol=str(uuid.UUID(bytes_le=QUERY_GUID)),query_slot='0x10',
        read_protocol=str(uuid.UUID(bytes_le=GUID)),read_slot='0x20',
        read_entry='register-service:0x428c',indirect_reader='register-service:0x52cc',
        presence_register='0x5d3bc',presence_first_bit=22,presence_width=2,
        target_fuse_registers=['0x304a03dc','0x324a03dc'],
        target_mask_test='present AND low byte != 0xff',
        final_target_selection='target 0 if returned mask bit 0 is set, else target 1 even if no target passed',
        other_outputs='selected register low byte; constant 8; selected register bit 8',
        pci_index_offset='0xb8',pci_data_offset='0xbc',index_restored_after_each_read=True,
        ignored_read_service_failure_cases=4,read_failure_scratch_is_synthetic_zero=True,
        register_data_writes=0,read_service_selection_is_fixture=True,socket_descriptor_is_fixture=True,
        pci_window_and_replies_are_fixtures=True)


def publication(image,start,stop,boot_pointer,expected_guid,expected_table):
    """Replay only the original argument-building slice around InstallProtocol.

    The service itself is a recording stub. Eligibility, prior initialization,
    installation success and runtime provider choice are not asserted.
    """
    vm=u.Uc(u.UC_ARCH_X86,u.UC_MODE_64)
    for address,size in ((0,0x10000),(0x100000,0x20000),(DATA,0x10000)):vm.mem_map(address,size)
    vm.mem_write(0,image);vm.mem_write(boot_pointer,struct.pack('<Q',BOOT))
    vm.mem_write(BOOT+0x80,struct.pack('<Q',LOCATE))
    vm.reg_write(x.UC_X86_REG_RSP,STACK);vm.reg_write(x.UC_X86_REG_RBP,STACK+0x100)
    calls=[]
    def code(uc,address,size,_):
        if address==LOCATE:
            assert uc.reg_read(x.UC_X86_REG_R8)==0
            guid=bytes(uc.mem_read(uc.reg_read(x.UC_X86_REG_RDX),16))
            table=uc.reg_read(x.UC_X86_REG_R9)
            assert guid==expected_guid and table==expected_table
            calls.append((str(uuid.UUID(bytes_le=guid)),table))
            sp=uc.reg_read(x.UC_X86_REG_RSP);ret,=struct.unpack('<Q',uc.mem_read(sp,8))
            uc.reg_write(x.UC_X86_REG_RSP,sp+8);uc.reg_write(x.UC_X86_REG_RIP,ret);uc.reg_write(x.UC_X86_REG_RAX,0)
        else:
            assert start<=address<stop and bytes(uc.mem_read(address,size))==image[address:address+size]
    def write(uc,access,address,size,value,_):
        assert 0x100000<=address and address+size<=0x120000,hex(address)
    vm.hook_add(u.UC_HOOK_CODE,code);vm.hook_add(u.UC_HOOK_MEM_WRITE,write)
    vm.emu_start(start,stop,count=50)
    assert vm.reg_read(x.UC_X86_REG_RIP)==stop and len(calls)==1
    return dict(start=hex(start),stop=hex(stop),guid=calls[0][0],table=hex(calls[0][1]),
        service_is_recording_stub=True,installation_success_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('provider-a','register-service','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    provider=H['image'](args.provider_a,H['PROVIDERS']['a'][0])
    service=H['image'](args.register_service,SERVICE_SHA)
    assert provider[0x3270:0x3280]==QUERY_GUID and provider[0x32e0:0x32f0]==GUID
    assert struct.unpack_from('<Q',provider,0x34a8)[0]==0x1250
    assert service[0x7fa0:0x7fb0]==GUID and struct.unpack_from('<Q',service,0x8040)[0]==0x428c
    report=dict(schema=1,provider_sha256=H['PROVIDERS']['a'][0],register_service_sha256=SERVICE_SHA,
        hardware_execution=False,host_cpuid_executed=False,firmware_installation_executed=False,
        publication=[publication(provider,0x7ab,0x7d1,0x36e8,QUERY_GUID,0x3498),
                     publication(service,0xc45,0xc67,0x8f10,GUID,0x8020)],
        acquisition=check(Replay(provider,service)),
        additional_same_guid_implementations=['50578334-FCFD-4DCA-B3AF-47DD76E946CF','AD82C06C-34C0-44F0-BA98-801C3CA7592F'],
        runtime_provider_selection_verified=False,smu_core_mask_equivalence_verified=False,
        physical_ccd_mapping_verified=False,os_cpu_mapping_verified=False,runtime_write_enabled=False,
        limits='The checked provider query and selected SMN read implementation run on synthetic protocol/descriptor/peripheral inputs. Multiple capsule modules install the same read GUID. The selected fuse low byte is not authenticated as the 0x203c003c SMU core mask, and no target is bound to a physical CCD or OS CPU.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:({x:y for x,y in v.items() if x!='examples'} if k=='acquisition' else v) for k,v in report.items()}))


if __name__=='__main__':main()
