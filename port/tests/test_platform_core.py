"""Exercise platform transactions and decoders without real hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PlatformCore(unittest.TestCase):
    def test_platform_controls_without_hardware(self):
        compiler = next((shutil.which(name) for name in ('c++', 'g++', 'clang++')
                         if shutil.which(name)), None)
        if compiler is None:
            if os.name == 'nt':
                self.skipTest('C++ toolchain absent; cloud CMake tests GCC, Clang and MSVC')
            self.fail('C++11 compiler is required')
        core = ROOT / 'gui/core'
        with tempfile.TemporaryDirectory(prefix='octool-platform-') as temp:
            binary = Path(temp) / ('platform.exe' if os.name == 'nt' else 'platform')
            command = [compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(core)]
            command += [str(core / (name + '.cpp')) for name in
                        ('hardware', 'register_update', 'intel_controls', 'amd_smu', 'amd_pstates', 'spd')]
            command += [str(ROOT / 'gui/tests/platform_core_test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), '8 platform scenario groups passed')
