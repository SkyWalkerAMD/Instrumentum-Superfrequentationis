"""Compile bounded helper IPC regressions; no privileged hardware requests."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Helper(unittest.TestCase):
    def build_run(self, source, extra, expected):
        compiler = next((shutil.which(x) for x in ('c++', 'g++', 'clang++') if shutil.which(x)), None)
        if not compiler:
            if os.name == 'nt':
                self.skipTest('C++ compiler unavailable locally; cloud core CMake includes this regression')
            self.fail('C++11 compiler required')
        with tempfile.TemporaryDirectory(prefix='octool-helper-') as tmp:
            binary = Path(tmp)/('helper.exe' if os.name == 'nt' else 'helper')
            files = ['gui/core/hardware.cpp', 'gui/core/helper_protocol.cpp', source] + extra
            built = subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pthread'] +
                                   [str(ROOT/x) for x in files] + ['-o', str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(), expected)

    def test_codec_and_privileged_dispatch(self):
        self.build_run('gui/tests/helper_protocol_core_test.cpp', [], 'helper protocol: 4 scenario groups passed')

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux socket/process transport')
    def test_socket_protocol_failures_and_cancellation(self):
        self.build_run('gui/tests/helper_transport_test.cpp', ['gui/platform/linux_helper.cpp'],
                       'helper transport: 5 scenario groups passed')


if __name__ == '__main__':
    unittest.main()
