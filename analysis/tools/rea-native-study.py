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
# Exact symbol starts in the hash-verified original ELF.
PROCEDURES = ('0x2e7360', '0x2c7910', '0x2254c0', '0x21e770', '0x21e6f0')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--rea', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary, rea, output = args.binary.resolve(), args.rea.resolve(), args.output.resolve()
    if hashlib.sha256(binary.read_bytes()).hexdigest() != SOURCE_SHA:
        raise ValueError('Unexpected reference binary')
    output.mkdir(parents=True, exist_ok=True)
    node = shutil.which('node')
    if not node or not rea.is_file():
        raise ValueError('Missing analysis runtime')
    sequence, records = 0, []

    def save(name, value):
        (output / (name + '.json')).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    with tempfile.TemporaryDirectory(prefix='octool-rea-') as temporary:
        # Deliberately do not inherit GitHub credentials or the caller's configs.
        env = {key: os.environ[key] for key in (
            'PATH', 'JAVA_HOME', 'GHIDRA_INSTALL_DIR', 'GHIDRA_HEADLESS_MAXMEM',
            'REA_GHIDRA_STARTUP_TIMEOUT_MS') if key in os.environ}
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

            def send(method, params, request=True, timeout=2520):
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
                for address in PROCEDURES:
                    call('procedure_pseudo_code', dict(procedure=address), 'pseudo-' + address)
                    call('procedure_info', dict(procedure=address), 'info-' + address)
                call('procedure_callers', dict(procedure=PROCEDURES[0]), 'pm-callers')
                call('export_evidence_bundle', dict(path=str(output / 'evidence.json')), 'export')
            finally:
                if opened:
                    try:
                        call('close_binary', {}, 'close')
                    except Exception as error:
                        save('close-error', dict(error=str(error)))
                save('study', dict(schema=1, source_sha256=SOURCE_SHA, hardware_execution=False,
                    rea_version='6.7.0', ghidra_version='12.1.4', records=records))
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
    print('Static study completed; no target execution or hardware acceptance', flush=True)


if __name__ == '__main__':
    main()
