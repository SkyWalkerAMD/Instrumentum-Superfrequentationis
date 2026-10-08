#!/usr/bin/env python3
"""Stage the existing DKMS skeleton and use native dpkg/rpmbuild tooling."""
import argparse
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path
from check_elf import elf_info, violations

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "port/packaging"


def version():
    value = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise ValueError("VERSION must be x.y.z")
    return value


def write(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(data)
    path.chmod(mode)


def copy(source, target, mode=0o644):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    target.chmod(mode)


def module_stage(stage, ver):
    src = stage / ("usr/src/octool-hwio-" + ver)
    for name in ("octool_hwio.c", "octool_hwio_abi.h", "octool_bus_access.h",
                 "class_create_probe.c", "Kbuild", "Makefile"):
        copy(ROOT / "port/kmod" / name, src / "kmod" / name)
    copy(ROOT / "port/abi/octool_hwio_abi.h", src / "abi/octool_hwio_abi.h")
    conf = (PACK / "dkms.conf").read_text()
    conf = re.sub(r'^PACKAGE_VERSION=.*$', 'PACKAGE_VERSION="' + ver + '"', conf, flags=re.M)
    write(src / "dkms.conf", conf)
    for name in ("dkms-register.sh", "dkms-sign.sh"):
        copy(PACK / name, stage / "usr/libexec/octool" / name, 0o755)
    copy(PACK / "99-octool-hwio.rules", stage / "usr/lib/udev/rules.d/99-octool-hwio.rules")
    copy(PACK / "octool-msr.conf", stage / "usr/lib/modules-load.d/octool-msr.conf")


def gui_stage(stage, gui):
    binary = gui / "opt/octool/bin/octool-real"
    errors = violations(elf_info(binary))
    if errors:
        raise ValueError("EL8 release ABI gate failed: " + "; ".join(errors))
    for path in (gui / "opt/octool").rglob("*"):
        if re.match(r"^(ld-linux|lib(c|m|pthread|dl|rt|resolv)\.so\.)", path.name):
            raise ValueError("system glibc/loader must not be bundled: " + str(path))
        if path.is_symlink():
            resolved = path.resolve()
            if (gui / "opt/octool").resolve() not in resolved.parents:
                raise ValueError("GUI payload symlink escapes its private directory: " + str(path))
    # Only the built payload is accepted, never the old mylib/ directory.
    shutil.copytree(gui / "opt", stage / "opt")
    copy(ROOT / "port/runtime/octool", stage / "usr/bin/octool", 0o755)
    copy(ROOT / "port/runtime/octool.desktop", stage / "usr/share/applications/octool.desktop")
    shutil.copytree(ROOT / "docs", stage / "usr/share/doc/octool")


def deb(stage, ver, output, gui=False):
    if gui:
        # dpkg-shlibdeps uses this minimal source control file for context.
        write(stage / "debian/control", "Source: octool\n\nPackage: octool\nArchitecture: amd64\n")
        result = subprocess.check_output(
            ["dpkg-shlibdeps", "-O", "-e" + str(stage / "opt/octool/bin/octool-real")],
            cwd=stage, text=True)
        deps = next(line.split("=", 1)[1] for line in result.splitlines()
                    if line.startswith("shlibs:Depends="))
        shutil.rmtree(stage / "debian")
        template = "gui.control.in"
    else:
        deps, template = "", "module.control.in"
        write(stage / "DEBIAN/postinst", "#!/bin/sh\nset -eu\nif [ \"$1\" = configure ]; then\n"
              "  /usr/libexec/octool/dkms-register.sh " + ver + "\nfi\n", 0o755)
        write(stage / "DEBIAN/prerm", "#!/bin/sh\nset -eu\ncase \"$1\" in remove|upgrade|deconfigure)\n"
              "  if [ -n \"$(dkms status -m octool-hwio -v " + ver + ")\" ]; then\n"
              "    dkms remove -m octool-hwio -v " + ver + " --all\n  fi\n;; esac\n", 0o755)
    control = (PACK / "debian" / template).read_text().replace("@VERSION@", ver).replace("@SHLIBS@", deps)
    write(stage / "DEBIAN/control", control)
    name = "octool" if gui else "octool-hwio-dkms"
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zgzip", "--build", str(stage),
                    str(output / (name + "-" + ver + "-1.amd64.deb"))], check=True)


def rpm(stage, ver, output, workspace, gui=False):
    top = workspace / ("rpm-gui" if gui else "rpm-module")
    for directory in ("SOURCES", "SPECS", "BUILD", "BUILDROOT", "RPMS", "SRPMS"):
        (top / directory).mkdir(parents=True)
    name = "octool" if gui else "octool-hwio"
    suffix = "runtime" if gui else "src"
    tar_path = top / "SOURCES" / (name + "-" + ver + "-" + suffix + ".tar.gz")
    with tarfile.open(tar_path, "w:gz") as archive:
        archive.add(stage, arcname=name + "-" + ver)
    template = PACK / ("octool.spec.in" if gui else "octool-hwio-dkms.spec")
    spec = top / "SPECS" / "octool.spec"
    write(spec, template.read_text().replace("@VERSION@", ver))
    subprocess.run(["rpmbuild", "-bb", "--define", "_topdir " + str(top), str(spec)], check=True)
    for artifact in (top / "RPMS").rglob("*.rpm"):
        shutil.copy2(artifact, output / artifact.name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--format", choices=["deb", "rpm"], required=True)
    parser.add_argument("--module-only", action="store_true")
    parser.add_argument("--gui-stage", type=Path, default=ROOT / "build/gui-stage")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    ver = version()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="octool-pack-") as tmp:
        workspace = Path(tmp)
        for gui in ([False] if args.module_only else [False, True]):
            stage = workspace / ("gui" if gui else "module")
            stage.mkdir()
            if gui:
                gui_stage(stage, args.gui_stage.resolve())
            else:
                module_stage(stage, ver)
            if args.format == "deb":
                deb(stage, ver, args.output, gui)
            else:
                rpm(stage, ver, args.output, workspace, gui)


if __name__ == "__main__":
    main()
