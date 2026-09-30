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
    return cfg, project


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/gui")
    parser.add_argument("--stage", type=Path, default=ROOT / "build/gui-stage")
    parser.add_argument("--qmake", default="/opt/octool-qt/bin/qmake")
    args = parser.parse_args()
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
                    "QMAKE_CXXFLAGS+=-march=x86-64 -mtune=generic", *cfg["qmake_args"]],
                   cwd=args.build_dir, check=True)
    subprocess.run(["make", "-j" + os.environ.get("JOBS", "2")], cwd=args.build_dir, check=True)
    if cfg.get("test_project"):
        tests = args.build_dir / "regression"
        tests.mkdir(exist_ok=True)
        subprocess.run([args.qmake, str(source_path(cfg["test_project"])), "CONFIG+=release"],
                       cwd=tests, check=True)
        subprocess.run(["make", "-j" + os.environ.get("JOBS", "2")], cwd=tests, check=True)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        subprocess.run([str((tests / "gui-regression").resolve()), "-o", "gui-tests.txt,txt"],
                       cwd=tests, env=env, check=True, timeout=120)
        print((tests / "gui-tests.txt").read_text())
    binary = (args.build_dir / cfg["binary"]).resolve()
    if args.build_dir.resolve() not in binary.parents or not binary.is_file():
        parser.error("configured GUI binary was not produced: " + str(binary))
    destination = args.stage / "opt/octool"
    (destination / "bin").mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, destination / "bin/octool-real")
    os.chmod(destination / "bin/octool-real", 0o755)
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
