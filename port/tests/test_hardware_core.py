"""Compile and exercise portable access without Qt, a HAL or hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HardwareCore(unittest.TestCase):
    def test_validation_errors_lifetime_and_concurrency(self):
        compiler = next((shutil.which(name) for name in ("c++", "g++", "clang++")
                         if shutil.which(name)), None)
        if compiler is None:
            if os.name == "nt":
                self.skipTest("C++ compiler unavailable on this Windows host; cloud CMake also tests MSVC")
            self.fail("A C++11 compiler is required to validate the portable core")
        with tempfile.TemporaryDirectory(prefix="octool-hardware-") as tmp:
            binary = Path(tmp) / ("hardware-core.exe" if os.name == "nt" else "hardware-core")
            built = subprocess.run(
                [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
                 str(ROOT / "gui/core/hardware.cpp"),
                 str(ROOT / "gui/tests/hardware_core_test.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            tested = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(tested.returncode, 0, tested.stdout + tested.stderr)
            self.assertEqual(tested.stdout.strip(), "hardware core: 7 scenario groups passed")


if __name__ == "__main__":
    unittest.main()
