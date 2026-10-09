"""Compile the reconstructed PStates core without Qt or the Linux HAL."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PstateCore(unittest.TestCase):
    def test_standalone_core_with_synthetic_reader(self):
        compiler = next((shutil.which(name) for name in ("c++", "g++", "clang++")
                         if shutil.which(name)), None)
        if compiler is None:
            if os.name == "nt":
                self.skipTest("C++ compiler unavailable on this Windows host")
            self.fail("A C++11 compiler is required to validate the portable core")
        with tempfile.TemporaryDirectory(prefix="octool-core-") as tmp:
            binary = Path(tmp) / ("pstates-core.exe" if os.name == "nt" else "pstates-core")
            command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                       str(ROOT / "gui/core/amd_pstates.cpp"),
                       str(ROOT / "gui/tests/pstates_core_test.cpp"), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            tested = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            self.assertEqual(tested.stdout.strip(), "amd_pstates core: 7 scenario groups passed")


if __name__ == "__main__":
    unittest.main()
