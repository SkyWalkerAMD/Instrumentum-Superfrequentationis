#!/usr/bin/env python3
"""Real LD_PRELOAD lifecycle/fault regressions with an ordinary file mailbox.

Runs compiled production capture and parity, plus a userspace-only responder.
No /dev/mydev, MSR, PCI, ports, module loading or original GUI is used.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
GOOD = (
    "success", "double-map", "read-protect", "dup-same", "dup-fail",
    "reuse", "dup-replace", "reopen-alias", "openat-alias", "openat64-alias",
    "mutate-request", "trace-short", "trace-eintr", "delayed", "unmap-tail",
)
BAD = (
    "munmap", "mprotect", "mremap", "fixed", "small-map", "private-map",
    "dup", "dup2", "dup3", "fcntl-alias", "unmap-during", "close-during", "overlap",
    "write-fail", "short-write", "bad-pointer", "bad-count", "timeout",
    "driver-error", "bad-done", "trace-fail", "trace-partial-fail",
    "finalize-fail", "marker-partial", "sync-fail", "abrupt", "signal", "fork", "exec-child",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    observations = []
    with tempfile.TemporaryDirectory(prefix="octool-capture-") as tmp:
        base = Path(tmp)
        for case in GOOD + BAD:
            directory = base / case
            directory.mkdir()
            device = directory / "mailbox-file"
            device.write_bytes(b"\0" * 8192)
            alias = directory / "alias"
            alias.symlink_to(device)
            trace = directory / "trace.bin"
            env = os.environ.copy()
            env.update(OCTOOL_CAP_DEV=str(device), OCTOOL_CAP_OUT=str(trace),
                       CAPTURE_ALIAS=str(alias), LD_PRELOAD=":".join(str(ROOT / f) for f in
                       ("octool_capture.so", "capture_backend.so")))
            child = subprocess.run([str(ROOT / "capture-harness"), case], env=env,
                                   capture_output=True, text=True, timeout=12)
            expected = -signal.SIGTERM if case == "signal" else 0
            assert child.returncode == expected, (case, child.returncode, child.stderr)
            data = trace.read_bytes()
            assert len(data) >= 32, (case, child.stderr)
            magic, reqsz, mboxw, nrec = struct.unpack("<4Q", data[:32])
            assert (reqsz, mboxw) == (96, 5), case
            check = subprocess.run([str(ROOT / "octool_parity"), "--check-trace", "--trace", str(trace)],
                                   capture_output=True, text=True, timeout=3)
            if case in GOOD:
                expected_records = 1 if case in ("reuse", "dup-replace") else 2
                assert (magic, nrec, len(data)) == (0x4f43545250520002, expected_records,
                                                   32 + expected_records * 152), case
                for index in range(expected_records):
                    rec = data[32+index*152:32+(index+1)*152]
                    req = struct.unpack("<12Q", rec[:96])
                    assert req == (0x0c, 71, 0x1000 + index*0x1000, 0xfeed, 4, 5, 6, 7, 8, 9, 10, 11), case
                    words = struct.unpack("<6Qii", rec[96:])
                    assert words[:5] == (1, 0x123456789abcdef0, 2, 3, 4), case
                    expected_seq = 0 if case in ("reopen-alias", "openat-alias", "openat64-alias") else index
                    assert words[5:] == (expected_seq, 96, 1), (case, words)
                assert check.returncode == 0 and "trace-check OK" in check.stdout, (case, check.stderr)
                assert "INCOMPLETE" not in child.stderr, (case, child.stderr)
            else:
                assert magic != 0x4f43545250520002, (case, "invalid capture committed")
                assert check.returncode == 2 and "capture incomplete" in check.stderr, (case, check.stderr)
                if case not in ("abrupt", "signal"):
                    assert "INCOMPLETE" in child.stderr, (case, child.stderr)
            observations.append({"case": case, "process_exit": child.returncode,
                                 "trace_accepted": check.returncode == 0,
                                 "trace_bytes": len(data), "diagnostic": child.stderr.strip()})
    result = {"passed": True, "hardware_io": False, "observations": observations}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("capture lifecycle: {} cases passed ({} finalized, {} rejected), no hardware".format(
        len(observations), len(GOOD), len(BAD)))


if __name__ == "__main__":
    main()
