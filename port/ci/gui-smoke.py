#!/usr/bin/env python3
"""Require a visible named window owned by the real GUI process, then dwell.
No fake device, LD_PRELOAD hardware stub, root privilege or hardware writes.
"""
import argparse
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path


def visible_window(probe, pid, title):
    tree = subprocess.check_output([str(probe), str(pid)], text=True)
    for line in tree.splitlines():
        window, encoded = line.split("\t", 1)
        int(window, 16)
        name = bytes.fromhex(encoded).decode("utf-8")
        if title.search(name):
            return name
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/usr/bin/octool")
    parser.add_argument("--config", type=Path, default=Path("/opt/octool/build.json"))
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--probe", type=Path, default=Path("/out/window-probe"))
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error("run the GUI smoke as an unprivileged user")
    cfg = json.loads(args.config.read_text())
    title = re.compile(cfg["window_title_regex"])
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("w") as log:
        env = dict(os.environ, OCTOOL_SMOKE_SCREENSHOT=str(args.log.with_suffix("")))
        proc = subprocess.Popen([args.binary], stdout=log, stderr=subprocess.STDOUT, env=env,
                                start_new_session=True)
        seen, start = None, time.monotonic()
        try:
            while time.monotonic() - start < 30:
                if proc.poll() is not None:
                    raise RuntimeError("GUI exited before smoke completed, rc=" + str(proc.returncode))
                window = visible_window(args.probe, proc.pid, title)
                if window:
                    if seen is None:
                        seen = time.monotonic()
                    if time.monotonic() - seen >= 5:
                        # The shell/probe PID cannot own the GUI's window. Check
                        # that the observer does not accept title alone.
                        if visible_window(args.probe, os.getpid(), title):
                            raise RuntimeError("window ownership negative control failed")
                        print("GUI SMOKE PASS: visible window, pid={}, title={!r}".format(proc.pid, window))
                        return 0
                else:
                    seen = None
                time.sleep(0.2)
            raise RuntimeError("no persistent visible OCTool window within 30 seconds")
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()


if __name__ == "__main__":
    raise SystemExit(main())
