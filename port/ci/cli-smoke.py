#!/usr/bin/env python3
"""Installed CLI in a fresh distribution, with no desktop or HW device."""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
for key in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM", "DBUS_SESSION_BUS_ADDRESS"):
    assert key not in os.environ
for program in ("qmake", "Xvfb", "Xwayland", "gcc", "g++", "cmake", "dkms"):
    assert not shutil.which(program), program
assert not Path("/dev/mydev").exists()
assert not Path("/opt/octool").exists()
linkage = subprocess.check_output(["ldd", "/usr/bin/octool-cli"], text=True)
assert not re.search(r"lib(?:Qt|X11|Xcb|xcb|wayland)|not found", linkage)


def run(*words, expected=0):
    p = subprocess.run(["octool-cli", *words], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    assert p.returncode == expected and not p.stderr, (words, p.returncode, p.stdout, p.stderr)
    result = json.loads(p.stdout)
    assert result["ok"] == (expected == 0)
    return result


diagnose = run("diagnose")
assert not diagnose["data"]["register_access_tested"] and not diagnose["data"]["display_required"]
cpu = str(diagnose["data"]["allowed_cpus"][-1])
identity = run("cpu", "--cpu", cpu)
assert identity["data"]["identity"]["vendor"] in ("GenuineIntel", "AuthenticAMD")
invalid = run("intel-set", "--cpu", cpu, "--field", "pl1", "--value", "100", expected=2)
assert "--apply" in invalid["error_message"]
run("amd-curve-read", "--cpu", cpu, "--ccd", "16", "--core", "0", expected=2)
run("spd-decode", "--file", "/nonexistent-octool-spd", expected=3)
assert "no Qt" in subprocess.check_output(["octool-cli", "--help"], text=True)
# Minimal Ubuntu images exclude /usr/share/doc via dpkg path-exclude; some
# RPM images similarly enable nodocs. The package contains the guide, but a
# target's installation policy may omit it without breaking the CLI.
guide_installed = Path("/usr/share/doc/octool-cli/headless-cli.md").is_file()
args.output.write_text(json.dumps({"passed": True, "no_display": True, "no_build_tools": True,
    "no_hardware_device": True, "register_access_tested": False, "diagnose": diagnose,
    "identity": identity, "ldd": linkage, "guide_installed": guide_installed,
    "binary_bytes": Path("/usr/bin/octool-cli").stat().st_size}, indent=2) + "\n")
print("Installed CLI passed without GUI, compiler, DKMS or a hardware device")
