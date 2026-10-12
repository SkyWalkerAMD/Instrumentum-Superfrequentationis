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

lines = subprocess.check_output([fixture, "--emit"], text=True, env=env, timeout=30).splitlines()
reports = [json.loads(line) for line in lines]
assert len(reports) == 462, len(reports)
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
assert len(imc_reports) == 40
assert [r["ok"] for r in imc_reports] == ([True]*4+[False]*6)*2 + [True]*4+[False]*4 + [True]*6+[False]*6
for i,r in enumerate(imc_reports[:20]):
    memory = r["data"]["memory"]
    if not r["ok"]:
        assert memory is None
        continue
    assert memory["format"] == "octool-intel-memory-v3"
    assert len(memory["registers"]) == 18 and len(memory["fields"]) == 57
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
for i,r in enumerate(imc_reports[28:]):
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
with tempfile.TemporaryDirectory(prefix="octool-intel-memory-") as directory:
    path = Path(directory)/"内存快照.json"
    for source in (imc_reports[0]["data"]["memory"],imc_reports[0],imc_reports[10]):
        path.write_text(json.dumps(source),encoding="utf-8")
        decoded = run("intel-memory-decode","--file",str(path))
        assert decoded["data"]["memory"]["registers"] == imc_reports[0]["data"]["memory"]["registers"]
        assert decoded["data"]["memory"]["fields"][0]["encoded"] == 36
    for source in imc_reports[20:24]+imc_reports[28:34]:
        path.write_text(json.dumps(source),encoding="utf-8")
        decoded = run("intel-memory-decode","--file",str(path))
        assert decoded["data"]["memory"] == source["data"]["memory"]
    path.write_text(json.dumps(imc_reports[4]),encoding="utf-8")
    assert run("intel-memory-decode","--file",str(path),code=3)["data"]["memory"] is None
    os.mkfifo(Path(directory)/"fifo")
    assert run("intel-memory-decode","--file",str(Path(directory)/"fifo"),code=3)["data"]["memory"] is None
pm_reports = [r for r in reports if r["command"] == "amd-pm-decode" and "pm" in r["data"]]
assert len(pm_reports) == 20
assert [r["ok"] for r in pm_reports] == [True] * 9 + [False, True, True, False, False, False, False, True, True, False, False]
for i, r in enumerate(pm_reports):
    assert r["data"]["offline"] and not r["data"]["table_read_attempted"] and not r["data"]["direct_register_access"]
    pm = r["data"]["pm"]
    if r["ok"]:
        assert len(pm["fields"]) == 24 and len(pm["raw_hex"]) == pm["expected_bytes"] * 2
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
with tempfile.TemporaryDirectory(prefix="octool-pm-") as directory:
    root = Path(directory)
    capture = pm_reports[8]["data"]["pm"]
    path = root / '快照-".json'
    path.write_text(json.dumps(capture), encoding="utf-8")
    decoded = run("amd-pm-decode", "--file", str(path))
    assert decoded["data"]["pm"] == capture
    path.write_text(json.dumps(decoded), encoding="utf-8")
    assert run("amd-pm-decode", "--file", str(path))["data"]["pm"] == capture
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
