"""Regression tests for release gates. No Linux hardware required."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
from check_elf import elf_info, violations
from build_packages import module_stage, version
from build_gui import source_path


def minimal_elf(path, requirement="GLIBC_2.28", library="libc.so.6", stack_flags=6):
    """A real ELF64 layout; requirements are in .gnu.version_r, not strings."""
    strings = b"\0" + library.encode() + b"\0" + requirement.encode() + b"\0"
    index = len(library) + 2
    dynamic = struct.pack("<qQqQ", 1, 1, 0, 0)
    verneed = struct.pack("<HHIII", 1, 1, 1, 16, 0) + struct.pack("<IHHII", 0, 0, 2, index, 0)
    content = [b"", strings, dynamic, verneed]
    program = (struct.pack("<IIQQQQQQ", 0x6474e551, stack_flags, 0, 0, 0, 0, 0, 16)
               if stack_flags is not None else b"")
    offset, blocks, sections = 64 + len(program), bytearray(), []
    for kind, link, entry, block in zip([0, 3, 6, 0x6ffffffe], [0, 0, 1, 1], [0, 0, 16, 0], content):
        sections.append(struct.pack("<IIQQQQIIQQ", 0, kind, 0, 0, offset, len(block), link, 0, 1, entry))
        blocks.extend(block)
        offset += len(block)
    ident = b"\x7fELF\x02\x01\x01" + b"\0" * 9
    header = struct.pack("<16sHHIQQQIHHHHHH", ident, 3, 62, 1, 0, 64, offset, 0, 64, 56,
                         int(bool(program)), 64, 4, 0)
    path.write_bytes(header + program + blocks + b"".join(sections))


class ReleaseGates(unittest.TestCase):
    def test_real_elf_requirement_passes_at_el8_floor(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "gui"
            minimal_elf(binary)
            self.assertEqual(violations(elf_info(binary)), [])

    def test_newer_glibc_binary_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "gui"
            minimal_elf(binary, requirement="GLIBC_2.35")
            self.assertIn("above EL8 floor: GLIBC_2.35", violations(elf_info(binary)))

    def test_each_soname_hazard_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            for library in ["libicui18n.so.70", "libjpeg.so.8", "libQt5Widgets.so.5", "libopencv_core.so.414",
                            "libtiff.so.5", "libtiffxx.so.5", "libwebp.so.7", "libwebpdemux.so.2", "libwebpmux.so.3"]:
                binary = Path(tmp) / "gui"
                minimal_elf(binary, library=library)
                self.assertTrue(violations(elf_info(binary)), library)

    def test_cpp_floor_and_private_versions(self):
        for requirement in ["GLIBCXX_3.4.26", "CXXABI_1.3.12", "GLIBC_PRIVATE"]:
            info = {"needed": ["libc.so.6"], "versions": [requirement], "rpaths": [], "gnu_stack_flags": [6]}
            self.assertTrue(violations(info), requirement)

    def test_numeric_version_comparison_and_build_rpath(self):
        info = {"needed": ["libc.so.6"], "versions": ["GLIBC_2.9"], "rpaths": [], "gnu_stack_flags": [6]}
        self.assertFalse(violations(info))
        info["rpaths"] = ["/home/author/qt/lib"]
        self.assertTrue(violations(info))

    def test_executable_or_undeclared_stack_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "gui"
            minimal_elf(binary, stack_flags=7)
            info = elf_info(binary)
            self.assertEqual(info['gnu_stack_flags'], [7])
            self.assertIn('executable GNU_STACK is not allowed in the portable GUI', violations(info))
            minimal_elf(binary, stack_flags=None)
            self.assertIn('expected exactly one PT_GNU_STACK declaration', violations(elf_info(binary)))
            minimal_elf(binary, stack_flags=6)
            self.assertEqual(violations(elf_info(binary)), [])

    def test_non_elf_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "gui"
            binary.write_bytes(b"IntxLNK\0")
            with self.assertRaises(ValueError):
                elf_info(binary)

    def test_dkms_payload_retains_canonical_abi(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage, ver = Path(tmp), version()
            module_stage(stage, ver)
            src = stage / ("usr/src/octool-hwio-" + ver)
            self.assertEqual((src / "abi/octool_hwio_abi.h").read_bytes(),
                             (ROOT / "port/abi/octool_hwio_abi.h").read_bytes())
            self.assertIn('../abi/octool_hwio_abi.h', (src / "kmod/octool_hwio_abi.h").read_text())
            self.assertEqual((src / "kmod/octool_bus_access.h").read_bytes(),
                             (ROOT / "port/kmod/octool_bus_access.h").read_bytes())
            self.assertIn('PACKAGE_VERSION="' + ver + '"', (src / "dkms.conf").read_text())
            self.assertIn('/build/kmod', (src / "dkms.conf").read_text())

    def test_gui_manifest_cannot_read_outside_source(self):
        for value in ("../outside.pro", str(ROOT / "docs/README.md"), "gui/../docs/README.md"):
            with self.assertRaises(ValueError):
                source_path(value)

    def test_matrix_covers_all_required_targets(self):
        targets = json.loads((ROOT / "port/ci/targets.json").read_text())
        self.assertEqual({t["id"] for t in targets}, {
            "el8", "el9", "el10", "ubuntu20.04", "ubuntu22.04", "ubuntu24.04",
            "ubuntu26.04", "debian11", "debian12", "debian13"})
        self.assertEqual(len(targets), 10)
        self.assertEqual(next(t["display"] for t in targets if t["id"] == "el10"), "xwayland")


if __name__ == "__main__":
    unittest.main()
