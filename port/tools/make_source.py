#!/usr/bin/env python3
"""Create a source-only archive with stable metadata and dot-separated version."""
import argparse
import gzip
import hashlib
import os
import tarfile
from pathlib import Path
from build_packages import ROOT, version

SKIP_DIRS = {".git", "build", "dist", "__pycache__", ".tmp_versions"}
SKIP_SUFFIXES = {".pyc", ".o", ".a", ".so", ".ko", ".mod", ".zip", ".key", ".der", ".pem"}
SKIP_NAMES = {"loopback", "transport", "bus-acpi", "bus-noacpi", "port-thread",
              "octool_parity", "hwio_smoke", "gui-regression", "Module.symvers", "modules.order"}


def create(output):
    ver = version()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / ("octool-" + ver + "-src.tar.gz")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "1790726400"))
    files = []
    for directory, subdirs, names in os.walk(ROOT):
        # Prune, rather than traverse, CI workspaces: every matrix job owns a
        # source copy under build/, so rglob would scan those copies repeatedly.
        subdirs[:] = [name for name in subdirs if name not in SKIP_DIRS]
        for name in names:
            p = Path(directory) / name
            rel = p.relative_to(ROOT)
            if not p.is_file():
                continue
            if p.suffix in SKIP_SUFFIXES or p.name in SKIP_NAMES or p.name.endswith((".tar.gz", ".tar.xz", ".mod.c", ".cmd")):
                continue
            if rel.parts[:2] == ("port", "tests") and p.suffix == ".bin":
                continue
            if p.is_symlink() and ROOT not in p.resolve().parents:
                raise ValueError("source symlink escapes repository: " + str(rel))
            files.append((p, rel))
    with archive.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as compressed:
        # Materialize in-tree file links: absolute links to an author's source
        # directory are not portable to the cloud host or to a release tarball.
        with tarfile.open(fileobj=compressed, mode="w", dereference=True) as tar:
            for p, rel in sorted(files, key=lambda item: item[1].as_posix()):
                info = tar.gettarinfo(str(p), arcname="octool-" + ver + "/" + rel.as_posix())
                info.uid = info.gid = 0
                info.uname = info.gname = "root"
                info.mtime = epoch
                info.mode = 0o755 if p.suffix in (".sh", ".py") or rel.as_posix() == "port/runtime/octool" else 0o644
                with p.open("rb") as source:
                    tar.addfile(info, source)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + ".sha256").write_text(digest + "  " + archive.name + "\n", encoding="ascii")
    print(archive)
    return archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--require-gui", action="store_true")
    args = parser.parse_args()
    if args.require_gui:
        from build_gui import config
        config()
    create(args.output.resolve())


if __name__ == "__main__":
    main()
