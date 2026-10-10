#!/usr/bin/env python3
"""Package the native CLI without Qt, display, polkit or DKMS dependencies."""
import argparse
import json
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
from build_packages import ROOT, copy, version, write
from check_elf import elf_info

ALLOWED = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
           "libstdc++.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2"}


def stage_cli(stage, binary):
    info = elf_info(binary)
    if set(info["needed"]) - ALLOWED or info["rpaths"]:
        raise ValueError("CLI must link only the target system C/C++ runtime: " + json.dumps(info))
    if len(info["gnu_stack_flags"]) != 1 or info["gnu_stack_flags"][0] & 1:
        raise ValueError("CLI must have a non-executable stack")
    copy(binary, stage / "usr/bin/octool-cli", 0o755)
    subprocess.run(["strip", "--strip-unneeded", str(stage / "usr/bin/octool-cli")], check=True)
    copy(ROOT / "gui/LICENSE", stage / "usr/share/licenses/octool-cli/LICENSE")
    copy(ROOT / "docs/headless-cli.md", stage / "usr/share/doc/octool-cli/headless-cli.md")
    copy(ROOT / "docs/umc-offline.md", stage / "usr/share/doc/octool-cli/umc-offline.md")
    copy(ROOT / "docs/intel-uncore-recovery.md", stage / "usr/share/doc/octool-cli/intel-uncore-recovery.md")
    return info


def package_deb(stage, output, ver):
    write(stage / "debian/control", "Source: octool-cli\n\nPackage: octool-cli\nArchitecture: amd64\n")
    report = subprocess.check_output(["dpkg-shlibdeps", "-O", "-e" + str(stage / "usr/bin/octool-cli")], cwd=stage, text=True)
    deps = next(line.split("=", 1)[1] for line in report.splitlines() if line.startswith("shlibs:Depends="))
    shutil.rmtree(stage / "debian")
    write(stage / "DEBIAN/control", "Package: octool-cli\nVersion: " + ver + "-1\nArchitecture: amd64\n"
          "Section: admin\nPriority: optional\nMaintainer: OCTool maintainers <octool@example.invalid>\n"
          "Depends: " + deps + "\nSuggests: octool-hwio-dkms\n"
          "Description: OCTool command-line hardware interface without a desktop\n"
          " Uses the existing OCTool core and Linux backend; no Qt or display server.\n")
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zgzip", "--build", str(stage),
                    str(output / ("octool-cli-" + ver + "-1.amd64.deb"))], check=True)


def package_rpm(stage, output, ver, workspace):
    top = workspace / "rpm"
    for name in ("SOURCES", "SPECS", "BUILD", "BUILDROOT", "RPMS", "SRPMS"):
        (top / name).mkdir(parents=True)
    with tarfile.open(top / "SOURCES" / ("octool-cli-" + ver + ".tar.gz"), "w:gz") as archive:
        archive.add(stage, arcname="octool-cli-" + ver)
    spec = top / "SPECS/octool-cli.spec"
    write(spec, (ROOT / "port/packaging/octool-cli.spec.in").read_text().replace("@VERSION@", ver))
    subprocess.run(["rpmbuild", "-bb", "--define", "_topdir " + str(top), str(spec)], check=True)
    for package in (top / "RPMS").rglob("*.rpm"):
        if "debuginfo" not in package.name and "debugsource" not in package.name:
            shutil.copy2(package, output / package.name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=("deb", "rpm"), required=True)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/cli/octool-cli")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/cli")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="octool-cli-pack-") as temp:
        workspace = Path(temp)
        stage = workspace / "stage"
        stage.mkdir()
        info = stage_cli(stage, args.binary.resolve())
        if args.format == "deb":
            package_deb(stage, output, version())
        else:
            package_rpm(stage, output, version(), workspace)
        (output / "cli-elf.json").write_text(json.dumps(info, indent=2) + "\n")


if __name__ == "__main__":
    main()
