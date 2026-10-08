#!/usr/bin/env python3
"""Static checks available on Windows too; not a substitute for Linux CI."""
import ast
import hashlib
import json
import re
import tarfile
import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
from make_source import create


def main():
    scripts = list((ROOT / "port").rglob("*.py"))
    for path in scripts:
        ast.parse(path.read_text(encoding="utf-8"), filename=str(path), feature_version=(3, 8))
    # Canonical ABI matches the input archive. HAL implementation/header hashes
    # pin the fixes in docs/review-2026-10-08.md and docs/bus-coordination.md;
    # these are source-integrity checks, not a substitute for wire regressions.
    expected = {
        "port/abi/octool_hwio_abi.h": "5f07bfefd2a96519756cad1567dfdf4b0c878db142f670e1b208cf39523800d8",
        "port/hal/octool_hwio.c": "09cb2b514d766408ed24332d48f94d9a3b1f287a127629f897b25a1285f7c4c0",
        "port/hal/octool_hwio.h": "952ae36e38f519176361ab2abeee0333f4cc73284ca69b54cf26f837fb872642",
    }
    # Normalize only CRLF introduced by a checkout, not any source tokens.
    for file, digest in expected.items():
        data = (ROOT / file).read_bytes().replace(b"\r\n", b"\n")
        assert hashlib.sha256(data).hexdigest() == digest, file
    for doc in (ROOT / "docs").rglob("*.md"):
        for link in re.findall(r'\]\(([^)]+)\)', doc.read_text(encoding="utf-8")):
            if "://" in link or link.startswith("#"):
                continue
            assert (doc.parent / link.split("#", 1)[0]).exists(), (str(doc), link)
    with tempfile.TemporaryDirectory(prefix="octool-source-test-") as tmp:
        archive = create(Path(tmp))
        first = hashlib.sha256(archive.read_bytes()).hexdigest()
        create(Path(tmp))
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == first
        assert re.fullmatch(r"octool-\d+\.\d+\.\d+-src\.tar\.gz", archive.name)
        with tarfile.open(archive) as tar:
            names = tar.getnames()
            assert any(x.endswith("/.github/workflows/portability.yml") for x in names)
            assert any(x.endswith("/docs/hardware-acceptance.md") for x in names)
            assert not any("/build/" in x or "/dist/" in x or "__pycache__" in x for x in names)
            assert not any(x.endswith((".key", ".ko", ".zip")) for x in names)
            for entry in tar:
                if entry.name.endswith(".sh"):
                    assert entry.mode == 0o755, entry.name
    print(json.dumps({"python_files": len(scripts), "abi_unchanged_hal_fix_pinned": True,
                      "docs_links": "pass", "source_archive": "deterministic"}, indent=2))


if __name__ == "__main__":
    main()
