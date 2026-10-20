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
    parser.add_argument('--study', choices=('baseline', 'arrays', 'monitors', 'percore'), default='baseline')
    args = parser.parse_args()
    binary, rea, output = args.binary.resolve(), args.rea.resolve(), args.output.resolve()
    if hashlib.sha256(binary.read_bytes()).hexdigest() != SOURCE_SHA:
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
                              'monitors': MONITOR_PROCEDURES, 'percore': PERCORE_PROCEDURES}[args.study]
                for offset, name in procedures:
                    pseudo = call('procedure_pseudo_code',
                        dict(procedure='file-offset:' + offset), 'pseudo-' + offset)
                    address = selected_address(pseudo, offset)
                    info = call('procedure_info', dict(procedure=address), 'info-' + offset)
                    function = info['structuredContent']['normalized_result']
                    if function['entrypoint'] != address or name not in function['name']:
                        raise ValueError('Resolved function does not match the original ELF symbol')
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
                save('study', dict(schema=1, source_sha256=SOURCE_SHA, hardware_execution=False,
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
    if hashlib.sha256(binary.read_bytes()).hexdigest() != SOURCE_SHA:
        raise ValueError('Reference changed during analysis')
    if failure is not None:
        raise failure
    print('Static study completed; no target execution or hardware acceptance', flush=True)


if __name__ == '__main__':
    main()
