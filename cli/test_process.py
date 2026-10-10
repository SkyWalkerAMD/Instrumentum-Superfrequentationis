#!/usr/bin/env python3
"""CLI process/JSON/affinity and sysfs fixture checks. No register device access."""
import binascii
import json
import os
import subprocess
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

lines = subprocess.check_output([fixture, "--emit"], text=True, env=env, timeout=30).splitlines()
reports = [json.loads(line) for line in lines]
assert len(reports) == 180, len(reports)
assert len([r for r in reports if r["error"] == -22]) >= 93
turbo = [r for r in reports if r["command"] in ("intel-turbo-read", "intel-turbo-set") and "before" in r["data"]]
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
controls = [r for r in reports if r["command"] == "intel-set" and "verified" in r.get("data", {})]
assert len(controls) == 10
for result in controls:
    data = result["data"]
    assert data["verified"] == result["ok"]
    if result["ok"]:
        assert data["readback_raw"] == data["expected_raw"]
        assert data["readback_value"] in (100, 192, 15000, 0)
        assert data["completed_writes"] == (0 if data["unchanged"] else 1)
    else:
        assert data["readback_value"] is None
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
vf = [r for r in reports if r["command"] == "intel-vf-read" and r["error"] != -22]
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
curve = [r for r in reports if r["command"] == "amd-curve-read" and r["ok"]][0]
assert curve["data"]["raw"] == "0xffffffe2" and curve["data"]["raw_signed"] == -30
curve_fail = [r for r in reports if r["command"] == "amd-curve-read" and r["error"] == -5][0]
assert curve_fail["data"]["raw"] is None and curve_fail["data"]["message_attempted"]
pstate = [r for r in reports if r["command"] == "amd-pstates" and r["ok"]][0]
assert pstate["data"]["rows"][0]["raw"] == "0xfffffffffffffc00"
assert pstate["data"]["rows"][2]["raw"] is None
umc = [r for r in reports if r["command"] == "amd-umc-read" and r["ok"]][0]
contract = json.loads((Path(__file__).resolve().parents[1] / "gui/tests/fixtures/cli-umc.json").read_text())
assert {k: umc[k] for k in contract if k != "data"} == {k: v for k, v in contract.items() if k != "data"}
assert {k: umc["data"][k] for k in contract["data"]} == contract["data"]
assert len(umc["data"]["fields"]) == 212 and len(umc["data"]["registers"]) == 56
assert len({f["id"] for f in umc["data"]["fields"]}) == 212
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
print("CLI process checks passed: no display, sparse affinity, 147 JSON reports, exact 64-bit registers, RAPL/HWP readback and activity window, voltage preservation and VF partial errors, inventory limits, SPD CRC/errors")
