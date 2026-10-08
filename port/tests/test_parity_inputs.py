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
    def test_same_character_device_and_symlink_are_rejected(self):
        header = struct.pack("<QQQQ", 0x4f43545250520001, 96, 5, 0)
        # One completed MMIO read. No request is sent: identity fails first.
        request = struct.pack("<12Q", 0x0c, 71, 0x1000, *([0] * 9))
        record = request + struct.pack("<5QQii", 1, 0, 0, 0, 0, 0, 96, 1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            corpus = path / "corpus.bin"
            corpus.write_bytes(header + record)
            alias = path / "alias"
            alias.symlink_to("/dev/null")
            for new_device in ("/dev/null", str(alias)):
                run = subprocess.run([str(BINARY), "--old", "/dev/null", "--new", new_device,
                                      "--trace", str(corpus)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 2)
                self.assertIn("same device", run.stdout + run.stderr)

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

    def test_finalized_counts_and_failed_duplicates(self):
        request = struct.pack("<12Q", 0x0c, 71, 0x1000, *([0] * 9))
        good = request + struct.pack("<5QQii", 1, 2, 0, 0, 0, 0, 96, 1)
        bad = request + struct.pack("<5QQii", 0xfffffff400000001, 2, 0, 0, 0, 1, 96, 1)
        cases = [
            (0, 0, good, "capture incomplete"),
            (0x4f43545250520002, 0, good, "count mismatch"),
            (0x4f43545250520002, 2, good, "count mismatch"),
            (0x4f43545250520002, 1, good + good, "count mismatch"),
            (0x4f43545250520002, 2, good + bad, "incomplete request"),
            (0x4f43545250520001, 0, good + bad, "incomplete request"),
        ]
        for magic, nrec, data, message in cases:
            with self.subTest(magic=magic, nrec=nrec, message=message):
                with tempfile.NamedTemporaryFile() as corpus:
                    corpus.write(struct.pack("<4Q", magic, 96, 5, nrec) + data)
                    corpus.flush()
                    run = subprocess.run([str(BINARY), "--check-trace", "--trace", corpus.name],
                                         capture_output=True, text=True)
                    self.assertEqual(run.returncode, 2)
                    self.assertIn(message, run.stderr)

    def test_empty_capture_cannot_reuse_previous_valid_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            request = struct.pack("<12Q", 0x0c, 71, 0x1000, *([0] * 9))
            old_data = (struct.pack("<4Q", 0x4f43545250520002, 96, 5, 1) + request +
                        struct.pack("<5QQii", 1, 0, 0, 0, 0, 0, 96, 1))
            corpus = path / "corpus.bin"
            corpus.write_bytes(old_data)
            device = path / "device-file"
            device.touch()
            # Exercise the real shell script up to its preflight, without root,
            # driver loading or a GUI. A module attempt would leave a marker.
            for command, body in {
                "id": "echo 0\n", "make": "exit 0\n",
                "insmod": 'touch "$MODULE_ATTEMPT"\nexit 99\n',
                "rmmod": 'touch "$MODULE_ATTEMPT"\nexit 99\n',
            }.items():
                stub = path / command
                stub.write_text("#!/bin/sh\n" + body, encoding="ascii")
                stub.chmod(0o755)
            marker = path / "module-attempt"
            env = os.environ.copy()
            env.update(PATH=str(path) + os.pathsep + env["PATH"], CORPUS=str(corpus),
                       OLD_DEV=str(device), NEW_DEV="octool-test-" + path.name,
                       OCTOOL="/bin/true", OCTOOL_ARGS="", MODULE_ATTEMPT=str(marker))
            run = subprocess.run(["sh", str(BINARY.parent / "parity-run.sh")], env=env,
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 2, run.stdout + run.stderr)
            self.assertIn("capture incomplete", run.stderr)
            self.assertFalse(marker.exists())
            self.assertEqual(corpus.read_bytes(), old_data)
            self.assertEqual(list(path.glob("*.capture.*")), [])


if __name__ == "__main__":
    unittest.main()
