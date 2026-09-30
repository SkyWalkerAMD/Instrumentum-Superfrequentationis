"""Bad trace input must never pass acceptance. Requires the real compiled tool."""
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

BINARY = Path(__file__).resolve().parent / "octool_parity"


@unittest.skipUnless(os.name == "posix" and BINARY.is_file(), "requires Linux-built octool_parity")
class BadTrace(unittest.TestCase):
    def test_empty_or_truncated_trace_fails_before_device_open(self):
        header = struct.pack("<QQQQ", 0x4f43545250520001, 96, 5, 0)
        for payload, message in [(header, "INCONCLUSIVE"), (header + b"x", "truncated")]:
            with tempfile.NamedTemporaryFile() as corpus:
                corpus.write(payload)
                corpus.flush()
                run = subprocess.run([str(BINARY), "--old", "/nonexistent-old", "--new",
                                      "/nonexistent-new", "--trace", corpus.name],
                                     capture_output=True, text=True)
                self.assertEqual(run.returncode, 2)
                self.assertIn(message, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
