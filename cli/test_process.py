#!/usr/bin/env python3
"""CLI process/JSON/affinity and sysfs fixture checks. No register device access."""
import binascii
import json
import os
import subprocess
import struct
import sys
import tempfile
from pathlib import Path

binary, fixture = map(str, map(Path, sys.argv[1:3]))
env = dict(os.environ)
for key in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM", "DBUS_SESSION_BUS_ADDRESS"):
    env.pop(key, None)


def run(*args, code=0, preexec_fn=None):
    p = subprocess.run([binary, *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True, timeout=15, preexec_fn=preexec_fn)
    assert p.returncode == code, (args, p.returncode, p.stdout, p.stderr)
    assert not p.stderr, p.stderr
    result = json.loads(p.stdout)
    assert result["schema_version"] == 1 and result["ok"] == (code == 0)
    return result


data = run("diagnose")["data"]
assert data["allowed_cpus"] and not data["display_required"] and not data["register_access_tested"]
cpu = sorted(os.sched_getaffinity(0))[-1]
limited = run("diagnose", preexec_fn=lambda: os.sched_setaffinity(0, {cpu}))
assert limited["data"]["allowed_cpus"] == [cpu]
run("intel-set", "--cpu", str(cpu), "--field", "pl1", "--value", "100", code=2)
run("cpu", "--cpu", "999999999999999999999999", code=2)

lines = subprocess.check_output([fixture, "--emit"], text=True, env=env, timeout=60).splitlines()
reports = [json.loads(line) for line in lines]
queries = [r for r in reports if r['command'] in ('amd-ln2-read','amd-core-performance-read')]
assert len(queries) == 44
query_results = [r for r in queries if 'query' in r['data']]
assert len(query_results) == 24
assert sum(r['ok'] for r in query_results) == 14
for r in query_results:
    d = r['data']
    assert not d['physical_mapping_verified'] and not d['setting_commands_sent']
    assert d['valid'] == r['ok']
    if d['raw_valid']:
        assert isinstance(d['raw'], str) and 0 <= int(d['raw'], 16) <= 0xffffffff
    else:
        assert d['raw'] is None and d['ln2_enabled'] is None
    if d['query'] == 'ln2-mode':
        assert d['firmware_index'] is None and d['command'] == '0x000000a6'
        if r['ok']:
            assert d['ln2_enabled'] == (int(d['raw'],16) == 1)
        else:
            assert d['ln2_enabled'] is None
    else:
        assert d['ln2_enabled'] is None and d['command'] == '0x0000005c'
        assert d['firmware_index'] in (0,1,7,127)
assert sum(r['error'] == -22 for r in queries) == 20
assert len(reports) == 1547, len(reports)
l3_reports=[r for r in reports if r['command']=='amd-topology' and r['data'].get('topology',{}).get('l3_requested')]
assert len(l3_reports)==4 and [r['ok'] for r in l3_reports]==[True,True,False,False]
for i,r in enumerate(l3_reports):
    t=r['data']['topology']
    assert not t['pm_slot_mapping_verified'] and not t['firmware_mapping_verified']
    if i<2:
        assert t['ccd_level_type']==2
        assert t['l3']['bytes']==[100663296,33554432][i]
        assert t['l3']['cache_id']==[1,0][i] and t['ccd_in_socket']==[1,0][i]
        assert t['l3']['sharing_capacity']==16
    else:
        assert t['l3'] is None and t['apic_id'] is None
adjustments = [r for r in reports if r['command'].startswith('amd-power-adjustment-')]
assert len(adjustments) == 39
assert sum(r['error'] == -22 for r in adjustments) == 18
for r in adjustments:
    d = r['data']
    if 'scope' not in d:
        continue
    assert d['scope'] == 'shared' and not d['physical_units_verified'] and not d['hardware_effect_measured']
    states = [d['state']] if 'state' in d else [d['before'], d['after']]
    for state in states:
        if state['valid']:
            assert state['firmware_version'] == '0x00732e00'
            assert -128 <= state['counts'] <= 127
            assert int(state['raw'],16) == state['counts'] & 0xffffffff
        else:
            assert state['counts'] is None
    if 'verified' in d:
        assert d['verified'] == r['ok']
        if d['verified']:
            assert d['after']['counts'] == d['requested_counts']
        assert d['command'] == '0x000000a7'
all_curve = [r for r in reports if r['command'] == 'amd-curve-all-set']
assert len(all_curve) == 35 and [r['ok'] for r in all_curve] == [True]*8+[False]*27
for i,r in enumerate(all_curve[:13]):
    d = r['data']
    assert d['scope'] == 'all-cores' and int(d['command'],16) == 7
    assert int(d['argument'],16) == (d['margin_counts'] & 0xffff)
    assert not d['verified'] and not d['physical_mapping_verified']
    assert d['current_margin_counts'] is None and d['affected_core_count'] is None
    assert d['accepted'] == (i < 8) and d['write_attempted'] == (i < 8 or i == 10)
all_native_pm = [r for r in reports if r['command'] == 'amd-pm-native-read']
assert len(all_native_pm) == 49
native_pm = all_native_pm[:33]
assert len(native_pm) == 33 and [r['ok'] for r in native_pm] == [True]*8+[False]*21+[True,False,False,False]
for i,r in enumerate(native_pm[:17]):
    n = r['data']['native']; pm = r['data']['pm']
    assert n['cpu'] == 130 and not n['origin_verified'] and not n['atomic_snapshot_guaranteed']
    assert not n['freshness_verified'] and not n['physical_mapping_verified'] and not n['tuning_settings_requested']
    if i < 9:
        assert n['metadata_rechecked'] and n['transfer_attempted'] and n['transfer_accepted']
        assert n['bytes_read'] == n['reported_bytes'] == pm['captured_bytes'] == len(bytes.fromhex(pm['raw_hex']))
        assert int(n['address'],16) == 0x123456000 and n['phase'] == ('complete' if i < 8 else 'decode')
    else:
        assert not n['metadata_rechecked'] and pm['raw_hex'] == '' and pm['fields'] == []
    assert not n['ccd_mapping']['requested'] and not n['ccd_mapping']['valid']
for i,r in enumerate(native_pm[29:]):
    n = r['data']['native']; m = n['ccd_mapping']; pm = r['data']['pm']
    assert m['requested'] and m['valid'] == m['rechecked'] == (i == 0)
    assert m['firmware_ccd_ids_by_slot'] == ([0,15] if i == 0 else [])
    assert not m['os_cpu_mapping_verified'] and not m['active_core_mask_verified']
    assert not n['physical_mapping_verified'] and not n['tuning_settings_requested']
    if i == 0:
        assert m['queried_before'] == m['queried_after'] == 16
        assert int(m['present_mask_before'],16) == int(m['present_mask_after'],16) == 0x8001
        assert pm['raw_hex'] and int(m['smu_version'],16) == 0x732e00
    else:
        assert not pm['raw_hex'] and not n['metadata_rechecked']
granite_pm = all_native_pm[33:41]
assert [r['ok'] for r in granite_pm] == [True,True]+[False]*6
assert all(r['error'] == -22 for r in all_native_pm[45:])
for i,r in enumerate(all_native_pm[41:45]):
    n,pm=r['data']['native'],r['data']['pm'];m=n['ccd_mapping']
    assert r['ok']==(i==0) and m['requested'] and m['valid']==m['rechecked']==(i==0)
    assert m['source']=='granite-two-slot-initializer' and not m['presence_mask_queried']
    assert m['queried_before']==m['queried_after']==0
    assert m['firmware_ccd_ids_by_slot']==([0,1] if i==0 else [])
    assert m['inferred_present_mask']==('0x0003' if i==0 else None)
    assert not n['physical_mapping_verified'] and not m['os_cpu_mapping_verified']
    assert bool(pm['raw_hex'])==(i==0) and n['metadata_rechecked']==(i==0)
for i, r in enumerate(granite_pm):
    n, pm = r['data']['native'], r['data']['pm']
    assert n['platform'] == 'granite-ridge' and not n['tuning_settings_requested']
    assert not n['ccd_mapping']['valid'] and not n['ccd_mapping']['requested']
    assert not n['physical_mapping_verified'] and not n['freshness_verified']
    if i < 3:
        slots = 1 if i == 0 else 2
        length = 1828 if i == 0 else 2452
        assert n['metadata_rechecked'] and n['smu_version'] == '0x00625400'
        assert n['bytes_read'] == pm['captured_bytes'] == length
        assert pm['platform'] == 'granite-ridge'
        assert len(pm['firmware_core_arrays']) == 18*8*slots
        assert len(pm['firmware_ccd_arrays']) == 12*slots
        assert len(pm['granite_stored_words']) == length//4
        assert pm['granite_stored_words'][-1]['raw'] == ('0x7f800001' if i == 2 else '0x42c80000')
        assert len(pm['fields'])==22
        assert all(f['unit'] == 'unverified' for f in pm['fields'][:7])
        assert [f['unit'] for f in pm['fields'][7:]]==['V','V','A','W','deg C']*3
        assert [f['stored_value'] for f in pm['fields'][7:]]==[1.125,1.25,12.5,15.625,60.5,.875,1.5,20,30,65.25,.625,1,2,2,70]
        for j,f in enumerate(pm['fields'][7:]):
            assert f['firmware_channel']==j//5
            assert f['group']==('granite_command_mirror' if j%5==0 else 'granite_svi')
            if j%5==0: assert f['software_command_mirror'] and not f['measurement']
            assert not any(f[k] for k in ('capture_firmware_verified','physical_rail_verified','freshness_verified','setting_readback_verified'))
        for f in pm['firmware_core_arrays']:
            if f['row']>=13:
                assert f['stored_value']==(.625,.125,25,75,150)[f['row']-13]
                assert f['unit']==('ratio' if f['row']<15 else '%')
                assert not f['counter_meaning_verified'] and not f['freshness_verified']
        for f in pm['firmware_ccd_arrays']:
            if f['row']==11:
                assert f['unit']=='%' and not f['counter_meaning_verified'] and not f['freshness_verified']
                if not f['error']: assert f['stored_value']==(100 if f['slot']==slots-1 else 25)
    else:
        assert not n['metadata_rechecked'] and not pm['raw_hex'] and not pm['granite_stored_words']
with tempfile.TemporaryDirectory(prefix='octool-granite-') as directory:
    capture, raw = Path(directory)/'capture.json', Path(directory)/'table.bin'
    for i, r in enumerate(granite_pm[:3]):
        pm = r['data']['pm']; capture.write_text(json.dumps(r),encoding='utf-8')
        code = 3 if i == 2 else 0
        decoded = run('amd-pm-decode','--file',str(capture),code=code)['data']['pm']
        assert decoded == pm
        raw.write_bytes(bytes.fromhex(pm['raw_hex']))
        decoded = run('amd-pm-decode','--file',str(raw),'--version',pm['version'],code=code)['data']['pm']
        assert decoded == pm
        raw.write_bytes(raw.read_bytes()[:-4])
        assert run('amd-pm-decode','--file',str(raw),'--version',pm['version'],code=3)['error'] == -90
oc_modes = [r for r in reports if r['command'] == 'amd-oc-mode-set' and 'requested_enabled' in r['data']]
assert len(oc_modes) == 9 and [r['ok'] for r in oc_modes] == [True]*3 + [False]*6
for r in oc_modes:
    d = r['data']
    assert d['verified'] == r['ok'] and not d['hardware_effect_measured'] and not d['scalar_reset_requested']
    assert int(d['command'],16) == (0x5d if d['requested_enabled'] else 0x5e)
    if r['ok']: assert d['after']['manual_oc_enabled'] == d['requested_enabled']
    for state in ('before', 'after'):
        s = d[state]
        if s['valid']:
            raw = int(s['capabilities_raw'], 16)
            assert s['manual_oc_available'] == bool(raw & 1)
            assert s['manual_oc_enabled'] == bool(raw & 0x10000)
        else:
            assert s['manual_oc_available'] is None and s['manual_oc_enabled'] is None
assert oc_modes[2]['data']['unchanged'] and not oc_modes[2]['data']['write_attempted']
assert [r['data']['accepted'] for r in oc_modes] == [True,True,False,False,False,False,True,False,True]
assert oc_modes[6]['data']['after']['manual_oc_enabled'] is False
assert oc_modes[8]['data']['after']['manual_oc_enabled'] is None
oc_freq = [r for r in reports if r['command'] == 'amd-oc-frequency-set' and 'requested_mhz' in r['data'] and r['data']['scope'] != 'cores']
assert len(oc_freq) == 9 and [r['ok'] for r in oc_freq] == [True]*4 + [False]*5
for r in oc_freq:
    d = r['data']
    assert not d['verified'] and not d['readback_supported'] and d['current_frequency_mhz'] is None
    assert not d['hardware_effect_measured'] and not d['physical_mapping_verified']
for r in oc_freq[:4]:
    d = r['data']; single = d['scope'] == 'core'
    assert d['accepted'] and d['write_attempted'] and d['requested_mhz'] == 5000
    assert d['firmware_ccd'] == (15 if single else None) and d['firmware_core'] == (7 if single else None)
    assert int(d['command'],16) == (0x60 if single else 0x5f)
    assert int(d['argument'],16) == (0xf0701388 if single else 5000)
oc_selected = [r for r in reports if r['command'] == 'amd-oc-frequency-set' and r['data'].get('scope') == 'cores']
assert len(oc_selected) == 16 and [r['ok'] for r in oc_selected] == [True]*4 + [False]*12
for i,r in enumerate(oc_selected):
    d = r['data']; mask = int(d['core_mask'],16)
    assert d['firmware_ccd'] == 15 and d['requested_mhz'] == 5000 and d['completed'] == r['ok']
    assert not d['verified'] and not d['readback_supported'] and not d['physical_mapping_verified'] and not d['hardware_effect_measured']
    assert [v['firmware_core'] for v in d['cores']] == [n for n in range(8) if mask & (1<<n)]
    for v in d['cores']:
        bit = 1 << v['firmware_core']
        assert v['visited'] == bool(int(d['visited_mask'],16) & bit)
        assert v['write_attempted'] == bool(int(d['attempted_mask'],16) & bit)
        assert v['accepted'] == bool(int(d['accepted_mask'],16) & bit)
        assert not v['verified'] and v['current_frequency_mhz'] is None
        if not v['visited']:
            assert all(v[k] is None for k in ('error','phase','capabilities_raw','argument','response'))
        else:
            assert int(v['argument'],16) == (15<<28) | (v['firmware_core']<<20) | 5000
    if i < 4:
        assert mask == (1,128,133,255)[i] and d['failed_core'] is None and d['accepted']
        assert int(d['accepted_mask'],16) == mask and int(d['attempted_mask'],16) == mask
    else:
        fault,position = divmod(i-4,3); before = (0,1,5)[position]; bit = 1 << (0,2,7)[position]
        assert int(d['accepted_mask'],16) == before and not d['accepted']
        assert int(d['attempted_mask'],16) == before | (bit if fault == 0 else 0)
        assert int(d['visited_mask'],16) == before | bit and d['failed_core'] == (0,2,7)[position]
for cores in ('','0,0','2,0','0,8','0, 2','0-7'):
    run('amd-oc-frequency-set','--cpu',str(cpu),'--scope','cores','--ccd','0','--cores',cores,'--mhz','5000','--expect-caps','0x10007','--apply',code=2)
run('amd-oc-mode-set','--cpu',str(cpu),'--enabled','1','--expect-caps','6',code=2)
run('amd-oc-mode-set','--cpu',str(cpu),'--enabled','true','--expect-caps','6','--apply',code=2)
run('amd-oc-frequency-set','--cpu',str(cpu),'--scope','all-cores','--mhz','5000','--expect-caps','0x10007',code=2)
run('amd-oc-frequency-set','--cpu',str(cpu),'--scope','core','--ccd','0','--core','0','--mhz','5000','--expect-caps','7','--apply',code=2)
limits = [r for r in reports if r['command'] == 'amd-pbo-limit-set' and 'requested_value' in r['data']]
assert len(limits) == 9 and [r['ok'] for r in limits] == [True]*4 + [False]*5
for r,kind,unit,value,arg,cmd in zip(limits[:4],('ppt','tdc','edc','temperature'),('W','A','A','C'),(350,280,500,90),(350000,280000,500000,90),(0x56,0x57,0x58,0x59)):
    d = r['data']
    assert d['limit'] == kind and d['unit'] == unit and d['requested_value'] == value
    assert int(d['command'],16) == cmd and int(d['argument'],16) == arg and d['accepted'] and d['write_attempted']
for r in limits:
    assert not r['data']['verified'] and not r['data']['readback_supported'] and r['data']['current_limit'] is None
    assert not r['data']['hardware_effect_measured']
assert [r['data']['write_attempted'] for r in limits[4:]] == [False,False,True,False,False]
run('amd-pbo-limit-set','--cpu',str(cpu),'--limit','ppt','--value','350','--expect-caps','6',code=2)
run('amd-pbo-limit-set','--cpu',str(cpu),'--limit','temperature','--value','101','--expect-caps','6','--apply',code=2)
fmax = [r for r in reports if r['command'] == 'amd-pbo-fmax-set' and 'requested_mhz' in r['data']]
assert len(fmax) == 10 and [r['ok'] for r in fmax] == [True,True]+[False]*8
assert [r['data']['verified'] for r in fmax] == [True,True]+[False]*8
assert all(r['data']['readback_supported'] and r['data']['operating_frequency_mhz'] is None and not r['data']['hardware_effect_measured'] for r in fmax)
assert fmax[0]['data']['accepted'] and fmax[0]['data']['requested_mhz'] == 5000
assert fmax[0]['data']['current_fmax_mhz'] == 5000 and fmax[1]['data']['unchanged'] and not fmax[1]['data']['write_attempted']
assert fmax[6]['data']['current_fmax_mhz'] == 6000 and fmax[7]['data']['current_fmax_mhz'] == 5500
fmax_reads = [r for r in reports if r['command'] == 'amd-pbo-fmax-read' and 'fmax' in r['data']]
assert [r['data']['current_fmax_mhz'] for r in fmax_reads] == [0,5500,0xfffff,None,None,None,None]
assert all(r['data']['operating_frequency_mhz'] is None for r in fmax_reads)
for a,b,word in ((700,1250,91 | (201 << 17)),(0,1000,151 | (151 << 17))):
    r = run('amd-pbo-rails-encode','--rail0-mv',str(a),'--rail1-mv',str(b))['data']
    assert not r['hardware_accessed'] and not r['write_supported'] and int(r['argument'],16) == word
small = run('amd-pm-layout-audit','--version','0x730204','--bytes','2844','--fused-ccds','2')['data']['layout_audit']
large = run('amd-pm-layout-audit','--version','0x731004','--bytes','11636','--fused-ccds','16')['data']['layout_audit']
assert small['outside_words'] == 22 and large['overlapping_words'] == 176
assert not large['decoder_enabled'] and not large['physical_mapping_verified']
pbo_settings = [r for r in reports if r['command'] == 'amd-pbo-scalar-set' and 'requested_multiplier' in r['data']]
assert len(pbo_settings) == 8
assert [r['ok'] for r in pbo_settings] == [True,True,False,False,False,False,False,False]
assert [r['data']['write_attempted'] for r in pbo_settings] == [True,False,False,True,True,False,False,True]
assert [r['data']['accepted'] for r in pbo_settings] == [True,False,False,True,False,False,False,True]
for r in pbo_settings:
    d = r['data']
    assert d['verified'] == r['ok'] and not d['hardware_effect_measured']
    assert int(d['argument'],16) == d['requested_multiplier'] * 100
    if r['ok']: assert d['after']['multiplier'] == d['requested_multiplier']
assert pbo_settings[1]['data']['unchanged']
assert pbo_settings[2]['error'] == -11 and pbo_settings[2]['data']['phase'] == 'compare'
assert pbo_settings[3]['data']['after']['multiplier'] == 1 and pbo_settings[3]['data']['phase'] == 'verify'
assert pbo_settings[7]['data']['after']['multiplier'] is None and int(pbo_settings[7]['data']['after']['raw'],16) == 0x7fc00000
pbo_reads = [r for r in reports if r['command'] == 'amd-pbo-scalar-read']
assert [r['data']['scalar']['multiplier'] for r in pbo_reads] == [0,2.5,None]
assert [r['data']['scalar']['valid'] for r in pbo_reads] == [True,True,False]
curve_settings = [r for r in reports if r['command'] == 'amd-curve-set' and r['error'] != -22]
assert len(curve_settings) == 9
assert [r['ok'] for r in curve_settings] == [True, False, False, False, False, False, False, True, True]
assert [int(r['data']['attempted_mask'],16) for r in curve_settings] == [0x85,0,1,1,5,0,0,0,0x80]
assert [int(r['data']['verified_mask'],16) for r in curve_settings] == [0x85,0,0,0,1,0,0,0x85,0x80]
for r in curve_settings:
    data = r['data']
    assert not data['physical_mapping_verified'] and not data['hardware_effect_measured']
    assert data['verified'] == r['ok']
    for core in data['cores']:
        if core['verified']: assert int(core['readback_raw'],16) == data['margin_counts'] & 0xffffffff
        if not core['attempted']: assert not core['accepted']
assert curve_settings[4]['data']['stopped_core'] == 2 and curve_settings[4]['data']['cores'][2]['readback_raw'] is None
curve_selections = [r for r in reports if r['command'] == 'amd-curve-read' and 'cores' in r['data']]
assert len(curve_selections) == 2 and curve_selections[1]['data']['cores'] is None
assert [c['firmware_core'] for c in curve_selections[0]['data']['cores']] == [0,2,7]
platform_reports=[r for r in reports if r['command']=='intel-read' and 'readings' in r['data']]
assert len(platform_reports)==81
hwp_context_reports=platform_reports[63:]
platform_reports=platform_reports[:63]  # Preserve the previous telemetry/error fixture assertions.
for report in platform_reports:
    assert not report['data']['energy_counters_are_instantaneous_power'] and not report['data']['energy_counters_include_wrap_history']
    assert report['data']['hwp_status_is_history']
    readings={r['name']:r for r in report['data']['readings']}
    unit=readings.get('RAPL energy counter unit'); limit=readings.get('RAPL energy counter wrap range')
    if unit and not unit['error']:
        exponent=(int(unit['raw'],16)>>8)&31
        assert unit['unit']=='J/tick' and unit['value']==2.0**-exponent
        assert limit['value']==2.0**(32-exponent) and not unit['editable'] and not limit['editable']
    for r in readings.values():
        if r['msr']=='0x00000601':
            assert r['name']=='Package PL4 (peak power)' and r['unit']=='W'
            if r['error']: assert r['value'] is None and r['raw'] is None and not r['editable']
            else:
                raw=int(r['raw'],16); exponent=int(unit['raw'],16)&15
                assert r['value']==(raw&0x1fff)*2.0**-exponent
                assert r['editable']==(not bool(raw&(1<<31)))
        if r['msr'] in ('0x00000611','0x00000619','0x00000639'):
            assert not r['editable'] and r['unit']=='J'
            if not r['error']:
                assert r['value']==(int(r['raw'],16)&0xffffffff)*unit['value']
                assert r['value']<limit['value']
            else: assert r['raw'] is None and r['value'] is None
        if r['name'] in ('HWP guaranteed capability','HWP most efficient capability'):
            assert not r['editable'] and r['msr']=='0x00000771' and r['unit']=='performance level'
            if not r['error']:
                shift=8 if r['name']=='HWP guaranteed capability' else 16
                assert r['value']==((int(r['raw'],16)>>shift)&255)
            else: assert r['raw'] is None and r['value'] is None
    history=[r for r in readings.values() if r['msr']=='0x00000777']
    assert len(history) in (0,2)
    for r,bit,name in zip(history,(0,2),('guaranteed performance change','excursion to minimum')):
        assert r['name']=='HWP '+name+' history' and r['unit']=='0 / 1' and not r['editable']
        if r['error']: assert r['raw'] is None and r['value'] is None
        else: assert r['value']==((int(r['raw'],16)>>bit)&1)
assert [r['ok'] for r in platform_reports[-9:]]==[True]*8+[False]
assert not any(r['msr']=='0x00000777' for r in platform_reports[-2]['data']['readings'])
assert sum(r['msr']=='0x00000777' for r in platform_reports[-3]['data']['readings'])==2
for report in platform_reports:
    row=next((v for v in report['data']['readings'] if v['name']=='Maximum non-turbo ratio'),None)
    if row is None:
        assert report is platform_reports[-3]  # Architectural HWP without a model-specific RAPL profile.
        continue
    assert row['msr']=='0x000000ce' and row['unit']=='ratio' and not row['editable']
    if not row['error']:
        assert row['value']==((int(row['raw'],16)>>8)&255) and row['value']>0
    else:
        assert row['value'] is None and row['raw'] is None

assert len(hwp_context_reports)==18
assert [r['ok'] for r in hwp_context_reports]==[True]*12+[False]*6
for report in hwp_context_reports:
    d=report['data']; assert d['hwp_requests_are_stored_hints'] and not d['hwp_request_snapshot_atomic']
    for row in d['readings']:
        if row['error']:
            assert row['value'] is None and row['raw'] is None and not row['editable']
        if row['msr'] in ('0x00000773','0x00000775','0x00000776'):
            assert not row['editable']
golden={r['name']:r for r in hwp_context_reports[0]['data']['readings']}
expected={'HWP package minimum':16,'HWP package maximum':56,'HWP package energy preference':136,
          'HWP package activity window (0 = automatic)':20000,'HWP PECI minimum':18,'HWP PECI maximum':48,
          'HWP PECI energy preference':195,'HWP PECI minimum override':0,'HWP PECI maximum override':1,'HWP PECI EPP override':1}
for name,value in expected.items():
    assert golden[name]['value']==value
    assert golden[name]['editable']==(name in ('HWP package minimum','HWP package activity window (0 = automatic)'))
assert golden['HWP minimum']['editable'] and not golden['HWP maximum']['editable'] and not golden['HWP energy preference']['editable']
sets=[r for r in reports if r['command']=='intel-set' and r.get('data',{}).get('field')=='hwp-epp'][-8:]
assert all(r['command']=='intel-set' and r['data']['field']=='hwp-epp' for r in sets)
assert [r['ok'] for r in sets]==[True,True]+[False]*6
assert [r['data']['write_attempted'] for r in sets]==[True,True,False,False,False,True,True,False]
assert all(not r['data']['verified'] for r in sets[2:])

package=[r for r in reports if r['command']=='intel-set' and r.get('data',{}).get('shared_package_hint')]
assert len(package)==22 and [r['ok'] for r in package]==[True]*10+[False]*12
original=0xa5a5019488003810
for i,report in enumerate(package):
    data=report['data']
    assert data['capability_reference_cpu']==130 and not data['selection_flags_changed']
    assert not data['hardware_effect_measured'] and data['verified']==report['ok']
    if i<10:
        index=i//2; shift=(0,8,16,24,32)[index]; mask=(1023 if index==4 else 255)<<shift
        expected_raw=(original & ~mask)|((24,48,32,192,399)[index]<<shift)
        assert data['msr']=='0x00000772' and int(data['expected_raw'],16)==expected_raw
        assert int(data['readback_raw'],16)==expected_raw
        assert data['readback_value']==(24,48,32,192,15000)[index]
        assert data['write_attempted']==(i%2==0) and data['unchanged']==(i%2==1)
        assert data['completed_writes']==(1 if i%2==0 else 0)
    else:
        assert data['readback_value'] is None
        assert data['write_attempted']==(i-10 in (6,7,8,9,11))

event_names=('thermal event','PROCHOT event','critical temperature','thermal threshold 1','thermal threshold 2','power limit')
event_rows=[]
for report in platform_reports:
    rows=[r for r in report['data']['readings'] if r['msr']=='0x000001b1' and r['unit']=='0 / 1']
    if not rows: continue
    assert len(rows)==12
    event_rows.append(rows)
    for bit,row in enumerate(rows):
        assert row['name']=='Package '+event_names[bit//2]+(' history' if bit%2 else ' active')
        assert not row['editable']
        if row['error']: assert row['value'] is None and row['raw'] is None
        else: assert row['value']==((int(row['raw'],16)>>bit)&1)
assert len(event_rows)==18 and sum(bool(rows[0]['error']) for rows in event_rows)==3
limits=[r for r in reports if r['command'] in ('intel-limits-read','intel-limits-clear') and 'before' in r['data']]
assert len(limits)==29 and [r['ok'] for r in limits]==[True]*17+[False]*12
reason_bits=dict(zip(('prochot','thermal','residency','ratl','vr-thermal','vr-current','other','pl1','pl2','turbo','transition'),(0,1,4,5,6,7,8,10,11,12,13)))
for report in limits:
    data=report['data']; before=data['before']
    assert not data['continuous_monitoring']
    for snap in (before,data.get('after',before)):
        if snap['valid']:
            raw=int(snap['raw'],16)
            assert len(snap['reasons'])==11
            for row in snap['reasons']:
                bit=reason_bits[row['key']]
                assert row['status_bit']==bit and row['log_bit']==bit+16
                assert row['active']==bool(raw & (1<<bit)) and row['logged']==bool(raw & (1<<(bit+16)))
        else:
            assert snap['raw'] is None and snap['reasons']==[]
    if report['command']=='intel-limits-clear':
        mask=0x3df30000 if data['reason']=='all' else 1<<(reason_bits[data['reason']]+16)
        assert int(data['selected_mask'],16)==mask and data['verified']==report['ok']
        if data['write_attempted']:
            assert int(data['submitted_raw'],16)==((int(before['raw'],16)|0x3df30000)&~mask)
        else:
            assert data['submitted_raw'] is None
        if report['ok']:
            assert not int(data['after']['raw'],16)&mask and not int(data['still_set_mask'],16)
            assert data['unchanged']==(not data['write_attempted'])
assert any(int(r['data'].get('still_set_mask','0'),16) for r in limits if not r['ok'])
thermal=[r for r in reports if r['command'] in ('intel-thermal-read','intel-thermal-set') and 'before' in r['data']]
assert len(thermal)==23 and [r['ok'] for r in thermal]==[True]*10+[False]*13
for report in thermal:
    data=report['data']; before=data['before']
    assert not data['hardware_effect_measured']
    for snap in (before,data.get('after',before)):
        if snap['valid']:
            raw=int(snap['raw'],16)
            assert snap['default_tcc_c']==((raw>>16)&255)
            assert snap['offset_c']==((raw>>24)&63)
            assert snap['activation_c']==snap['default_tcc_c']-snap['offset_c']
            assert snap['locked']==bool(raw & (1<<31))
            assert snap['offset_programmable']==bool(int(snap['platform_info_raw'],16) & (1<<30))
            assert snap['editable']==(snap['offset_programmable'] and not snap['locked'] and snap['activation_c']>0)
        else:
            assert all(snap[k] is None for k in ('raw','platform_info_raw','default_tcc_c','offset_c','activation_c','locked','offset_programmable'))
    if report['command']=='intel-thermal-set':
        assert data['verified']==report['ok']
        if report['ok']:
            expected=(int(before['raw'],16)&~(63<<24))|(data['requested_offset_c']<<24)
            assert int(data['after']['raw'],16)==expected==int(data['expected_raw'],16)
            assert data['unchanged']==(expected==int(before['raw'],16))
            assert data['write_attempted']==(not data['unchanged'])
avx=[r for r in reports if r['command'] in ('intel-avx-read','intel-avx-set') and 'before' in r['data']]
assert len(avx)==22 and [r['ok'] for r in avx]==[True]*9+[False]*13
for report in avx:
    data=report['data']; before=data['before']
    assert not data['hardware_effect_measured']
    for snap in (before,data.get('after',before)):
        if snap['valid']:
            assert snap['avx2_offset_bins']==((int(snap['raw'],16)>>5)&31)
            assert snap['editable']==(not snap['locked'])
        else:
            assert snap['raw'] is None and snap['avx2_offset_bins'] is None and snap['locked'] is None
    if report['command']=='intel-avx-set':
        assert data['verified']==report['ok']
        if report['ok']:
            expected=(int(before['raw'],16)&~0x3e0)|(data['requested_offset_bins']<<5)
            assert int(data['after']['raw'],16)==expected==int(data['expected_raw'],16)
            assert data['unchanged']==(expected==int(before['raw'],16))
            assert data['write_attempted']==(not data['unchanged'])

iccmax=[r for r in reports if r['command'] in ('intel-iccmax-read','intel-iccmax-set') and 'before' in r['data']]
assert len(iccmax)==28
assert [r['ok'] for r in iccmax]==[True]*6+[v for _ in range(6) for v in (True,False)]+[False]*10
for i,r in enumerate(iccmax):
    d=r['data']; b=d['before']
    assert not d['hardware_effect_measured'] and 'shared IA rail' in d['scope']
    if b['valid']:
        raw=int(b['raw'],16)
        assert b['encoded_limit_amps']==(raw & 2047)/4 and b['mode_bit31']==bool(raw & 0x80000000)
        assert b['ia_svid_address']==6 and b['topology_raw']=='0x0a060600'
        assert b['effective_limit_amps'] is None and b['unlimited_mode'] is None and not b['mode_semantics_verified']
    else:
        assert all(b[k] is None for k in ('raw','encoded_limit_amps','effective_limit_amps','mode_bit31','ia_svid_address','topology_raw','locked'))
    if r['command']=='intel-iccmax-set':
        assert d['verified']==r['ok']
        if r['ok']:
            assert d['after']['encoded_limit_amps']==d['requested_amps'] and int(d['expected_raw'],16)==int(d['after']['raw'],16)
            assert (int(b['raw'],16)&~2047)==(int(d['after']['raw'],16)&~2047)
assert iccmax[5]['data']['unchanged'] and not iccmax[5]['data']['write_attempted']
assert sum(r['data'].get('write_attempted',False) for r in iccmax)==6

bclkVoltage=[r for r in reports if r['command'] in ('intel-bclk-voltage-read','intel-bclk-voltage-set') and 'before' in r['data']]
assert len(bclkVoltage)==20 and [r['ok'] for r in bclkVoltage]==[True]*7+[False]*13
for report in bclkVoltage:
    data=report['data']; before=data['before']
    assert not data['hardware_effect_measured']
    for snap in (before,data.get('after',before)):
        if snap['valid']:
            assert snap['enabled']==bool(int(snap['raw'],16)&4)
            assert snap['editable']==(not snap['locked'])
        else:
            assert snap['raw'] is None and snap['enabled'] is None and snap['locked'] is None
    if report['command']=='intel-bclk-voltage-set':
        assert data['verified']==report['ok']
        if report['ok']:
            expected=(int(before['raw'],16)&~4)|(int(data['requested_enabled'])<<2)
            assert int(data['after']['raw'],16)==expected==int(data['expected_raw'],16)
            assert data['unchanged']==(expected==int(before['raw'],16))
            assert data['write_attempted']==(not data['unchanged'])

energy = [r for r in reports if r['command'] in ('intel-energy-read', 'intel-energy-set') and 'before' in r['data']]
assert len(energy) == 56 and [r['ok'] for r in energy] == [True]*28+[False]*28
for i, report in enumerate(energy):
    data = report['data']; before = data['before']
    assert data['msr'] == '0x000001fc' and not data['hardware_effect_measured']
    for snapshot in (before, data.get('after', before)):
        if snapshot['valid']:
            word = int(snapshot['raw'], 16)
            assert len(snapshot['raw']) == 18 and snapshot['editable']
            assert snapshot['efficient_turbo_enabled'] == (not bool(word & (1 << 19)))
            assert snapshot['race_to_halt_enabled'] == (not bool(word & (1 << 20)))
        else:
            assert not snapshot['editable']
            assert all(snapshot[k] is None for k in ('raw', 'efficient_turbo_enabled', 'race_to_halt_enabled'))
    if report['command'] == 'intel-energy-set':
        assert data['verified'] == report['ok']
        if report['ok']:
            mask = 1 << (19 if data['field'] == 'efficient-turbo' else 20)
            old = int(before['raw'],16); new = int(data['after']['raw'],16)
            assert new == (old & ~mask if data['requested_enabled'] else old | mask)
            assert int(data['expected_raw'],16) == new
            assert data['unchanged'] == (old == new) and data['write_attempted'] == (old != new)
            assert data['completed_writes'] == int(old != new)
        else:
            mode = (i-28) % 12
            assert data['write_attempted'] == (mode >= 8)
            assert data['completed_writes'] == int(mode in (8,10,11))
            assert (data['expected_raw'] is None) == (mode <= 4)
assert any(r['data'].get('expected_raw') == '0x0000000000000000' for r in energy)

tvb_edits = [r for r in reports if r['command'] in ('intel-tvb-table','intel-tvb-table-set') and 'before' in r['data']]
assert len(tvb_edits) == 28 and [r['ok'] for r in tvb_edits] == [True]*18+[False]*10
for i, report in enumerate(tvb_edits):
    data = report['data']; before = data['before']
    assert not data['hardware_effect_measured'] and not data['atomic_snapshot']
    if before['valid']:
        raw = int(before['raw'],16)
        values = (before['temperature_a_c'],before['down_bins_a'],before['temperature_b_c'],before['down_bins_b'])
        assert values == ((raw >> 8) & 255,raw & 255,(raw >> 16) & 255,raw >> 24)
        bounded = values[0] <= 100 and values[1] <= 10 and values[2] <= 100 and values[3] <= 10
        assert before['within_editing_profile'] == bounded
        assert before['editable'] == (bounded and before['configuration']['editable'])
    else:
        assert all(before[k] is None for k in ('raw','temperature_a_c','down_bins_a','temperature_b_c','down_bins_b','within_editing_profile'))
    if report['command'] == 'intel-tvb-table-set':
        assert data['verified'] == report['ok']
        if report['ok']:
            shift = {'temperature-a':8,'down-bins-a':0,'temperature-b':16,'down-bins-b':24}[data['field']]
            after = data['after']; old = int(before['raw'],16); new = int(after['raw'],16)
            assert new == ((old & ~(255 << shift)) | (data['requested_value'] << shift))
            assert after['configuration'] == before['configuration'] and after['active_cores'] == before['active_cores']
            assert data['unchanged'] == (new == old) and data['write_attempted'] == (new != old)
        else:
            assert data['write_attempted'] == (i in (22,25))
    else:
        assert not data['settings_write_attempted']
assert tvb_edits[25]['data']['firmware_status'] == 128
tvb_tables = [r for r in reports if r['command'] == 'intel-tvb-table' and 'rows' in r['data']]
assert len(tvb_tables) == 18 and [r['ok'] for r in tvb_tables] == [True]*10+[False]*8
for i, report in enumerate(tvb_tables):
    data = report['data']
    assert not data['settings_write_attempted'] and not data['hardware_effect_measured'] and not data['atomic_snapshot']
    assert data['selected_active_cores'] == (i if 1 <= i <= 8 else None)
    assert data['scan_completed'] == (i <= 11)
    if i <= 11:
        assert [r['active_cores'] for r in data['rows']] == ([i] if 1 <= i <= 8 else list(range(1,9)))
    if 12 <= i <= 15: assert not data['rows']
    if i == 16: assert len(data['rows']) == 4
    if i == 17: assert len(data['rows']) == 3
    for row in data['rows']:
        if row['valid']:
            raw = int(row['raw'],16)
            assert row['temperature_a_c'] == (raw >> 8) & 255
            assert row['down_bins_a'] == raw & 255
            assert row['temperature_b_c'] == (raw >> 16) & 255
            assert row['down_bins_b'] == raw >> 24
            assert row['query_attempted'] and row['query_completed'] and row['firmware_status'] == 0
        else:
            assert row['error'] and all(row[k] is None for k in ('raw','temperature_a_c','down_bins_a','temperature_b_c','down_bins_b'))
    if i == 10:
        assert not data['rows'][1]['valid'] and data['rows'][1]['firmware_status'] == 128
        assert data['rows'][-1]['valid']
    if i == 9:
        assert data['rows'][0]['raw'] == '0x00000000' and data['rows'][7]['raw'] == '0xffffffff'
tvb_reports = [r for r in reports if r["command"] in ("intel-tvb-read", "intel-tvb-set") and "before" in r["data"]]
assert len(tvb_reports) == 35 and [r["ok"] for r in tvb_reports] == [True]*18+[False]*17
for i, r in enumerate(tvb_reports):
    data = r["data"]; before = data["before"]
    assert not data["hardware_effect_measured"]
    if before["valid"]:
        raw = int(before["raw"],16)
        assert before["ratio_clipping_enabled"] == (not (raw & 4))
        assert before["voltage_optimization_enabled"] == (not (raw & 8))
        assert before["ring_down_bin_enabled"] == (not (raw & 2))
        assert before["editable"] == (not before["locked"])
    else:
        assert before["raw"] is None and before["ratio_clipping_enabled"] is None and before["voltage_optimization_enabled"] is None and before["ring_down_bin_enabled"] is None
    if r["command"] == "intel-tvb-set":
        assert data["verified"] == r["ok"]
        if r["ok"]:
            after = data["after"]; bit = {"ratio-clipping": 4, "voltage-optimization": 8, "ring-down-bin": 2}[data["field"]]
            assert (int(before["raw"],16) ^ int(after["raw"],16)) == (0 if data["unchanged"] else bit)
            assert (not (int(after["raw"],16) & bit)) == data["requested_enabled"]
        else:
            assert data["write_attempted"] == (i in (21,23,28,30))
assert tvb_reports[17]["data"]["before"]["locked"] and not tvb_reports[17]["data"]["before"]["editable"]
assert tvb_reports[33]["data"]["before"]["firmware_status"] == 0xfe
clock_reports = [r for r in reports if r["command"] in ("intel-clock-read","intel-clock-decode")]
assert len(clock_reports) == 20 and [r["ok"] for r in clock_reports] == ([True]*4+[False]*6)*2
for i,r in enumerate(clock_reports):
    clock = r["data"]["clock"]
    if i >= 10: assert not r["data"]["hardware_accessed"]
    if not r["ok"]:
        assert clock is None
        continue
    assert clock["format"] == "octool-intel-clock-v1" and len(clock["registers"]) == 3
    assert clock["offline"] == (i >= 10) and not clock["current_memory_frequency_available"]
    assert not clock["origin_verified"] and not clock["atomic_snapshot_guaranteed"]
    assert clock["bclk"]["reported_khz"] == (None if i in (1,13) else 101250)
    assert (clock["bclk"]["error"] != 0) == (i in (1,13))
    mrc = clock["last_mrc_request"]
    if i == 2:
        assert mrc["error"] < 0 and mrc["gear_ratio"] is None and mrc["vddq_tx_mv"] is None
    elif i == 3:
        assert not mrc["error"] and mrc["qclk_ratio_encoded"] == 0 and mrc["pll_shutdown_encoded"]
    else:
        assert not mrc["error"] and mrc["qclk_ratio_encoded"] == 32 and not mrc["pll_shutdown_encoded"]
        assert mrc["gear_ratio"] == 2 and mrc["reference_clock_code"] == 1
        assert mrc["vddq_tx_mv"] == 1150 and mrc["vddq_tx_iccmax_encoded"] == 9
with tempfile.TemporaryDirectory(prefix="octool-intel-clock-") as directory:
    path = Path(directory)/"时钟快照.json"
    for source in (clock_reports[0]["data"]["clock"],clock_reports[0],clock_reports[10],clock_reports[13]):
        expected = source.get("data",{}).get("clock",source)
        path.write_text(json.dumps(source),encoding="utf-8")
        decoded = run("intel-clock-decode","--file",str(path))["data"]
        assert not decoded["hardware_accessed"] and decoded["clock"]["offline"]
        assert decoded["clock"]["registers"] == expected["registers"]
        assert decoded["clock"]["bclk"] == expected["bclk"] and decoded["clock"]["last_mrc_request"] == expected["last_mrc_request"]
    path.write_text(json.dumps(clock_reports[4]),encoding="utf-8")
    assert run("intel-clock-decode","--file",str(path),code=3)["data"]["clock"] is None
    os.mkfifo(Path(directory)/"fifo")
    assert run("intel-clock-decode","--file",str(Path(directory)/"fifo"),code=3)["data"]["clock"] is None
imc_reports = [r for r in reports if r["command"] in ("intel-memory-read","intel-memory-decode")]
assert len(imc_reports) == 94
assert [r["ok"] for r in imc_reports] == ([True]*4+[False]*6)*2 + [True]*4+[False]*4 + [True]*6+[False]*6 + [True]*6+[False]*6 + [True]*8+[False]*8 + [True]*2+[False]*6 + [True]*7+[False]*5 + [True]*2+[False]*4
for i,r in enumerate(imc_reports[:20]):
    memory = r["data"]["memory"]
    if not r["ok"]:
        assert memory is None
        continue
    assert memory["format"] == "octool-intel-memory-v6"
    assert len(memory["registers"]) == 22 and len(memory["fields"]) == 83
    fields = {v["name"]:v for v in memory["fields"]}
    assert fields["tCL"]["encoded"] == 36 and fields["tWRPRE"]["encoded"] == 80
    assert fields["tWRRD_SG"]["encoded"] == 70 and fields["tWRRD_DD"]["encoded"] == 10
    assert fields["tREFI"]["encoded"] == 24000
    assert fields["RTL_Rank0"]["encoded"] == 40 and fields["RTL_Rank0"]["unit"] == "QCLK"
    assert fields["RTL_Rank1"]["encoded"] == 44 and fields["RTL_Rank2"]["encoded"] is None
    assert fields["RTL_Rank2"]["error"] < 0 and fields["RTL_Rank2"]["unit"] is None
    assert fields["tCSL"]["encoded"] == 9 and fields["tCSL"]["unit"] == "MC_DCLK"
    assert fields["tCSH"]["encoded"] == 11 and fields["tCSH"]["unit"] == "MC_DCLK"
    assert fields["tWRPDEN"]["encoded"] == 101 and fields["tCKE"]["encoded"] == 8
    assert fields["tCPDED"]["encoded"] == (5 if i == 2 else None)
    assert memory["registers"][4]["raw"] == "0x814160500042402a"
    assert fields["tCL"]["unit"] == ("encoded" if i in (3,13) else "tCK")
    assert fields["tREFIx9"]["encoded"] == 66 and fields["tREFIx9"]["unit"] == "1024_DCLK"
    assert fields["Refresh_block_wait"]["encoded"] == 6 and fields["Refresh_block_wait"]["unit"] == "MC_DCLK"
    assert fields["tXSDLL"]["encoded"] == 683 and fields["tXSDLL"]["unit"] == "encoded"
    assert fields["tZQCAL"]["encoded"] == (None if i == 2 else 1110)
    assert fields["tZQCAL"]["unit"] == (None if i == 2 else "encoded" if i in (3,13) else "2*tCK")
    assert fields["tZQCS"]["encoded"] == (293 if i == 2 else None)
    assert fields["tZQLAT"]["encoded"] == (None if i == 2 else 293)
    assert fields["tCKESR"]["encoded"] == (41 if i == 2 else None)
    assert fields["Self_refresh_tCSL"]["encoded"] == (None if i == 2 else 41)
    assert memory["offline"] == (i >= 10) and not memory["origin_verified"] and not memory["atomic_snapshot_guaranteed"]
    if i >= 10: assert not r["data"]["hardware_accessed"]
for i,r in enumerate(imc_reports[20:28]):
    assert not r["data"]["hardware_accessed"]
    memory = r["data"]["memory"]
    if i >= 4:
        assert memory is None
        continue
    version = 1 if i < 2 else 2
    assert memory["format"] == "octool-intel-memory-v"+str(version) and memory["offline"]
    assert len(memory["registers"]) == (12 if version == 1 else 14)
    assert len(memory["fields"]) == (30 if version == 1 else 43)
    fields = {v["name"]:v for v in memory["fields"]}
    if i < 2:
        assert "RTL_Rank0" not in fields and fields["tCL"]["encoded"] == 36
    if i == 2:
        assert memory["rank_mask"] == 8 and fields["RTL_Rank3"]["encoded"] == 52
        assert all(fields["RTL_Rank"+str(rank)]["encoded"] is None for rank in range(3))
    if i == 3:
        assert fields["tCPDED"]["encoded"] == 0 and fields["tCPDED"]["unit"] == "encoded"
        assert fields["RTL_Rank0"]["unit"] == "QCLK" and fields["tCSL"]["unit"] == "MC_DCLK"
for i,r in enumerate(imc_reports[28:40]):
    assert not r["data"]["hardware_accessed"]
    memory = r["data"]["memory"]
    if i >= 6:
        assert memory is None
        continue
    assert memory["format"] == "octool-intel-memory-v3" and memory["offline"]
    assert len(memory["registers"]) == 18 and len(memory["fields"]) == 57
    fields = {v["name"]:v for v in memory["fields"]}
    assert fields["tREFIx9"]["encoded"] == (0 if i == 2 else 66)
    assert fields["tREFIx9"]["unit"] == "1024_DCLK"
    assert fields["Refresh_block_wait"]["unit"] == "MC_DCLK"
    assert fields["tXSDLL"]["unit"] == "encoded"
    assert fields["tZQCAL"]["unit"] == (None if i == 1 else "encoded" if i == 3 else "2*tCK")
    assert fields["tZQCS"]["encoded"] == (293 if i == 1 else None)
    assert fields["tZQLAT"]["encoded"] == (None if i == 1 else 0 if i == 2 else 293)
    assert fields["tCKESR"]["encoded"] == (41 if i == 1 else None)
    assert fields["Self_refresh_tCSL"]["encoded"] == (None if i == 1 else 0 if i == 2 else 41)
    assert memory["registers"][17]["raw"] == ("0x0000000000000000" if i == 2 else "0xe290000000001234")
for i,r in enumerate(imc_reports[40:52]):
    assert not r["data"]["hardware_accessed"]
    memory = r["data"]["memory"]
    if i >= 6:
        assert memory is None
        continue
    assert memory["format"] == "octool-intel-memory-v4" and memory["offline"]
    assert len(memory["registers"]) == 19 and len(memory["fields"]) == 68
    fields = {v["name"]:v for v in memory["fields"]}
    expected = dict(zip(("self_refresh_enable","power_down_enable","idle_enable","CKE_valid_enable",
                         "CKE_valid_length","self_refresh_length","self_refresh_latency","power_down_length",
                         "power_down_latency","idle_length","raise_CKE_after_exit"), (1,0,1,1,9,3,21,6,10,9,1)))
    for name,value in expected.items():
        assert fields["DDRIO_"+name]["encoded"] == (0 if i == 2 else value)
    assert fields["DDRIO_CKE_valid_length"]["unit"] == "DCLK"
    assert fields["DDRIO_self_refresh_length"]["unit"] == "MC_DCLK"
    assert fields["DDRIO_self_refresh_latency"]["unit"] == ("encoded" if i == 3 else "8*tCK")
    assert fields["DDRIO_power_down_latency"]["unit"] == ("encoded" if i == 3 else "tCK")
    assert fields["DDRIO_idle_length"]["unit"] == "encoded"
    assert memory["registers"][18]["raw"] == ("0x0000000000000000" if i == 2 else "0x00000000b9355953")
for i,r in enumerate(imc_reports[52:76]):
    memory = r["data"]["memory"]
    if i < 16: assert not r["data"]["hardware_accessed"]
    if not r["ok"]:
        assert memory is None
        continue
    assert memory["format"] == ("octool-intel-memory-v5" if i < 16 else "octool-intel-memory-v6") and memory["offline"] == (i < 16)
    assert len(memory["registers"]) == (21 if i < 16 else 22) and len(memory["fields"]) == (79 if i < 16 else 83)
    assert not memory["origin_verified"] and not memory["atomic_snapshot_guaranteed"]
    assert memory["dram_type"] == ("DDR4" if i in (1,17) else "DDR5")
    assert memory["controller"] == (1 if i >= 16 else 0) and memory["channel"] == (1 if i == 16 else 0)
    assert memory["rank_mask"] == (8 if i in (4,16) else 15 if i == 5 else 3)
    fields = {v["name"]:v for v in memory["fields"]}
    for command,masks in (("read",(2,1,8,4)),("write",(14,13,11,7))):
        for rank,mask in enumerate(masks):
            field = fields[f"ODT_{command}_rank{rank}_mask"]
            present = bool(memory["rank_mask"] & (1 << rank))
            assert field["encoded"] == (None if not present else 0 if i in (2,16) else mask)
            assert field["unit"] == ("odt_pin_mask" if present else None)
            assert (field["error"] == 0) == present
    for name,value,unit in (("add_tCWL",45,"DCLK"),("subtract_tCWL",13,"DCLK"),("add_1QCLK",1,"flag")):
        field = fields["Write_delay_"+name]
        assert field["encoded"] == (0 if i in (2,16) else value) and field["unit"] == unit and field["error"] == 0
    assert fields["tCWL"]["encoded"] == 34 and fields["tCWL"]["unit"] == ("encoded" if i == 3 else "tCK")
    assert memory["registers"][19] == dict(offset="0x0000e080",raw="0x0000000000000000" if i in (2,16) else "0x000000007bde4812")
    assert memory["registers"][20] == dict(offset="0x0000e478",raw="0x0000000000000000" if i in (2,16) else "0x00000000a5a51b4d")
for i,r in enumerate(imc_reports[76:94]):
    memory = r["data"]["memory"]
    if i < 12: assert not r["data"]["hardware_accessed"]
    if not r["ok"]:
        assert memory is None
        continue
    assert memory["format"] == "octool-intel-memory-v6"
    assert len(memory["registers"]) == 22 and len(memory["fields"]) == 83
    assert not memory["temperature_freshness_verified"] and memory["offline"] == (i < 12)
    assert memory["controller"] == (1 if i >= 12 else 0) and memory["channel"] == 0
    fields = {v["name"]:v for v in memory["fields"]}
    assert fields["tCL"]["encoded"] == 36
    for rank in range(4):
        field = fields["DDR4_temperature_rank"+str(rank)]
        code = 0 if i in (2,13) else rank
        absent = i == 3 and rank != 2
        unavailable = i == 1 or absent or code == 3
        assert field["encoded"] == (None if unavailable else code)
        assert field["unit"] == (None if unavailable else "temperature_class")
        assert field["temperature_class"] == (None if unavailable else ("cold","normal","hot")[code])
        assert field["required_refresh_multiplier"] == (None if unavailable else 2 if code == 2 else 1)
        assert (field["error"] < 0) == unavailable
    assert memory["registers"][-1]["raw"] == ("0x0000000000000000" if i in (2,13) else "0x0000000003020100")
with tempfile.TemporaryDirectory(prefix="octool-intel-memory-") as directory:
    path = Path(directory)/"内存快照.json"
    for source in (imc_reports[0]["data"]["memory"],imc_reports[0],imc_reports[10]):
        path.write_text(json.dumps(source),encoding="utf-8")
        decoded = run("intel-memory-decode","--file",str(path))
        assert decoded["data"]["memory"]["registers"] == imc_reports[0]["data"]["memory"]["registers"]
        assert decoded["data"]["memory"]["fields"][0]["encoded"] == 36
    for source in imc_reports[20:24]+imc_reports[28:34]+imc_reports[40:46]+imc_reports[52:60]:
        path.write_text(json.dumps(source),encoding="utf-8")
        decoded = run("intel-memory-decode","--file",str(path))
        assert decoded["data"]["memory"] == source["data"]["memory"]
    path.write_text(json.dumps(imc_reports[4]),encoding="utf-8")
    assert run("intel-memory-decode","--file",str(path),code=3)["data"]["memory"] is None
    os.mkfifo(Path(directory)/"fifo")
    assert run("intel-memory-decode","--file",str(Path(directory)/"fifo"),code=3)["data"]["memory"] is None
pm_reports = [r for r in reports if r["command"] == "amd-pm-decode" and "pm" in r["data"]]
assert len(pm_reports) == 94
for report in reports:
    pm = report.get('data',{}).get('pm')
    if not pm or pm.get('format') != 'octool-amd-pm':
        continue
    assert not pm['firmware_arrays_units_verified'] and not pm['firmware_arrays_targets_verified']
    assert not pm['firmware_arrays_labels_verified']
    if not pm['fields']:
        assert not pm['firmware_core_arrays'] and not pm['firmware_ccd_arrays']
        assert pm['firmware_ccd_storage_slots'] == 0
        continue
    if pm['platform'] == 'granite-ridge':
        n = {1828:1,2452:2}[pm['captured_bytes']]
        first_core, ccd_rows = 0x4b4, 12
        start = first_core+18*8*n*4
        assert pm['firmware_arrays_layout'] == 'granite-ridge-18-core-12-ccd-rows'
        assert len(pm['fields'])==22
        assert all('candidate' in f['name'] and f['unit'] == 'unverified' for f in pm['fields'][:7])
        raw=bytes.fromhex(pm['raw_hex'])
        for j,f in enumerate(pm['fields'][7:]):
            channel,kind=divmod(j,5); off=4*(48+5*channel+kind)
            assert f['group']==('granite_command_mirror' if kind==0 else 'granite_svi') and f['firmware_channel']==channel
            if kind==0: assert f['software_command_mirror'] and not f['measurement']
            assert f['unit']==('V','V','A','W','deg C')[kind] and int(f['offset'],16)==off
            assert f['unit_evidence']=='granite-smu-98.84.0-static'
            assert not any(f[k] for k in ('capture_firmware_verified','physical_rail_verified','freshness_verified','setting_readback_verified'))
            assert not f['clock'] and f['scaled_clock_mhz'] is None
            assert f['stored_value']==struct.unpack_from('<f',raw,off)[0]
    else:
        assert not any('VDDG' in f['name'] for f in pm['fields'])
        n,start = {2844:(2,0xab4),4100:(4,0xf34),5356:(6,0x13b4),6612:(8,0x1834),
                   7868:(10,0x1cb4),9124:(12,0x2134),10380:(14,0x25b4),11636:(16,0x2a34)}[pm['captured_bytes']]
        first_core, ccd_rows = 0x634, 13
    assert pm['firmware_ccd_storage_slots'] == n
    raw = bytes.fromhex(pm['raw_hex']); seen=set()
    for key,count,slots,first in (('firmware_core_arrays',18,8*n,first_core),('firmware_ccd_arrays',ccd_rows,n,start)):
        rows=pm[key]; assert len(rows)==count*slots and int(rows[0]['offset'],16)==first
        for j,f in enumerate(rows):
            off=int(f['offset'],16); bits=int(f['raw'],16)
            assert off not in seen and first_core<=off and off+4<=len(raw); seen.add(off)
            assert bits==struct.unpack_from('<I',raw,off)[0]
            assert f['row']==j//slots and f['slot']==j%slots and f['slot_count']==slots
            units=({1:'V',2:'deg C',5:'%',6:'%',7:'%',8:'code',11:'code',13:'ratio',14:'ratio',15:'%',16:'%',17:'%'} if key=='firmware_core_arrays'
                   else {3:'deg C',4:'deg C',7:'code',11:'%'}) if pm['platform']=='granite-ridge' else {}
            known=f['row'] in units
            assert f['unit']==units.get(f['row'],'unverified')
            assert not f['clock'] and f['scaled_clock_mhz'] is None
            if pm['platform']=='granite-ridge' and key=='firmware_core_arrays':
                assert f['firmware_ccd_storage_slot']==f['slot']//8
                assert f['firmware_local_core_slot']==f['slot']%8
            if f['unit']=='code':
                assert not f['code_meaning_verified'] and not f['freshness_verified']
            if known:
                assert f['unit_evidence']=='granite-mp5-98.84.0-static' and not f['capture_firmware_verified']
            counter = pm['platform']=='granite-ridge' and ((key=='firmware_core_arrays' and f['row']>=13) or
                                                         (key=='firmware_ccd_arrays' and f['row']==11))
            if counter:
                assert not f['counter_meaning_verified'] and not f['freshness_verified']
            else:
                assert 'counter_meaning_verified' not in f
            if (bits & 0x7f800000)==0x7f800000:
                assert f['stored_value'] is None and f['error']==-33
            else:
                assert f['stored_value']==struct.unpack_from('<f',raw,off)[0] and not f['error']
    assert len(seen)==(len(raw)-first_core)//4
array_reports = pm_reports[72:]
fabric_reports, package_reports, limit_reports, pm_reports = pm_reports[56:72], pm_reports[38:56], pm_reports[20:38], pm_reports[:20]
assert [r["ok"] for r in pm_reports] == [True] * 9 + [False, True, True, False, False, False, False, True, True, False, False]
for i, r in enumerate(pm_reports):
    assert r["data"]["offline"] and not r["data"]["table_read_attempted"] and not r["data"]["direct_register_access"]
    pm = r["data"]["pm"]
    if r["ok"]:
        assert len(pm["fields"]) == 22 and len(pm["raw_hex"]) == pm["expected_bytes"] * 2
        fields = {v["name"]: v for v in pm["fields"]}
        assert fields["FCLK"]["stored_value"] == 2000 and fields["FCLK"]["raw"] == "0x44fa0000"
        assert fields["VDD current"]["stored_value"] == 100 and fields["VDD current"]["unit"] == "A"
        assert fields["Misc VRM temperature"]["stored_value"] == 50
        assert fields["FCLK"]["scaled_clock_mhz"] == (2050 if i in (8,16) else None)
    elif i == 9:
        assert pm["fields"][0]["stored_value"] is None and pm["fields"][0]["raw"] == "0x7f800001"
        assert pm["fields"][1]["stored_value"] == 1800
    else:
        assert pm["fields"] == []
limit_names = ['Fast PPT Limit','Fast PPT Value','TDC0 Limit','TDC0 Current Value',
    'TDC1 Limit','TDC1 Current Value','THM0 Limit','THM0 Current Value',
    'THM1 Limit','THM1 Current Value','FIT Limit','FIT Current Value',
    'VID0 Limit','VID0 Current Value','VID1 Limit','VID1 Current Value',
    'EDC_LIMIT_0','PEAK_TELEMETRY_CURRENT_0','PCC_LIMIT_0','PCC_RESIDENCY_0',
    'EDC_LIMIT_1','PEAK_TELEMETRY_CURRENT_1','PCC_LIMIT_1','PCC_RESIDENCY_1']
limit_values = [400.5,350.25,100,63.5,150,74.25,95,82.5,80,71.25,1,0.25,1.5,1.25,1.375,1.125,
    200,180.5,4,0.75,250,220.25,5,0.5]
limit_offsets = list(range(0,64,4))+list(range(0x1c8,0x1e8,4))
assert [r['ok'] for r in limit_reports] == [True]*8+[False]*4+[True]*5+[False]
for i, report in enumerate(limit_reports):
    pm = report['data']['pm']
    assert report['data']['offline'] and not report['data']['table_read_attempted'] and not report['data']['direct_register_access']
    assert pm['original_limits_units_verified'] is False
    if i == 17:
        assert pm['original_limits'] == [] and pm['fields'] == []
        continue
    fields = pm['original_limits']
    assert len(fields) == 24 and len(pm['fields']) == 22 and all(f['error'] == 0 for f in pm['fields'])
    assert [f['name'] for f in fields] == limit_names
    assert [int(f['offset'],16) for f in fields] == limit_offsets
    for n, field in enumerate(fields):
        assert field['unit'] == 'unverified' and field['clock'] is False and field['scaled_clock_mhz'] is None and field['scale_error'] == 0
        if 8 <= i <= 11 and n == [0,15,16,23][i-8]:
            assert field['stored_value'] is None and field['error'] == -33
            assert field['raw'] == ['0x7f800000','0xff800000','0x7fc12345','0x7f800001'][i-8]
        elif i == 12 and n < 5:
            raw = [0,0x80000000,0xbf800000,1,0x7f7fffff][n]
            assert field['error'] == 0 and int(field['raw'],16) == raw
            assert field['stored_value'] == struct.unpack('<f',struct.pack('<I',raw))[0]
        else:
            assert field['error'] == 0 and field['stored_value'] == limit_values[n]
    assert pm['fields'][0]['scaled_clock_mhz'] == (2465 if i == 16 else None)
package_offsets = list(range(0x60,0x7c,4))+list(range(0x40,0x60,4))+list(range(0x7c,0xb4,4))+list(range(0xd0,0x130,4))
package_groups = ['power']*7+['workload']*8+['power_model']*4+['voltage_policy']*10+['limit_contributors']*24
assert [r['ok'] for r in package_reports] == [True]*8+[False]*5+[True]*4+[False]
for i, report in enumerate(package_reports):
    pm=report['data']['pm']
    assert report['data']['offline'] and not report['data']['table_read_attempted'] and not report['data']['direct_register_access']
    assert pm['original_package_units_verified'] is False and pm['original_package_targets_verified'] is False
    if i==17:
        assert pm['original_package']==[] and pm['original_limits']==[] and pm['fields']==[]
        continue
    fields=pm['original_package']; assert len(fields)==53
    assert [int(f['offset'],16) for f in fields]==package_offsets
    assert [f['group'] for f in fields]==package_groups
    assert fields[0]['name']=='CPU Power' and fields[-1]['name']=='PPT_APU1'
    for n,f in enumerate(fields):
        assert f['unit']=='unverified' and not f['clock'] and not f['scale_error'] and f['scaled_clock_mhz'] is None
        if 8<=i<13 and n==[0,7,15,19,29][i-8]:
            assert f['stored_value'] is None and f['raw']=='0x7f800001' and f['error']==-33
        else:
            assert f['stored_value']==10+n/4 and not f['error']
    assert pm['fields'][0]['scaled_clock_mhz']==(2465 if i==16 else None)
    assert all(not f['error'] for f in pm['fields']+pm['original_limits'])
fabric_offsets = list(range(0x154,0x178,4))+list(range(0x24c,0x294,4))
fabric_names = ['FCLK_PCIE_UP_THRESHOLD','FCLK_PCIE_BANDWIDTH','FCLK_PCIE_DOWN_THRESHOLD',
    'FCLK_XGMI_UP_THRESHOLD','FCLK_XGMI_BANDWIDTH','FCLK_XGMI_DOWN_THRESHOLD',
    'FCLK_CXL_UP_THRESHOLD','FCLK_CXL_BANDWIDTH','FCLK_CXL_DOWN_THRESHOLD',
    'SMNCLK','SMNCLK_EFF','WAFLCLK','WAFLCLK_EFF','MP0CLK','MP0CLK_EFF',
    'MP1CLK','MP1CLK_EFF','MPIOCLK','MPIOCLK_EFF','ACLK','ACLK_EFF','SVI3CLK','SVI3CLK_EFF',
    'MPDMA_TF_CLK','MPDMA_TF_CLK_EFF','MPDMA_PM_CLK','MPDMA_PM_CLK_EFF']
assert [r['ok'] for r in fabric_reports] == [True]*8+[False]*2+[True]*5+[False]
for i, report in enumerate(fabric_reports):
    pm=report['data']['pm']
    assert report['data']['offline'] and not report['data']['table_read_attempted'] and not report['data']['direct_register_access']
    assert pm['original_fabric_units_verified'] is False and pm['original_fabric_targets_verified'] is False
    fields=pm['original_fabric']
    if i==15:
        assert fields==[] and pm['fields']==[] and pm['original_package']==[] and pm['original_limits']==[]
        continue
    assert [f['name'] for f in fields]==fabric_names
    assert [int(f['offset'],16) for f in fields]==fabric_offsets
    assert [f['group'] for f in fields]==['link_bandwidth']*9+['internal_clocks']*18
    for n,f in enumerate(fields):
        assert f['unit']=='unverified' and not f['clock'] and not f['scale_error'] and f['scaled_clock_mhz'] is None
        if (i,n) in ((8,0),(9,9)):
            assert f['stored_value'] is None and f['raw']=='0x7f800001' and f['error']==-33
        elif i==10 and n==26:
            assert f['stored_value']==0 and f['raw']=='0x80000000' and not f['error']
        else:
            assert f['stored_value']==20+n/2 and not f['error']
    assert pm['fields'][0]['scaled_clock_mhz']==(2465 if i==14 else None)
    assert all(not f['error'] for f in pm['fields']+pm['original_limits']+pm['original_package'])
array_names = ['GMI_FULL_WIDTH','GMI_HALF_WIDTH','GMI_QUARTER_WIDTH','DIMM_POWER','DIMM_TEMP',
    'PCIE_ALLOWED','PCIE_ACTIVE','PCIE_UP_THRESHOLD','PCIE_DOWN_THRESHOLD','PCIE_IOHC_BANDWIDTH[4]']
array_widths = [12,12,12,24,24,1,1,1,1,4]
array_labels = [name for name,width in zip(array_names,array_widths) for _ in range(width)]
array_slots = [slot for width in array_widths for slot in range(width)]
assert [r['ok'] for r in array_reports] == [True]*8+[False]*3+[True]*6+[False]+[True]*3+[False]
for i, report in enumerate(array_reports):
    pm=report['data']['pm']
    assert report['data']['offline'] and not report['data']['table_read_attempted'] and not report['data']['direct_register_access']
    assert pm['original_arrays_units_verified'] is False and pm['original_arrays_targets_verified'] is False
    if i in (17,21):
        assert all(pm[key]==[] for key in ('fields','original_limits','original_package','original_fabric','original_arrays'))
        continue
    fields=pm['original_arrays']
    assert [f['name'] for f in fields]==array_labels
    assert [int(f['offset'],16) for f in fields]==list(range(0x294,0x404,4))
    assert [f['slot'] for f in fields]==array_slots
    assert [f['slot_count'] for f in fields]==[width for width in array_widths for _ in range(width)]
    assert [f['group'] for f in fields]==['gmi_storage']*36+['dimm_storage']*48+['pcie_storage']*8
    for n,f in enumerate(fields):
        assert f['unit']=='unverified' and not f['clock'] and not f['scale_error'] and f['scaled_clock_mhz'] is None
        if (i,n) in ((8,0),(9,60),(10,91)):
            assert f['stored_value'] is None and f['raw']=='0x7f800001' and f['error']==-33
        elif i==11 and n==91:
            assert f['stored_value']==0 and f['raw']=='0x80000000' and not f['error']
        elif i>=18:
            assert f['raw']=='0xff7fffff' and f['stored_value']==struct.unpack('<f',bytes.fromhex('ffff7fff'))[0]
        else:
            assert f['stored_value']==64+n and not f['error']
    assert all(not f['error'] for f in pm['fields']+pm['original_limits']+pm['original_package']+pm['original_fabric'])
    if i<18:
        assert pm['fields'][0]['scaled_clock_mhz']==(2465 if i==16 else None)
with tempfile.TemporaryDirectory(prefix="octool-pm-") as directory:
    root = Path(directory)
    capture = pm_reports[8]["data"]["pm"]
    path = root / '快照-".json'
    path.write_text(json.dumps(capture), encoding="utf-8")
    decoded = run("amd-pm-decode", "--file", str(path))
    assert decoded["data"]["pm"] == capture
    path.write_text(json.dumps(decoded), encoding="utf-8")
    assert run("amd-pm-decode", "--file", str(path))["data"]["pm"] == capture
    for source in (limit_reports[0], limit_reports[11], limit_reports[12], limit_reports[16],
                   package_reports[0], package_reports[8], package_reports[16],
                   fabric_reports[0], fabric_reports[8], fabric_reports[10], fabric_reports[14],
                   array_reports[0],array_reports[9],array_reports[11],array_reports[16],array_reports[18],array_reports[19]):
        path.write_text(json.dumps(source),encoding='utf-8')
        decoded = run('amd-pm-decode','--file',str(path),code=0 if source['ok'] else 3)
        assert decoded['data']['pm'] == source['data']['pm']
    complete=json.dumps(array_reports[18]).encode('utf-8')
    assert 65536<len(complete)<1048576
    path.write_bytes(complete+b' '*(1048576-len(complete)))
    assert run('amd-pm-decode','--file',str(path))['data']['pm']==array_reports[18]['data']['pm']
    path.write_bytes(path.read_bytes()+b' ')
    assert run('amd-pm-decode','--file',str(path),code=3)['error']==-27
    raw = root / "raw.bin"; raw.write_bytes(bytes.fromhex(capture["raw_hex"]))
    decoded = run("amd-pm-decode", "--file", str(raw), "--version", capture["version"])
    assert decoded["data"]["pm"]["fields"][0]["scaled_clock_mhz"] is None
    for dest in (root, root / "bad", root / "huge"):
        dest.mkdir(exist_ok=True)
        (dest / "pm_table_version").write_bytes(struct.pack("<I", 0x730204))
        (dest / "pm_table_size").write_bytes(struct.pack("<Q", 2844))
    (root / "pm_table").write_bytes(raw.read_bytes())
    os.mkfifo(root / "bad/pm_table")
    (root / "huge/pm_table").write_bytes(raw.read_bytes() + b"\0")
    subprocess.run([fixture,"--pm-fixture",str(root)],check=True,timeout=15,env=env)
    run("amd-pm-decode", "--file", str(root / "bad/pm_table"), "--version", "0x730204", code=3)
    if not Path("/sys/kernel/ryzen_smu_drv").exists():
        absent = run("amd-pm-read", code=3)
        assert absent["error"] == -2 and not absent["data"]["table_read_attempted"]
        assert absent["data"]["pm"]["fields"] == []
spd_reports = [r for r in reports if r["command"] == "spd-decode" and "spd" in r["data"]]
assert len(spd_reports) == 101
assert [r["ok"] for r in spd_reports[:68]] == [True, False, False, False, True, False, True, True, False,
                                        True, False, False, False, True, False, True, False, False, False,
                                        True, True, False, False, False, False, True, True, False, False,
                                        True, True, False, True, True,
                                        True, True, True, False, True, True, True, False,
                                        True, True, True, True, True, True, False,
                                        True, True, True, True, False,
                                        True, True, False, False, True, True, True, True, True, False,
                                        True, True, True, True]
for i, r in enumerate(spd_reports):
    xmp = r["data"]["spd"]["xmp"]
    assert not xmp["active_configuration_measured"]
    if i in (0, 1, 3, 6, 7):
        assert len(xmp["profiles"]) == 3 and xmp["crc"]["valid"]
        for p in xmp["profiles"]:
            if p["error"] or not p["enabled"]:
                assert p["values"] == []
            else:
                assert p["crc"]["valid"] and len(p["raw_hex"]) == 128
                assert len(p["values"]) == 30
    if i == 1:
        assert r["data"]["spd"]["crc_valid"] and xmp["profiles"][0]["error"] == -84
        assert xmp["profiles"][1]["values"]
    if i == 4:
        assert not xmp["inspected"] and xmp["present"] is None
    if i == 6:
        assert xmp["expo_present"] and xmp["profiles"][2]["blocked_by_expo"]
    if i == 7:
        assert xmp["profiles"][0]["name"] is None and not xmp["profiles"][0]["name_valid"]
values = {v["name"]: v for v in spd_reports[0]["data"]["spd"]["xmp"]["profiles"][0]["values"]}
assert values["VDD"] == {"name": "VDD", "value": 1250, "unit": "mV"}
assert values["tRFC1 minimum"] == {"name": "tRFC1 minimum", "value": 295, "unit": "ns"}
for i, r in enumerate(spd_reports):
    expo = r["data"]["spd"]["expo"]
    assert not expo["active_configuration_measured"]
    assert expo["enhanced_timings_decoded"] == any(p["stored_enhanced_timings"] for p in expo["profiles"])
    for p in expo["profiles"]:
        assert p["enhanced_timings_enabled"] is None
        assert len(p["stored_enhanced_timings"]) == (8 if p["enabled"] and not p["error"] else 0)
        assert all(v["unit"] == "ps" for v in p["stored_enhanced_timings"])
    if i == 4:
        assert not expo["inspected"] and expo["present"] is None and expo["revision"] is None
    if i == 0:
        assert expo["inspected"] and expo["present"] is False
    if i in (10, 11, 12):
        assert expo["profiles"] == [] and expo["error"] == {10: -84, 11: -90, 12: -95}[i]
        assert len(r["data"]["spd"]["xmp"]["profiles"][0]["values"]) == 30
    if i == 11:
        assert expo["raw_hex"] is None and expo["crc"]["valid"] is None and expo["configuration_raw"] is None
    if i in (6, 9, 13, 14, 15, 16, 17, 18):
        assert expo["present"] and expo["crc"]["valid"] and expo["revision"] == 16 and len(expo["raw_hex"]) == 256
        assert len(expo["profiles"]) == 2
        for slot, p in enumerate(expo["profiles"]):
            assert p["index"] == slot+1 and p["offset"] == 842+40*slot and len(p["raw_hex"]) == 80
            assert p["enabled"] == bool(expo["configuration_raw"] & (1 << (slot*4)))
            if p["enabled"] and not p["error"]:
                assert len(p["values"]) == 13
                values = {v["name"]: v for v in p["values"]}
                assert values["VDD"] == {"name": "VDD", "value": 1250-50*slot, "unit": "mV"}
                assert values["tCK minimum"]["value"] == 333+24*slot and values["tCK minimum"]["unit"] == "ps"
                assert values["tRFC1 minimum"]["value"] == 295 and values["tRFC1 minimum"]["unit"] == "ns"
            else:
                assert p["values"] == []
    if i in (14, 18):
        assert expo["error"] == -22 and expo["profiles"][int(i == 18)]["error"] == -22
    if i in (16, 17):
        assert expo["error"] == 0 and r["data"]["spd"]["xmp"]["error"] != 0
for i in range(19, 29):
    data = spd_reports[i]["data"]["spd"]
    xmp = data["xmp"]
    assert data["memory_type"] == 12 and data["crc_valid"]
    assert not data["expo"]["inspected"] and data["expo"]["present"] is None
    assert xmp["crc_supported"] is (None if i == 26 else False)
    assert xmp["crc"] == {"checked": False, "valid": None, "stored": None, "computed": None}
    assert xmp["header_captured"] == (i not in (25, 26, 28))
    assert xmp["revision"] == (None if i in (25, 26, 28) else 33 if i == 23 else 32)
    assert xmp["present"] is (False if i == 25 else None if i == 26 else True)
    assert xmp["error"] == {21: -95, 22: -90, 23: -95, 24: -22, 27: -95, 28: -90}.get(i, 0)
    if i in (23, 25, 26, 28):
        assert xmp["profiles"] == []
        continue
    assert len(xmp["profiles"]) == 2 and len(xmp["raw_hex"]) == 18
    for slot, p in enumerate(xmp["profiles"]):
        assert p["index"] == slot+1 and p["offset"] == 393+47*slot
        assert not p["name_supported"] and p["name_valid"] is None and p["name"] is None
        assert not p["crc"]["checked"] and p["crc"]["valid"] is None
        assert p["enabled"] == (i != 20 or slot == 1)
        assert p["raw_hex"] is None if i == 22 and slot == 1 else len(p["raw_hex"]) == 94
        assert p["timebase_raw"] == ({20: 15, 21: 1}.get(i, 0) if slot == 0 else 4 if i == 27 else 0)
        if p["error"] or not p["enabled"]:
            assert p["values"] == []
        else:
            expected = ([1350, 625, 9999, 11001, 11998, 36000, 51975, 350000, 260000, 160000, 30000, 3997, 6004],
                        [1200, 667, 10124, 11126, 12123, 36125, 52100, 350125, 260125, 160125, 30125, 4122, 6129])[slot]
            assert [v["value"] for v in p["values"]] == expected
            assert [v["unit"] for v in p["values"]] == ["mV"] + ["ps"]*12
            assert p["values"][9]["name"] == "tRFC4 minimum"
secondary = ["tRRD_L", "tCCD_L_WR", "tCCD_L_WR2", "tCCD_L_WTR", "tCCD_S_WTR", "tCCD_L", "tRTP", "tFAW"]
times = [5000, 20000, 10000, 10000, 2500, 5000, 7500, 10000]
limits = [8, 32, 16, 16, 4, 8, 12, 32]
for i in (0, 29, 30, 31, 32, 33):
    xmp = spd_reports[i]["data"]["spd"]["xmp"]
    for slot, profile in enumerate(xmp["profiles"]):
        if slot == 0 and i in (31, 32):
            assert profile["values"] == []
            assert profile["error"] == (-84 if i == 31 else 0)
            assert profile["enabled"] is (i == 31)
            continue
        assert profile["crc"]["valid"] and len(profile["values"]) == 30
        values = {v["name"]: v for v in profile["values"]}
        for field, name in enumerate(secondary):
            ps, ck = times[field]+17*slot, limits[field]+slot
            if slot == 0 and i in (29, 30):
                ps, ck = (0, 0) if i == 29 else (65535, 255)
            if slot == 0 and i == 33 and field == 0:
                ps, ck = 1, 255
            assert values[name+" minimum"] == {"name": name+" minimum", "unit": "ps", "value": ps}
            assert values[name+" cycle lower limit"] == {"name": name+" cycle lower limit", "unit": "nCK", "value": ck}
        assert values["tRFC1 minimum"]["unit"] == "ns" and values["tRFC1 minimum"]["value"] == 295
for i, report in enumerate(spd_reports[:54]):
    data = report["data"]["spd"]
    cas = data["cas"]
    if i in (8, 41, 48, 53):
        assert cas is None
    elif i in (38, 46):
        assert cas == {"decoded": False, "offset": 24, "raw_hex": "8101000080", "encoding": None, "reserved_bits_set": None, "latencies_nck": None}
    elif data["memory_type"] == 12:
        assert cas["decoded"] and cas["offset"] == 20
        assert cas["reserved_bits_set"] is (i == 40)
        assert cas["latencies_nck"] == ([23, 32, 46, 52] if i == 39 else [7, 16, 30, 36])
    else:
        assert cas["decoded"] and cas["offset"] == 24 and not cas["reserved_bits_set"]
        assert cas["latencies_nck"] == ([] if i == 34 else [20, 34, 36, 98])
    for slot, profile in enumerate(data["xmp"]["profiles"]):
        assert profile["recommended_dimms_per_channel"] == (1 if data["memory_type"] == 12 and profile["enabled"] else None)
        if profile["error"] or not profile["enabled"]:
            assert profile["cas"] is None and profile["command_rate"] is None
            continue
        if data["memory_type"] == 12:
            assert profile["cas"] == {"decoded": True, "offset": 397+47*slot, "raw_hex": "00000000", "encoding": "xmp2-cl7-24", "reserved_bits_set": False, "latencies_nck": []}
            assert profile["command_rate"] is None
            continue
        cas, rate = profile["cas"], profile["command_rate"]
        expected = [22+2*slot, 40+2*slot, 98-2*slot]
        raw = 0x11+slot
        if slot == 0:
            if i == 34: expected, raw = [], 0
            if i == 35: expected, raw = list(range(20, 100, 2)), 0xf3
            if i == 36: raw = 0xa4
        assert cas["decoded"] and cas["offset"] == 711+64*slot and len(cas["raw_hex"]) == 10
        assert cas["latencies_nck"] == expected and not cas["reserved_bits_set"]
        code = raw & 15
        assert rate == {"raw": raw, "code": code, "recognized": code <= 3, "cycles": code if 1 <= code <= 3 else None}
base_names = ["tRRD_L", "tCCD_L", "tCCD_L_WR", "tCCD_L_WR2", "tFAW", "tCCD_L_WTR",
              "tCCD_S_WTR", "tRTP", "tCCD_M", "tCCD_M_WR", "tCCD_M_WTR"]
ddr4_names = ["SPD tWR minimum (ps)", "SPD tWTR_S minimum (ps)", "SPD tWTR_L minimum (ps)"]
for i in range(42, 54):
    data = spd_reports[i]["data"]["spd"]
    fields = {f["name"]: f["value"] for f in data["fields"]}
    assert len(fields) == len(data["fields"])
    if i < 49:
        for t, name in enumerate(base_names):
            time, cycles = "SPD " + name + " minimum (ps)", "SPD " + name + " cycle lower limit (nCK)"
            if i in (46, 48) or (i == 43 and t >= 8):
                assert time not in fields and cycles not in fields
            else:
                ps, ck = (65535, 255) if i == 44 else (0, 0) if i == 45 else (1200+179*t, 3+5*t)
                assert fields[time] == str(ps) and fields[cycles] == str(ck)
        assert data["crc_checked"] is (i != 47)
        assert data["crc_valid"] is (None if i == 47 else i != 48)
    else:
        if i in (52, 53):
            assert all(name not in fields for name in ddr4_names)
        else:
            expected = [511875]*3 if i == 50 else [0]*3 if i == 51 else [36375, 70500, 138750]
            assert [fields[name] for name in ddr4_names] == list(map(str, expected))
        if i == 52:
            assert data["cas"]["decoded"] and len(data["xmp"]["profiles"][0]["values"]) == 13
for i in range(54, 68):
    data = spd_reports[i]["data"]["spd"]
    fields = {f["name"]: f["value"] for f in data["fields"]}
    assert len(fields) == len(data["fields"])
    if i < 58 or i >= 64:
        for slot, p in enumerate(data["xmp"]["profiles"]):
            assert p["recommended_dimms_per_channel"] == (None if i == 55 and slot == 0 else (2, 4)[slot])
            assert p["command_rate"] is None and not p["crc"]["checked"]
            if (i in (55, 56) and slot == 0) or (i == 57 and slot == 1):
                assert p["cas"] is None
            else:
                assert p["cas"]["encoding"] == "xmp2-cl7-24" and p["cas"]["latencies_nck"] == ([9, 11, 12, 14, 15, 17], [7, 24])[slot]
                assert p["cas"]["reserved_bits_set"] == bool(slot)
        if i >= 64:
            assert fields.get("Module capacity (MiB)") == {64: "12288", 65: "98304", 66: "24576"}.get(i)
    else:
        assert fields.get("Module capacity (MiB)") == {58: "393216", 60: "786432"}.get(i)
        assert fields.get("Subchannels per module") == {58: "4", 60: "8"}.get(i)
        assert fields.get("SPD tRFC1 different logical rank minimum (ns)") == (None if i in (62, 63) else "321")
        if i != 63:
            assert all(p["recommended_dimms_per_channel"] is None and p["cas"]["encoding"] == "ddr5-cl20-98" for p in data["xmp"]["profiles"])
        else:
            assert data["crc_checked"] and not data["crc_valid"] and data["cas"] is None
assert [r["ok"] for r in spd_reports[68:83]] == [True, True, False, True, True, True, False, True, False, False, False, True, True, True, False]
assert [r["ok"] for r in spd_reports[83:91]] == [True, True, True, True, False, True, True, True]
for i in range(83, 91):
    spd = spd_reports[i]["data"]["spd"]
    fields = {f["name"]: f["value"] for f in spd["fields"]}
    expected_capacity = {83: "204800", 88: "204800", 89: "106496", 90: "294912"}
    assert fields.get("Module capacity (MiB)") == expected_capacity.get(i)
    if i in (83, 88, 89):
        assert fields["First geometry capacity (MiB)"] == ("8192" if i == 89 else "196608")
        assert fields["Second geometry capacity (MiB)"] == ("98304" if i == 89 else "8192")
        assert fields["Second SDRAM device width (bits)"] == ("8" if i == 89 else "16")
    if i in (86, 87, 90):
        assert "Second SDRAM density (encoded)" not in fields
    assert spd["crc_checked"] == (i != 88)
    if i == 87:
        assert spd["error"] == -84 and not spd["crc_valid"]
assert [r["ok"] for r in spd_reports[91:]] == [True, True, True, True, True, False, True, False, True, True]
for i in range(91, 101):
    spd = spd_reports[i]["data"]["spd"]
    fields = {f["name"]: f["value"] for f in spd["fields"]}
    assert fields.get("Module capacity (MiB)") == {91: "40960", 92: "45056", 93: "217088", 97: "40960", 100: "49152"}.get(i)
    if i in (91, 92, 93, 97):
        assert fields["First geometry capacity (MiB)"] == {91: "16384", 92: "32768", 93: "131072", 97: "16384"}[i]
        assert fields["Second geometry capacity (MiB)"] == {91: "24576", 92: "12288", 93: "86016", 97: "24576"}[i]
        assert fields["Second SDRAM density per die (Mbit)"] == "12288"
        assert int(fields["First geometry package ranks"]) == (2 if i == 92 else 1)
        assert int(fields["Second geometry package ranks"]) == (2 if i in (91, 97) else 1)
    if i in (94, 95):
        assert "Not decoded" in fields["Module capacity status"]
        assert "First geometry capacity (MiB)" not in fields
    if i >= 98 or i == 96:
        assert "SDRAM density ratio (encoded)" not in fields
    if i == 94:
        assert fields["SDRAM density ratio (encoded)"] == "3" and "Second SDRAM density per die (Mbit)" not in fields
    if i == 96:
        assert spd["error"] == -84 and spd["crc_checked"] and not spd["crc_valid"]
    if i == 97:
        assert spd["crc_valid"] and not spd["xmp"]["inspected"]
for i in range(68, 83):
    xmp = spd_reports[i]["data"]["spd"]["xmp"]
    slots = xmp["user_profiles"]
    if i in (77, 78):
        assert slots == []
        continue
    assert len(slots) == 2
    for slot, p in enumerate(slots):
        assert p["enabled"] is None and p["name"] is None and not p["name_supported"]
        assert p["index"] == slot+1 and p["offset"] == 896+64*slot
        if (i in (75, 76, 80, 81, 82) and slot == 0):
            assert p["storage_state"] == "overlap_expo" and p["blocked_by_expo"] and not p["crc"]["checked"]
            assert p["values"] == [] and p["captured_bytes"] == 64
        elif i == 71:
            assert p["storage_state"] == ("incomplete", "not_captured")[slot]
            assert p["captured_bytes"] == (54, 0)[slot] and not p["crc"]["checked"] and p["values"] == []
        elif i in (72, 73):
            assert p["storage_state"] == ("all_zero" if i == 72 else "all_ff")
            assert not p["crc"]["checked"] and p["values"] == []
        elif i in (70, 74) and slot == 0:
            assert p["storage_state"] == "stored" and p["error"] == (-84 if i == 70 else -22) and p["values"] == []
        else:
            assert p["storage_state"] == "stored" and p["crc"]["valid"] and len(p["values"]) == 30
            values = {v["name"]: v for v in p["values"]}
            assert values["tCK minimum"]["value"] == (333 if i == 69 and slot == 0 else 400+25*slot)
            assert values["tRFC1 minimum"]["unit"] == "ns" and p["cas"]["decoded"]
            if i == 69 and slot == 0:
                assert p["crc"]["computed"] == p["crc"]["stored"] == 0
    if i == 79:
        assert all(not p["enabled"] and p["values"] == [] for p in xmp["profiles"])
    if i in (80, 81):
        for slot, p in enumerate(spd_reports[i]["data"]["spd"]["expo"]["profiles"]):
            expected = [] if i == 81 and slot == 0 else [1000+17*(8*slot+t) for t in range(8)]
            assert [v["value"] for v in p["stored_enhanced_timings"]] == expected
assert len([r for r in reports if r["error"] == -22]) >= 132
uncore = [r for r in reports if r["command"] in ("intel-uncore-read", "intel-uncore-set") and "before" in r["data"]]
assert len(uncore) == 16
for result in uncore:
    data = result["data"]
    assert data["msr"] == "0x00000620" and not data["hardware_effect_measured"]
    for key in ("before", "after"):
        if key not in data:
            continue
        snapshot = data[key]
        if snapshot["valid"]:
            raw = int(snapshot["raw"], 16)
            assert snapshot["minimum_ratio"] == (raw >> 8) & 127
            assert snapshot["maximum_ratio"] == raw & 127
            assert snapshot["editable"] == (0 < snapshot["minimum_ratio"] <= snapshot["maximum_ratio"])
        else:
            assert not snapshot["editable"]
            assert all(snapshot[k] is None for k in ("raw", "minimum_ratio", "maximum_ratio"))
    if result["command"] == "intel-uncore-set":
        assert data["verified"] == result["ok"]
        if result["ok"]:
            assert data["after"]["raw"] == data["expected_raw"]
            assert data["after"]["minimum_ratio"] == data["requested_minimum_ratio"] == 12
            assert data["after"]["maximum_ratio"] == data["requested_maximum_ratio"] == 42
            assert (int(data["before"]["raw"], 16) ^ int(data["after"]["raw"], 16)) & ~0x7f7f == 0
            assert data["write_attempted"] == (not data["unchanged"])
            assert data["completed_writes"] == int(not data["unchanged"])
assert [r["error"] for r in uncore[6:15]] == [-95, -95, -13, -5, -5, -11, -5, -22, -13]
assert uncore[-1]["ok"] and not uncore[-1]["data"]["before"]["editable"]
vf_edit = [r for r in reports if "edit_context" in r.get("data", {})]
assert len(vf_edit) == 24
for index, result in enumerate(vf_edit[:8]):
    data = result["data"]
    assert result["ok"] and not data["hardware_effect_measured"]
    before = data["edit_context"]
    assert before["valid"] and before["editable"] and before["domain_configuration_default"]
    assert before["flex_ratio_raw"] == "0x8000000000000000"
    if index % 4 == 0:
        assert result["command"] == "intel-vf-read" and "after" not in data
        continue
    after = data["after"]
    assert data["verified"] and data["unchanged"] == (index % 4 == 2)
    assert data["write_attempted"] == (index % 4 != 2)
    assert after["offset_mv"] == (0 if index % 4 == 3 else -49.8046875)
    assert after["point_raw"] == data["expected_point_raw"]
    assert (int(before["point_raw"], 16) & 0x1fffff) == (int(after["point_raw"], 16) & 0x1fffff)
    assert all(before[k] == after[k] for k in ("control_raw", "domain_raw", "flex_ratio_raw", "ratio", "per_core_override"))
    if data["write_attempted"]:
        assert (int(data["submitted_raw"], 16) & 0x1fffff) == 0
        assert data["settings_completed"] and data["settings_firmware_status"] == 0
    else:
        assert data["submitted_raw"] is None and not data["settings_completed"]
assert [r["error"] for r in vf_edit[8:21]] == [-1, -1, -1, -1, -1, -5, -13, -95, -5, -5, -11, -11, -5]
for mode, result in enumerate(vf_edit[8:21]):
    data = result["data"]
    assert not result["ok"] and not data["verified"]
    assert data["write_attempted"] == (mode in (5, 8, 11, 12))
    for snapshot in (data["edit_context"], data["after"]):
        if not snapshot["valid"]:
            assert not snapshot["editable"]
            assert all(snapshot[k] is None for k in ("control_raw", "domain_raw", "point_raw", "flex_ratio_raw", "ratio", "offset_mv", "locked", "per_core_override", "domain_configuration_default"))
assert vf_edit[16]["data"]["settings_firmware_status"] == 254
assert vf_edit[21]["ok"] and vf_edit[21]["data"]["edit_context"]["locked"] and not vf_edit[21]["data"]["edit_context"]["editable"]
assert all(r["data"]["edit_context"]["per_core_override"] and r["data"]["edit_context"]["editable"] for r in vf_edit[22:])
assert vf_edit[23]["data"]["verified"] and vf_edit[23]["data"]["after"]["control_raw"] == "0x12345008"
turbo_all = [r for r in reports if r["command"] in ("intel-turbo-read", "intel-turbo-set") and "before" in r["data"]]
thresholds = [r for r in turbo_all if r["data"].get("field") == "active-cores"]
turbo = [r for r in turbo_all if r not in thresholds]
assert len(turbo) == 19
for result in turbo:
    data = result["data"]
    assert not data["hardware_effect_measured"]
    assert data["ratio_msr"] == ("0x000001ad" if data["core_type"] == "p" else "0x00000650")
    assert data["core_count_msr"] == ("0x000001ae" if data["core_type"] == "p" else "0x00000651")
    for key in ("before", "after"):
        if key not in data:
            continue
        snapshot = data[key]
        if snapshot["valid"]:
            assert len(snapshot["groups"]) == 8
            for i, group in enumerate(snapshot["groups"]):
                assert group["group"] == i
                assert group["ratio"] == ((int(snapshot["ratios_raw"], 16) >> (i * 8)) & 255)
                assert group["active_core_threshold"] == ((int(snapshot["core_counts_raw"], 16) >> (i * 8)) & 255)
                assert group["editable"] == bool(group["active"] and snapshot["layout_valid"] and snapshot["programmable"] and not snapshot["locked"])
        else:
            assert snapshot["ratios_raw"] is None and snapshot["core_counts_raw"] is None and not snapshot["groups"]
    if result["command"] == "intel-turbo-set":
        assert data["verified"] == result["ok"]
        if result["ok"]:
            assert data["after"]["ratios_raw"] == data["expected_raw"]
            assert data["before"]["core_counts_raw"] == data["after"]["core_counts_raw"]
            assert data["after"]["groups"][data["group"]]["ratio"] == data["requested_ratio"]
            assert data["completed_writes"] == int(not data["unchanged"])
assert [r["error"] for r in turbo[8:17]] == [-1, -1, -5, -11, -13, -95, -5, -22, -11]
assert turbo[-1]["ok"] and not turbo[-1]["data"]["before"]["layout_valid"]
assert len(thresholds) == 13
assert [r["error"] for r in thresholds] == [0, 0, 0, 0, -1, -22, -1, -5, -11, -11, -5, -34, -34]
for result in thresholds:
    data = result["data"]
    assert "requested_ratio" not in data and not data["hardware_effect_measured"]
    assert data["verified"] == result["ok"]
    for name in ("before", "after"):
        snapshot = data[name]
        active = [g["group"] for g in snapshot["groups"] if g["active"]]
        for g in snapshot["groups"]:
            assert g["threshold_editable"] == bool(g["editable"] and g["group"] != active[-1])
    if result["ok"]:
        assert data["after"]["core_counts_raw"] == data["expected_raw"] == "0x100e0c0a09060402"
        assert data["before"]["ratios_raw"] == data["after"]["ratios_raw"]
        assert data["after"]["groups"][3]["active_core_threshold"] == data["requested_active_cores"] == 9
        assert data["after"]["groups"][7]["active_core_threshold"] == 16
        assert data["completed_writes"] == int(not data["unchanged"])
registers = [r for r in reports if r["command"] in ("register-read", "register-write") and r["error"] != -22]
assert len(registers) == 19
assert registers[0]["data"]["value"] == "0xfedcba9876543210"
assert all(r["data"]["submitted"] == "0xffffffffffffffff" for r in registers[1:3])
assert registers[3]["data"]["address"] == "0xffffffffffffffff"
for result in registers:
    data = result["data"]
    assert not data["verified"]
    if data["space"] != "msr":
        assert "cpu" not in data
    if data["space"] == "pci":
        assert data["pci"] == {"domain": 0, "bus": 255, "device": 31, "function": 7}
    if result["command"] == "register-write":
        assert data["value"] is None and data["write_attempted"]
        assert data["completed_writes"] == int(result["ok"])
    elif not result["ok"]:
        assert data["value"] is None
pl4 = [r for r in reports if r['command']=='intel-set' and r['data'].get('field')=='pl4']
assert len(pl4)==15 and [r['ok'] for r in pl4]==[True]*6+[False]*9
for i,r in enumerate(pl4):
    data=r['data']; assert data['verified']==r['ok'] and not data['hardware_effect_measured']
    if r['ok']:
        expected=0x8765432140006000 | (1,800,8191)[i//2]
        assert int(data['expected_raw'],16)==expected and data['readback_raw']==data['expected_raw']
        assert data['readback_value']==(expected&0x1fff)/8
        assert data['msr']=='0x00000601' and data['write_attempted']==(i%2==0) and data['unchanged']==(i%2==1)
    else: assert data['readback_value'] is None
assert [r['data']['write_attempted'] for r in pl4[6:]]==[False]*5+[True]*3+[False]
controls = [r for r in reports if r["command"] == "intel-set" and "verified" in r.get("data", {}) and r['data'].get('field')!='pl4' and not r['data']['shared_package_hint']]
assert len(controls) == 18
for result in controls:
    data = result["data"]
    assert data["verified"] == result["ok"]
    if result["ok"]:
        assert data["readback_raw"] == data["expected_raw"]
        assert data["readback_value"] in (100, 192, 15000, 0)
        assert data["completed_writes"] == (0 if data["unchanged"] else 1)
    else:
        assert data["readback_value"] is None
controls = controls[:10]  # Preserve the earlier RAPL/window scenario-specific checks.
assert controls[4]["data"]["unchanged"] and not controls[4]["data"]["write_attempted"]
assert controls[6]["error"] == -5 and controls[6]["data"]["readback_raw"] != controls[6]["data"]["expected_raw"]
assert [r["data"]["readback_value"] for r in controls[-3:]] == [15000, 0, None]
assert controls[-1]["error"] == -1 and not controls[-1]["data"]["write_attempted"]
voltage = [r for r in reports if r["command"] == "intel-oc-set" and r.get("data", {}).get("field") == "target-mv"]
assert len(voltage) == 14
assert [r["data"]["write_attempted"] for r in voltage[:8]] == [True, False] * 4
for result in voltage[:8]:
    data = result["data"]
    assert result["ok"] and data["verified"] and data["requested"] == 1234
    before, after = (int(data[k]["raw"], 16) for k in ("before", "after"))
    assert (before & 0xffe000ff) == (after & 0xffe000ff)
    assert data["after"]["target_mv"] == 1234.375
    assert data["after"]["target_mode"] == data["requested_mode"]
assert all(not r["ok"] and not r["data"]["verified"] for r in voltage[8:])
assert [r["error"] for r in voltage[8:]] == [-1, -95, -5, -5, -11, -13]
for result in reports:
    if result["command"] not in ("intel-oc-read", "intel-oc-set"):
        continue
    for key in ("before", "after"):
        snapshot = result.get("data", {}).get(key)
        if snapshot is None:
            continue
        if snapshot["valid"]:
            raw = int(snapshot["raw"], 16)
            assert snapshot["target_mv"] == ((raw >> 8) & 4095) * 1000 / 1024
            assert snapshot["target_mode"] == ("override" if raw & (1 << 20) else "adaptive")
        else:
            assert snapshot["target_mv"] is None and snapshot["target_mode"] is None
vf = [r for r in reports if r["command"] == "intel-vf-read" and "points" in r.get("data", {})]
assert len(vf) == 6
all_points = vf[0]["data"]
assert all_points["scan_completed"] and all_points["selected_point"] is None
assert [p["point"] for p in all_points["points"]] == list(range(1, 16))
assert all(p["valid"] and p["completed"] and p["query_attempted"] for p in all_points["points"])
for p in all_points["points"]:
    raw = int(p["raw"], 16)
    code = raw >> 21
    assert p["ratio"] == raw & 255
    assert p["offset_mv"] == (code if code < 1024 else code - 2048) * 1000 / 1024
assert vf[1]["data"]["domain"] == "cache" and vf[1]["data"]["selected_point"] == 15
assert len(vf[1]["data"]["points"]) == 1
partial = vf[2]["data"]
assert not vf[2]["ok"] and partial["scan_completed"] and len(partial["points"]) == 15
assert partial["points"][7]["firmware_status"] == 254 and partial["points"][8]["valid"]
for result in vf[2:5]:
    failed = [p for p in result["data"]["points"] if not p["valid"]]
    assert len(failed) == 1
    assert all(failed[0][key] is None for key in ("raw", "ratio", "offset_mv"))
assert not vf[4]["data"]["scan_completed"] and vf[4]["data"]["points"][0]["firmware_status"] is None
assert vf[5]["data"]["points"] == [] and not vf[5]["data"]["scan_completed"]
curve = [r for r in reports if r["command"] == "amd-curve-read" and r["ok"] and "raw" in r["data"]][0]
assert curve["data"]["raw"] == "0xffffffe2" and curve["data"]["raw_signed"] == -30
curve_fail = [r for r in reports if r["command"] == "amd-curve-read" and r["error"] == -5 and "raw" in r["data"]][0]
assert curve_fail["data"]["raw"] is None and curve_fail["data"]["message_attempted"]
pstate = [r for r in reports if r["command"] == "amd-pstates" and r["ok"]][0]
assert pstate["data"]["rows"][0]["raw"] == "0xfffffffffffffc00"
assert pstate["data"]["rows"][2]["raw"] is None
assert pstate['data']['family'] == 0x1a and pstate['data']['layout'] == 'fid12-x5'
assert pstate['data']['vid_bit_positions'] == '32,21:14'
assert pstate['data']['rows'][0]['frequency_id'] == 3072 and pstate['data']['rows'][0]['frequency_mhz'] == 15360
assert pstate['data']['rows'][0]['divider_id'] is None and pstate['data']['rows'][0]['vid_bits'] == 511
legacy_pstates = [r for r in reports if r['command'] == 'amd-pstates' and r['data'].get('cpu') == 641]
assert len(legacy_pstates) == 29
for n, report in enumerate(legacy_pstates[:6]):
    d = report['data']; rows = d['rows']
    assert report['ok'] and d['family'] == (0x17 if n < 3 else 0x19)
    assert d['model'] == (1, 0x31, 0x71)[n % 3] and d['stepping'] == 1
    assert d['layout'] == 'fid8-dfs6' and d['vid_bit_positions'] == '21:14' and not d['frequency_is_measurement']
    assert d['minimum'] == 1 and d['maximum'] == 7 and d['limit_raw'] == '0xfedcba9876543271'
    assert [r['frequency_id'] for r in rows] == [160,171,165,160,160,15,160,160]
    assert [r['divider_id'] for r in rows] == [8,12,9,8,0,8,27,46]
    assert rows[0]['frequency_mhz'] == 4000 and rows[1]['frequency_mhz'] == 2850
    assert abs(rows[2]['frequency_mhz'] - 3666.6666666666665) < 1e-8
    assert [r['frequency_status'] for r in rows] == ['valid']*3 + ['disabled','divider_off','reserved_fid','unverified_divider','unverified_divider']
    assert all(r['frequency_mhz'] is None for r in rows[3:])
    assert all(r['vid_bits'] == 55 and r['raw_bit_32'] and r['idd_value_bits'] == 175 and r['idd_div_bits'] == 2 for r in rows)
for family in range(2):
    for mode, report in enumerate(legacy_pstates[6+family*10:16+family*10]):
        d = report['data']
        assert not report['ok']
        if mode in (5,6):
            bad = d['rows'][0 if mode == 5 else 7]
            assert bad['status'] == 'read_failed' and bad['error'] == -5
            assert all(bad[k] is None for k in ('raw','enabled','frequency_mhz','frequency_status','frequency_id','divider_id','vid_bits','raw_bit_32'))
            assert sum(r['status'] == 'read' for r in d['rows']) == 7
        else:
            assert all(r['raw'] is None and r['frequency_status'] is None for r in d['rows'])
            assert d['minimum'] is None and d['maximum'] is None
        if mode == 9:
            assert d['status'] == 'inconsistent_limit' and d['limit_raw'] == '0x0000000000000023' and report['error'] == -34
        if mode < 2:
            assert all(d[k] is None for k in ('family','model','stepping','layout','vid_bit_positions'))
for report in legacy_pstates[-3:]:
    assert not report['ok'] and report['data']['status'] == 'unsupported_family'
    assert report['data']['layout'] == 'unsupported' and report['data']['vid_bit_positions'] is None
electrical_pstates = [r for r in reports if r['command'] == 'amd-pstates' and r['data'].get('cpu') == 642]
assert len(electrical_pstates) == 27
for n, report in enumerate(electrical_pstates[:20]):
    d = report['data']; mode = n % 4; svi3 = n // 4 == 3
    assert report['ok'] == (mode != 2) and d['hypervisor'] == (mode == 1)
    assert d['electrical_encoding'] == ('svi3' if svi3 else 'svi2')
    assert d['current_descriptor_scope'] == 'expected_maximum_single_core_acpi_pss'
    assert not d['electrical_is_measurement'] and not d['frequency_is_measurement']
    assert d['vid_bit_positions'] == '21:14'  # Existing raw fragment is unchanged.
    volts = [0,250,1525,2800,1520,1925] if svi3 else [1550,1543.75,1050,6.25,0,0]
    vids = [0,1,256,511,255,336] if svi3 else [0,1,80,247,248,255]
    for i, row in enumerate(d['rows']):
        e = row['electrical']
        if (mode == 2 and i == 2) or (mode == 3 and i > 1):
            assert e is None and row['raw'] is None
            assert row['status'] == ('read_failed' if mode == 2 else 'above_limit')
            continue
        if mode == 1 or i >= 6:
            status = 'virtualized' if mode == 1 else 'disabled' if i == 6 else 'reserved_bits'
            assert e['voltage_status'] == status and e['current_status'] == status
            assert all(e[k] is None for k in ('vid_bits','voltage_mv','expected_core_current_a'))
            continue
        assert e['vid_bits'] == vids[i] and row['vid_bits'] == (vids[i] & 255)
        assert e['voltage_mv'] == volts[i]
        assert e['voltage_status'] == ('voltage_off' if volts[i] == 0 else 'valid')
        assert e['current_status'] == ('reserved_divisor' if i == 4 else 'valid')
        assert e['expected_core_current_a'] == [255,17.5,1.75,0,None,2.55][i]
for report in electrical_pstates[20:]:
    assert report['ok'] and report['data']['electrical_encoding'] == 'unavailable'
    e = report['data']['rows'][0]['electrical']
    assert e['voltage_status'] == e['current_status'] == 'unsupported_model'
    assert e['vid_bits'] is None and e['voltage_mv'] is None and e['expected_core_current_a'] is None
for report in [r for r in reports if r['command'] == 'amd-pstates']:
    for row in report['data']['rows']:
        if row['status'] != 'read': assert row['electrical'] is None
umc = [r for r in reports if r["command"] == "amd-umc-read" and r["ok"]][0]
contract = json.loads((Path(__file__).resolve().parents[1] / "gui/tests/fixtures/cli-umc.json").read_text())
assert {k: umc[k] for k in contract if k != "data"} == {k: v for k, v in contract.items() if k != "data"}
assert {k: umc["data"][k] for k in contract["data"]} == contract["data"]
assert len(umc["data"]["fields"]) == 212 and len(umc["data"]["registers"]) == 56
assert len({f["id"] for f in umc["data"]["fields"]}) == 212

# Exercise both directions through the real executable without Qt or hardware.
with tempfile.TemporaryDirectory(prefix="octool-umc-") as directory:
    root = Path(directory)
    before, after = root / 'before-"-内存.json', root / "after.json"
    gui = {"format": "octool-amd-umc-v1", "bank": 0, "refresh_slot": 0,
           "registers": [{"offset": 516, "value": 34}, {"offset": 4624, "value": 258048}]}
    before.write_text(json.dumps(gui), encoding="utf-8")
    decoded = run("amd-umc-decode", "--file", str(before))
    data = decoded["data"]
    assert data["offline"] and not data["hardware_accessed"] and not data["origin_verified"]
    assert not data["complete"] and data["cpu"] is None and data["pci"] is None
    assert data["fields"][32]["encoded"] == 34 and data["fields"][203]["encoded"] == 63
    assert any(f["encoded"] is None and f["error"] < 0 for f in data["fields"])
    after.write_text(json.dumps(decoded), encoding="utf-8")
    roundtrip = run("amd-umc-decode", "--file", str(after))["data"]
    assert roundtrip["registers"] == data["registers"] and roundtrip["fields"] == data["fields"]
    same = run("amd-umc-diff", "--file", str(before), "--compare", str(after))["data"]
    assert sum(same["counts"].values()) == 212 and same["counts"]["changed"] == 0
    assert same["fields"][32]["state"] == "unchanged" and same["fields"][203]["state"] == "unchanged"
    gui["registers"][0]["value"] = 35
    gui["registers"].pop()
    after.write_text(json.dumps(gui), encoding="utf-8")
    delta = run("amd-umc-diff", "--file", str(before), "--compare", str(after))["data"]
    assert delta["fields"][32]["encoded_delta"] == 1 and delta["fields"][32]["state"] == "changed"
    assert delta["fields"][203]["after_encoded"] is None and delta["fields"][203]["encoded_delta"] is None
    assert delta["fields"][203]["state"] == "missing_after" and not delta["physical_channel_verified"]
    gui["bank"] = 1
    after.write_text(json.dumps(gui), encoding="utf-8")
    mismatch = run("amd-umc-diff", "--file", str(before), "--compare", str(after), code=3)
    assert mismatch["error"] == -18 and "fields" not in mismatch["data"]
    after.write_text(json.dumps(umc), encoding="utf-8")
    live_import = run("amd-umc-decode", "--file", str(after))["data"]
    assert live_import["complete"] and live_import["registers"] == umc["data"]["registers"]
    assert live_import["fields"] == umc["data"]["fields"]
    content = before.read_bytes()
    before.write_bytes(content + b" " * (65536 - len(content)))
    run("amd-umc-decode", "--file", str(before))
    before.write_bytes(before.read_bytes() + b" ")
    assert run("amd-umc-decode", "--file", str(before), code=3)["error"] == -27
    before.write_text('{"format":"octool-amd-umc-v1","bank":0,"bank":1}', encoding="utf-8")
    assert run("amd-umc-decode", "--file", str(before), code=3)["error"] == -22
    fifo = root / "fifo"
    os.mkfifo(fifo)
    for path in (fifo, root, root / "missing"):
        failed = run("amd-umc-decode", "--file", str(path), code=3)
        assert not failed["data"]["hardware_accessed"] and "fields" not in failed["data"]
print("UMC offline process checks passed: GUI/CLI roundtrips, differences, missing values, 64 KiB boundary and nonblocking FIFO rejection")
for result in reports:
    if result["command"] == "intel-oc-set" and result["ok"]:
        assert result["data"]["verified"]

with tempfile.TemporaryDirectory(prefix="octool-cli-") as directory:
    root = Path(directory)

    def put(path, value):
        p = root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(value if isinstance(value, bytes) else value.encode())
        return p

    put("class/dmi/id/board_name", "Fixture board\n")
    put("class/dmi/id/bios_version", "x" * 4097)
    put("class/hwmon/hwmon0/name", "Fixture sensor\n")
    put("class/hwmon/hwmon0/temp1_label", "CPU\n")
    put("class/hwmon/hwmon0/temp1_input", "-1250\n")
    put("class/hwmon/hwmon0/temp1_fault", "1\n")
    put("class/hwmon/hwmon0/in1_input", "invalid\n")
    put("class/hwmon/hwmon0/power1_average", "125000000\n")
    eeprom = put("bus/i2c/drivers/ee1004/0-0050/eeprom", b"\0" * 1024)
    duplicate = root / "bus/i2c/drivers/spd5118/0-0050"
    duplicate.parent.mkdir(parents=True)
    duplicate.symlink_to(eeprom.parent, target_is_directory=True)
    put("bus/i2c/drivers/spd5118/1-0051/eeprom", b"\0" * 4097)
    os.mkfifo(root / "fifo")
    subprocess.run([fixture, "--inventory-fixture", str(root)], check=True, env=env, timeout=10)
    run("spd-decode", "--file", str(root / "fifo"), code=3)
    run("spd-decode", "--file", str(root / "missing"), code=3)
    run("spd-decode", "--file", str(root / "class/dmi/id/bios_version"), code=3)
    spd = bytearray(512)
    spd[1:7] = bytes([0x10, 0x12, 3, 4, 0, 0x20])
    spd[234:236] = bytes([8, 2])
    spd[510:] = binascii.crc_hqx(spd[:510], 0).to_bytes(2, "little")
    path = put('memory-"-dump.spd', bytes(spd))
    result = run("spd-decode", "--file", str(path))["data"]["spd"]
    assert result["path"] == str(path) and result["crc_valid"] and result["crc_checked"]
    spd[50] ^= 1
    path.write_bytes(spd)
    result = run("spd-decode", "--file", str(path), code=3)["data"]["spd"]
    assert result["crc_checked"] and not result["crc_valid"]
print(f"CLI process checks passed: no display, sparse affinity, {len(reports)} JSON reports, exact 64-bit registers, RAPL/HWP readback and activity window, voltage preservation and VF query/edit errors, turbo groups and full readback, inventory limits, SPD CRC/errors")
