#!/usr/bin/env python3
"""Pinned, static-only REA/Ghidra study of the original AMD PM implementation.

Uses one isolated MCP session; the reference executable is never launched.
Decompiler output is research evidence, not recovered source or firmware proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import tempfile
import threading
import time

SOURCE_SHA = '44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10'
# Separately pinned UEFI modules from the vendor's public ASUS 1317 capsule.
# These profiles do not relax the original ELF identity check.
FIRMWARE_STUDIES = {
    'granite_transport': ('563fbf5d9350c95a9f8a063e4f98a6fbe4d22d3e2bc11d2a3346a4e3320578c9',
                          (('0x4e4', 'entry'),)),
    'granite_transport2': ('9ca8b4e78e40f98853163c8414cd61745005ed8d4cd8f7a8463c76e6b16a280e',
                           (('0x4e4', 'entry'),)),
    'firmware_transport': ('7b5302929acb73d0a6a56842af62850b1b017e5c8d1fe1ccbc22a25551d06e67',
                           (('0x1464', 'entry'),)),
    'firmware_limits': ('7da8e70b76a41f065bfa645b04bdcb58a1cdab3f02bcae111c3c8581467e9f4d',
                        (('0x4850', 'entry'),)),
}
# Exact file offsets in the executable PT_LOAD of the hash-verified ELF.
# These happen to equal ELF virtual addresses. Ghidra may relocate this PIE;
# unqualified hexadecimal queries could select an unrelated function.
PROCEDURES = (('0x2e7360', 'get_pm_patch'), ('0x2c7910', 'AMD_PM_LOG'),
              ('0x2254c0', 'Wr_Scratch2'), ('0x21e770', 'get_fused_ccd'),
              ('0x21e6f0', 'get_amd_native_thread_count'))
ARRAY_PROCEDURES = (('0x2e7360', 'get_pm_patch'), ('0x2c7910', 'AMD_PM_LOG'),
                   ('0x6d4710', 'populate_lines'), ('0x6d7e90', 'on_pushButton_clicked'),
                   ('0x2e66f0', 'Update1_fast'), ('0x2daee0', 'Update2_fast'),
                   ('0x6f2800', 'pm_monitor'))
MONITOR_PROCEDURES = (('0x2e7360', 'get_pm_patch'),
                      ('0x6c1210', 'amd_am3::pm0_monitor'),
                      ('0x6c40c0', 'amd_am4::pm0_monitor'),
                      ('0x6c7690', 'amd_am5::pm0_monitor'),
                      ('0x8fb1f0', 'mainwindow2::handleTimeout'))
PERCORE_PROCEDURES = (('0x239100', 'get_percore_power_offset'),
                     ('0x236b80', 'get_layout'), ('0x230ea0', 'Rd_MMIO'),
                     ('0x2df0e0', 'AMD_PM_LOG'),
                     ('0x238d90', 'AMD_PM_LOG::set_curve_opt'),
                     ('0x238f40', 'set_curve_opt_percore'),
                     ('0x239400', 'AMD_PBO::set_curve_opt'),
                     ('0x2397b0', 'set_fmax'), ('0x239990', 'set_core01_rails'),
                     ('0x236890', 'get_mem_address64bit'),
                     ('0x2365c0', 'start_monitor'),
                     ('0x750b70', 'on_per_ccx_oc_apply_5_clicked'))
PBO_LAYOUT_PROCEDURES = (('0x21f700', 'MP1_C2PMSG'),
                         ('0x239150', 'AMD_PBO'),
                         ('0x2397b0', 'set_fmax'), ('0x239990', 'set_core01_rails'),
                         ('0x74f720', 'on_per_ccx_oc_apply_2_clicked'),
                         ('0x752270', 'on_per_ccx_oc_apply_clicked'),
                         ('0x7217c0', '~amdvf'), ('0x6fec20', 'tr5_get_both'),
                         ('0x761670', 'cpufunctions'),
                         ('0x2eca70', 'get_pm'), ('0x236820', 'refresh_monitor'))
PBO_SEMANTICS_PROCEDURES = (('0x21e350', 'get_fused_fmax'),
                            ('0x21dc20', 'get_fmax_from_model'),
                            ('0x257550', 'AMD_PM_LOG'),
                            ('0x2df0e0', 'AMD_PM_LOG'),
                            ('0x2eca80', 'stream_vftf'),
                            ('0x236670', 'refresh_monitor_fast'),
                            ('0x2366d0', 'refresh_monitor_2nd'),
                            ('0x750170', 'on_per_ccx_oc_apply_3_clicked'),
                            ('0x7502d0', 'on_per_ccx_oc_apply_4_clicked'),
                            ('0x751470', 'on_per_ccx_oc_apply_7_clicked'),
                            ('0x7517d0', 'on_per_ccx_oc_apply_8_clicked'),
                            ('0x751d70', 'on_per_ccx_oc_apply_9_clicked'))
PBO_LIMITS_PROCEDURES = (('0x7502d0', 'on_per_ccx_oc_apply_4_clicked'),
                         ('0x223d50', 'TEST_SMC_MSG_sus_pwrlimit'),
                         ('0x224500', 'TEST_SMC_MSG_vdd_current_limit'),
                         ('0x224920', 'TEST_SMC_MSG_soc_current_limit'),
                         ('0x7dbe90', 'dyn_edc::refreshit'),
                         ('0x7dc660', 'dyn_edc::update'),
                         ('0x7df630', 'dyn_edc::apply_clicked'),
                         ('0x2395b0', 'toggle_dynamic_mem'),
                         ('0x238cf0', 'get_THM'),
                         ('0x21d380', 'set_to_shimada'))
OC_FREQUENCY_PROCEDURES = (('0x21fb00', 'enableocmode'),
                           ('0x21fc00', 'amd_disableocmode'),
                           ('0x21feb0', 'Wr_AMD_freq'),
                           ('0x220110', 'per_ccx_freq'),
                           ('0x220430', 'one_core_freq2'),
                           ('0x21efe0', 'one_core_freq_fast'),
                           ('0x21f1f0', 'per_ccx_freq_fast'),
                           ('0x21fd00', 'Wr_ocvid'),
                           ('0x239990', 'set_core01_rails'),
                           ('0x752270', 'on_per_ccx_oc_apply_clicked'))
OC_CORES_PROCEDURES = (('0x21ffb0', 'amd_per_ccd_vermeer'),
                       ('0x2202b0', 'one_core_freq_vermeer'),
                       ('0x21f140', 'per_ccx_freq_vermeer_fast'),
                       ('0x21ef30', 'one_core_freq_vermeer_fast'),
                       ('0x220110', 'per_ccx_freq'),
                       ('0x21f1f0', 'per_ccx_freq_fast'),
                       ('0x220430', 'one_core_freq2'),
                       ('0x752270', 'on_per_ccx_oc_apply_clicked'))
PM_TRANSPORT_PROCEDURES = (('0x236730', 'AMD_PM_LOG::get_mem_address'),
                           ('0x236890', 'get_mem_address64bit'),
                           ('0x2365c0', 'AMD_PM_LOG::start_monitor'),
                           ('0x236310', 'AMD_PM_LOG_PARENT::start_monitor'),
                           ('0x236450', 'AMD_PM_LOG_PARENT::get_mem_address'),
                           ('0x236b80', 'get_layout'),
                           ('0x21f3e0', 'cal_vid_from_raw'),
                           ('0x21f470', 'cal_raw_from_vid'),
                           ('0x220db0', 'TEST_SMC_MSG_ForceCpuVid'),
                           ('0x239990', 'set_core01_rails'))
CURVE_ALL_PROCEDURES = (('0x238d90', 'AMD_PM_LOG::set_curve_opt'),
                        ('0x239400', 'AMD_PBO::set_curve_opt'),
                        ('0x750b70', 'on_per_ccx_oc_apply_5_clicked'),
                        ('0x220600', 'TEST_SMC_MSG_ForceAllDLDOPsm'),
                        ('0x2209e0', 'TEST_SMC_MSG_UnForceAllDLDOPsm'),
                        ('0x221240', 'TEST_SMC_MSG_UnForceCpuVid'),
                        ('0x21fd00', 'Wr_ocvid'),
                        ('0x239990', 'set_core01_rails'))
POWER_ADJUSTMENT_PROCEDURES = (('0x239100', 'get_percore_power_offset'),
                               ('0x21f700', 'MP1_C2PMSG'),
                               ('0x239990', 'set_core01_rails'))
VF_SOURCES_PROCEDURES = (('0x6ece40', 'get_amd_freq_vid'),
                         ('0x6eebf0', 'tr5_loop_boost_curve'),
                         ('0x6fd190', 'tr5_get_boost_curve'),
                         ('0x6fec20', 'tr5_get_both'),
                         ('0x2eca80', 'stream_vftf'),
                         ('0x221610', 'TEST_SMC_MSG_ForceSocVid'),
                         ('0x221a60', 'TEST_SMC_MSG_UnForceSocVid'),
                         ('0x239bc0', 'AMD_PBO::get_co'),
                         ('0x4bf430', 'ln2_en'),
                         ('0x239990', 'set_core01_rails'))


def selected_address(reply, offset):
    evidence = reply['structuredContent']
    if not isinstance(evidence.get('normalized_result'), str) or not evidence['normalized_result'].strip():
        raise ValueError('REA returned no function pseudocode')
    locations = evidence.get('locations', [])
    source = [item['offset'] for item in locations if item.get('kind') == 'file-offset']
    addresses = [item['address'] for item in locations if item.get('kind') == 'address']
    # REA 6.7.0 supplies the mapped address then the containing procedure start.
    if source != [int(offset, 16)] or len(addresses) != 2 or addresses[0] != addresses[1]:
        raise ValueError('File offset did not resolve to an exact procedure start')
    return addresses[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--rea', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--study', choices=('baseline', 'arrays', 'monitors', 'percore', 'pbo_layout', 'pbo_semantics', 'pbo_limits', 'oc_frequency', 'oc_cores', 'pm_transport', 'curve_all', 'vf_sources', 'power_adjustment', *FIRMWARE_STUDIES), default='baseline')
    args = parser.parse_args()
    binary, rea, output = args.binary.resolve(), args.rea.resolve(), args.output.resolve()
    source_sha = FIRMWARE_STUDIES[args.study][0] if args.study in FIRMWARE_STUDIES else SOURCE_SHA
    if hashlib.sha256(binary.read_bytes()).hexdigest() != source_sha:
        raise ValueError('Unexpected reference binary')
    output.mkdir(parents=True, exist_ok=True)
    node = shutil.which('node')
    if not node or not rea.is_file():
        raise ValueError('Missing analysis runtime')
    sequence, records = 0, []
    startup_ms = int(os.environ.get('REA_GHIDRA_STARTUP_TIMEOUT_MS', '600000'))
    if not 1 <= startup_ms <= 2147483647:
        raise ValueError('Invalid Ghidra startup deadline')
    request_timeout = startup_ms / 1000 + 120

    def save(name, value):
        (output / (name + '.json')).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    with tempfile.TemporaryDirectory(prefix='octool-rea-') as temporary:
        # Deliberately do not inherit GitHub credentials or the caller's configs.
        env = {key: os.environ[key] for key in (
            'PATH', 'JAVA_HOME', 'GHIDRA_INSTALL_DIR', 'GHIDRA_HEADLESS_MAXMEM',
            'REA_GHIDRA_STARTUP_TIMEOUT_MS', 'REA_GHIDRA_DISABLED_ANALYZERS') if key in os.environ}
        env.update(HOME=temporary, TMPDIR=temporary, LANG='C.UTF-8',
                   REA_ANALYSIS_PROVIDER='ghidra')
        with (output / 'rea-stderr.log').open('w', encoding='utf-8') as errors:
            proc = subprocess.Popen([node, str(rea), 'mcp'], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=errors, text=True, encoding='utf-8',
                env=env, cwd=temporary, start_new_session=True)
            inbox = queue.Queue()

            def receive():
                try:
                    while True:
                        line = proc.stdout.readline(32 * 1024 * 1024 + 1)
                        if not line:
                            raise EOFError('REA closed its output')
                        if len(line) > 32 * 1024 * 1024:
                            raise ValueError('Oversized MCP frame')
                        inbox.put(json.loads(line))
                except Exception as error:
                    inbox.put(error)

            threading.Thread(target=receive, daemon=True).start()

            def send(method, params, request=True, timeout=request_timeout):
                nonlocal sequence
                sequence += 1
                message = dict(jsonrpc='2.0', method=method, params=params)
                if request:
                    message['id'] = sequence
                proc.stdin.write(json.dumps(message) + '\n')
                proc.stdin.flush()
                if not request:
                    return
                deadline = time.monotonic() + timeout
                while True:
                    reply = inbox.get(timeout=max(0.01, deadline - time.monotonic()))
                    if isinstance(reply, Exception):
                        raise reply
                    if reply.get('id') == sequence:
                        if 'error' in reply:
                            raise RuntimeError(json.dumps(reply['error']))
                        return reply['result']
                    if time.monotonic() >= deadline:
                        raise TimeoutError(method)

            def call(name, arguments, label):
                print(label, flush=True)
                started = time.monotonic()
                value = send('tools/call', dict(name=name, arguments=arguments))
                save(label, value)
                records.append(dict(tool=name, arguments=arguments, file=label + '.json',
                    seconds=round(time.monotonic() - started, 2), error=value.get('isError', False)))
                if value.get('isError'):
                    raise RuntimeError('REA operation failed; see ' + label + '.json')
                return value

            opened = False
            failure = None
            try:
                save('initialize', send('initialize', dict(protocolVersion='2024-11-05',
                    capabilities={}, clientInfo=dict(name='octool-static-study', version='1'))))
                send('notifications/initialized', {}, False)
                schemas, cursor = {}, None
                while True:
                    page = send('tools/list', {} if cursor is None else dict(cursor=cursor))
                    schemas.update({item['name']: item for item in page['tools']})
                    cursor = page.get('nextCursor')
                    if not cursor:
                        break
                names = ('open_binary', 'procedure_pseudo_code', 'procedure_info',
                         'procedure_callers', 'close_binary', 'export_evidence_bundle')
                save('selected-tool-schemas', {name: schemas[name] for name in names})
                call('open_binary', dict(path=str(binary), provider_id='ghidra'), 'open')
                opened = True
                resolved = []
                procedures = {'baseline': PROCEDURES, 'arrays': ARRAY_PROCEDURES,
                              'monitors': MONITOR_PROCEDURES, 'percore': PERCORE_PROCEDURES,
                              'pbo_layout': PBO_LAYOUT_PROCEDURES,
                              'pbo_semantics': PBO_SEMANTICS_PROCEDURES,
                              'pbo_limits': PBO_LIMITS_PROCEDURES,
                              'oc_frequency': OC_FREQUENCY_PROCEDURES,
                              'oc_cores': OC_CORES_PROCEDURES,
                              'pm_transport': PM_TRANSPORT_PROCEDURES,
                              'curve_all': CURVE_ALL_PROCEDURES,
                              'vf_sources': VF_SOURCES_PROCEDURES,
                              'power_adjustment': POWER_ADJUSTMENT_PROCEDURES,
                              **{key: value[1] for key, value in FIRMWARE_STUDIES.items()}}[args.study]
                for offset, name in procedures:
                    pseudo = call('procedure_pseudo_code',
                        dict(procedure='file-offset:' + offset), 'pseudo-' + offset)
                    address = selected_address(pseudo, offset)
                    info = call('procedure_info', dict(procedure=address), 'info-' + offset)
                    function = info['structuredContent']['normalized_result']
                    if function['entrypoint'] != address or name not in function['name']:
                        raise ValueError('Resolved function does not match the pinned study identity')
                    resolved.append(dict(file_offset=offset, analysis_address=address,
                        expected_name=name, function=function, evidence_id=pseudo['structuredContent']['evidence_id']))
                    save('resolved-functions', resolved)
                call('procedure_callers', dict(procedure=resolved[0]['analysis_address']),
                     'entry-callers' if args.study == 'percore' else 'pm-callers')
            except Exception as error:
                failure = error
            finally:
                if opened:
                    # Provider errors can exceed the MCP response budget. Export
                    # retained evidence before closing, even on a failed query.
                    try:
                        call('export_evidence_bundle', dict(path=str(output / 'evidence.json')), 'export')
                    except Exception as error:
                        save('export-error', dict(error=str(error)))
                        if failure is None:
                            failure = error
                    try:
                        call('close_binary', {}, 'close')
                    except Exception as error:
                        save('close-error', dict(error=str(error)))
                        if failure is None:
                            failure = error
                save('study', dict(schema=1, source_sha256=source_sha, hardware_execution=False,
                    rea_version='6.7.0', ghidra_version='12.1.4', records=records,
                    startup_timeout_ms=startup_ms,
                    disabled_analyzers=env.get('REA_GHIDRA_DISABLED_ANALYZERS', ''),
                    study=args.study,
                    error=None if failure is None else str(failure)))
                proc.stdin.close()
                try:
                    proc.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGTERM)
                    try:
                        proc.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait(timeout=10)
    if hashlib.sha256(binary.read_bytes()).hexdigest() != source_sha:
        raise ValueError('Reference changed during analysis')
    if failure is not None:
        raise failure
    print('Static study completed; no target execution or hardware acceptance', flush=True)


if __name__ == '__main__':
    main()
