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
    # These values are from the supplied refactor archive. Only tests/build and
    # packaging changed; the HAL and MMIO request layout are frozen for this work.
    expected = {
        "port/abi/octool_hwio_abi.h": "5f07bfefd2a96519756cad1567dfdf4b0c878db142f670e1b208cf39523800d8",
        "port/hal/octool_hwio.c": "ad7940bedb280ec1789831d360611ab473febb13cff1458c10aff1b1d3e45d29",
        "port/hal/octool_hwio.h": "d80a19e0e0551ba162e55653bde7975c50b74a1e811a08f6776071af43762dc1",
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
    print(json.dumps({"python_files": len(scripts), "abi_hal_unchanged": True,
                      "docs_links": "pass", "source_archive": "deterministic"}, indent=2))


if __name__ == "__main__":
    main()
