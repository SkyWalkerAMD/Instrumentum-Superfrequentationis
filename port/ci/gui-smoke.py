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


def visible_window(pid, title):
    tree = subprocess.check_output(["xwininfo", "-root", "-tree"], text=True)
    for match in re.finditer(r'^\s*(0x[0-9a-fA-F]+)\s+"([^"\n]*)"', tree, re.M):
        window, name = match.groups()
        if not title.search(name):
            continue
        prop = subprocess.check_output(["xprop", "-id", window, "_NET_WM_PID"], text=True)
        if re.search(r'=\s*' + str(pid) + r'\s*$', prop) is None:
            continue
        info = subprocess.check_output(["xwininfo", "-id", window], text=True)
        if "Map State: IsViewable" in info:
            return name
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/usr/bin/octool")
    parser.add_argument("--config", type=Path, default=Path("/opt/octool/build.json"))
    parser.add_argument("--log", type=Path, required=True)
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
                window = visible_window(proc.pid, title)
                if window:
                    if seen is None:
                        seen = time.monotonic()
                    if time.monotonic() - seen >= 5:
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
