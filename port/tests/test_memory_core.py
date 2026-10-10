"""Exercise UMC recovery and transport gates without real hardware."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MemoryCore(unittest.TestCase):
    def test_original_fields_and_guarded_transport(self):
        compiler = next((shutil.which(name) for name in ('c++', 'g++', 'clang++') if shutil.which(name)), None)
        if compiler is None:
            if os.name == 'nt':
                self.skipTest('C++ toolchain absent; cloud CMake tests GCC, Clang and MSVC')
            self.fail('C++11 compiler is required')
        core = ROOT / 'gui/core'
        with tempfile.TemporaryDirectory(prefix='octool-memory-') as temp:
            binary = Path(temp) / ('memory.exe' if os.name == 'nt' else 'memory')
            command = [compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pthread', '-I', str(core)]
            command += [str(core / (name + '.cpp')) for name in ('hardware', 'register_update', 'amd_umc')]
            command += [str(ROOT / 'gui/tests/memory_core_test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), '7 memory scenario groups passed')
