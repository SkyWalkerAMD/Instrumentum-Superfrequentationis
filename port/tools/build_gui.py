#!/usr/bin/env python3
"""Build the configured OCTool qmake project and its offline GUI regressions."""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def source_path(value):
    relative = Path(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("GUI inputs must use repository-relative paths without '..': " + value)
    result = (ROOT / relative).resolve()
    if ROOT not in result.parents:
        raise ValueError("GUI inputs must be inside the source tree: " + value)
    return result


def config():
    cfg = json.loads((ROOT / "port/gui/build.json").read_text(encoding="utf-8"))
    project = source_path(cfg["project"])
    if not project.is_file():
        raise ValueError("GUI source missing: {}. Supply restored/reconstructed source and set "
                         "port/gui/build.json; prebuilt octool-linux.zip is not source.".format(project))
    if not source_path(cfg["license_file"]).is_file():
        raise ValueError("GUI distribution license file missing: " + cfg["license_file"])
    if not source_path(cfg["helper_project"]).is_file():
        raise ValueError("hardware helper source missing: " + cfg["helper_project"])
    return cfg, project


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/gui")
    parser.add_argument("--stage", type=Path, default=ROOT / "build/gui-stage")
    parser.add_argument("--qmake", default="/opt/octool-qt/bin/qmake")
    parser.add_argument("--floor", choices=("el7", "el8"), default="el8")
    args = parser.parse_args()
    link_args = (["QMAKE_LFLAGS+=-static-libstdc++ -static-libgcc"] if args.floor == "el7" else [])
    try:
        cfg, project = config()
    except (ValueError, OSError) as exc:
        parser.exit(2, str(exc) + "\n")
    if args.preflight:
        print("GUI input found: " + str(project))
        return
    if args.stage.exists() and any(args.stage.iterdir()):
        parser.error("GUI staging must be empty; choose a fresh --stage directory")
    args.build_dir.mkdir(parents=True, exist_ok=True)
    qt_config = subprocess.check_output([args.qmake, "-query", "QT_INSTALL_ARCHDATA"], text=True).strip()
    pri = (Path(qt_config) / "mkspecs/qconfig.pri").read_text()
    if not any(line.startswith("QT_CONFIG") and "static" in line.split()
               for line in pri.splitlines()):
        parser.error("a static Qt SDK is required")
    subprocess.run([args.qmake, str(project), "CONFIG+=release", "CONFIG-=debug",
                    "QMAKE_CFLAGS+=-march=x86-64 -mtune=generic",
                    "QMAKE_CXXFLAGS+=-march=x86-64 -mtune=generic", *cfg["qmake_args"], *link_args],
                   cwd=args.build_dir, check=True)
    subprocess.run(["make", "-j" + os.environ.get("JOBS", "2")], cwd=args.build_dir, check=True)
    if cfg.get("test_project"):
        tests = args.build_dir / "regression"
        tests.mkdir(exist_ok=True)
        subprocess.run([args.qmake, str(source_path(cfg["test_project"])), "CONFIG+=release", *link_args],
                       cwd=tests, check=True)
        subprocess.run(["make", "-j" + os.environ.get("JOBS", "2")], cwd=tests, check=True)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                   OCTOOL_SPD_TEST_SCREENSHOT=str((tests / "spd-fixture.png").resolve()),
                   OCTOOL_EXPO_TEST_SCREENSHOT=str((tests / "expo-fixture.png").resolve()),
                   OCTOOL_DDR4_XMP_TEST_SCREENSHOT=str((tests / "ddr4-xmp-fixture.png").resolve()),
                   OCTOOL_XMP_SECONDARY_TEST_SCREENSHOT=str((tests / "xmp-secondary-fixture.png").resolve()),
                   OCTOOL_CAS_TEST_SCREENSHOT=str((tests / "cas-fixture.png").resolve()),
                   OCTOOL_SPD_BASE_TEST_SCREENSHOT=str((tests / "base-timings-fixture.png").resolve()),
                   OCTOOL_DDR4_BASE_TEST_SCREENSHOT=str((tests / "ddr4-base-fixture.png").resolve()),
                   OCTOOL_XMP2_METADATA_TEST_SCREENSHOT=str((tests / "xmp2-metadata-fixture.png").resolve()),
                   OCTOOL_GEOMETRY_TEST_SCREENSHOT=str((tests / "geometry-fixture.png").resolve()),
                   OCTOOL_ASYMMETRIC_TEST_SCREENSHOT=str((tests / "asymmetric-fixture.png").resolve()),
                   OCTOOL_ASYMMETRIC_CAPACITY_TEST_SCREENSHOT=str((tests / "asymmetric-capacity-fixture.png").resolve()),
                   OCTOOL_INTEL_MEMORY_TEST_SCREENSHOT=str((tests / "intel-memory-fixture.png").resolve()),
                   OCTOOL_INTEL_REFRESH_TEST_SCREENSHOT=str((tests / "intel-refresh-fixture.png").resolve()),
                   OCTOOL_INTEL_TVB_TEST_SCREENSHOT=str((tests / "intel-tvb-fixture.png").resolve()),
                   OCTOOL_INTEL_LIMITS_TEST_SCREENSHOT=str((tests / "intel-limits-fixture.png").resolve()),
                   OCTOOL_INTEL_THERMAL_TEST_SCREENSHOT=str((tests / "intel-thermal-fixture.png").resolve()),
                   OCTOOL_INTEL_PACKAGE_EVENTS_SCREENSHOT=str((tests / "intel-package-events-fixture.png").resolve()),
                   OCTOOL_INTEL_BCLK_VOLTAGE_TEST_SCREENSHOT=str((tests / "intel-bclk-voltage-fixture.png").resolve()),
                   OCTOOL_INTEL_TELEMETRY_TEST_SCREENSHOT=str((tests / "intel-telemetry-fixture.png").resolve()),
                   OCTOOL_INTEL_AVX_TEST_SCREENSHOT=str((tests / "intel-avx-fixture.png").resolve()),
                   OCTOOL_INTEL_ENERGY_TEST_SCREENSHOT=str((tests / "intel-energy-fixture.png").resolve()),
                   OCTOOL_INTEL_TVB_TABLE_TEST_SCREENSHOT=str((tests / "intel-tvb-table-fixture.png").resolve()),
                   OCTOOL_INTEL_TVB_TABLE_EDIT_TEST_SCREENSHOT=str((tests / "intel-tvb-table-edit-fixture.png").resolve()),
                   OCTOOL_INTEL_CLOCK_TEST_SCREENSHOT=str((tests / "intel-clock-fixture.png").resolve()),
                   OCTOOL_DDR4_ASYMMETRIC_TEST_SCREENSHOT=str((tests / "ddr4-asymmetric-fixture.png").resolve()),
                   OCTOOL_USER_SPD_TEST_SCREENSHOT=str((tests / "user-spd-fixture.png").resolve()),
                   OCTOOL_ENHANCED_SPD_TEST_SCREENSHOT=str((tests / "enhanced-spd-fixture.png").resolve()),
                   OCTOOL_UNCORE_TEST_SCREENSHOT=str((tests / "uncore-fixture.png").resolve()),
                   OCTOOL_TURBO_TEST_SCREENSHOT=str((tests / "turbo-fixture.png").resolve()),
                   OCTOOL_TURBO_THRESHOLD_TEST_SCREENSHOT=str((tests / "turbo-threshold-fixture.png").resolve()),
                   OCTOOL_CONTROL_TEST_SCREENSHOT=str((tests / "controls-fixture.png").resolve()),
                   OCTOOL_VF_TEST_SCREENSHOT=str((tests / "vf-fixture.png").resolve()),
                   OCTOOL_VF_EDIT_TEST_SCREENSHOT=str((tests / "vf-edit-fixture.png").resolve()),
                   OCTOOL_UMC_DIFF_TEST_SCREENSHOT=str((tests / "umc-diff-fixture.png").resolve()),
                   OCTOOL_PM_TEST_SCREENSHOT=str((tests / "pm-fixture.png").resolve()),
                   OCTOOL_PM_ERROR_TEST_SCREENSHOT=str((tests / "pm-error-fixture.png").resolve()))
        try:
            subprocess.run([str((tests / "gui-regression").resolve()), "-o", "gui-tests.txt,txt"],
                           cwd=tests, env=env, check=True, timeout=120)
        finally:
            log = tests / "gui-tests.txt"
            if log.exists():
                print(log.read_text(), flush=True)
    binary = (args.build_dir / cfg["binary"]).resolve()
    if args.build_dir.resolve() not in binary.parents or not binary.is_file():
        parser.error("configured GUI binary was not produced: " + str(binary))
    destination = args.stage / "opt/octool"
    (destination / "bin").mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination / "bin/octool-real")
    os.chmod(destination / "bin/octool-real", 0o755)
    helper_dir = args.build_dir / "helper"
    helper_dir.mkdir(exist_ok=True)
    subprocess.run([args.qmake, str(source_path(cfg["helper_project"])), "CONFIG+=release",
                    "QMAKE_CFLAGS+=-march=x86-64 -mtune=generic",
                    "QMAKE_CXXFLAGS+=-march=x86-64 -mtune=generic", *link_args], cwd=helper_dir, check=True)
    subprocess.run(["make", "-j" + os.environ.get("JOBS", "2")], cwd=helper_dir, check=True)
    helper = (helper_dir / cfg["helper_binary"]).resolve()
    if helper_dir.resolve() not in helper.parents or not helper.is_file():
        parser.error("configured hardware helper was not produced: " + str(helper))
    shutil.copy2(helper, destination / "bin/octool-hwio-helper")
    os.chmod(destination / "bin/octool-hwio-helper", 0o755)
    (destination / "licenses").mkdir(exist_ok=True)
    shutil.copy2(source_path(cfg["license_file"]), destination / "licenses/octool-LICENSE")
    sdk_licenses = Path(qt_config) / "licenses"
    if not sdk_licenses.is_dir():
        parser.error("static Qt SDK license inventory missing")
    shutil.copytree(sdk_licenses, destination / "licenses/qt", dirs_exist_ok=True)
    for item in cfg["resources"]:
        src = source_path(item)
        dest = destination / "share" / item
        dest.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, dest, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dest)
    shutil.copy2(ROOT / "port/gui/build.json", destination / "build.json")


if __name__ == "__main__":
    main()
