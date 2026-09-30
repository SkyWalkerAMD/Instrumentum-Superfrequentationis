"""Exercise cloud-runner failure propagation without pretending to run Linux."""
import contextlib
import importlib.util
import io
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("cloud_runner", ROOT / "port/ci/run-matrix.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class CloudMatrix(unittest.TestCase):
    def setUp(self):
        self.targets = runner.load_targets()

    def execute(self, failures=None, scope="all", targets=None):
        failures = failures or {}
        selected = targets or self.targets
        executed, snapshots = [], []

        def fake(job):
            executed.append(job["name"])
            if isinstance(failures.get(job["name"]), Exception):
                raise failures[job["name"]]
            return {"status": failures.get(job["name"], "passed")}

        with contextlib.redirect_stdout(io.StringIO()):
            results = runner.execute_plan(runner.plan(selected, scope), fake,
                                          lambda rows: snapshots.append(dict(rows)))
        return results, executed, snapshots, runner.verdict(results, selected, scope)

    def test_all_31_required_jobs_and_gate(self):
        results, executed, snapshots, gate = self.execute()
        self.assertEqual(len(executed), 31)
        self.assertTrue(gate["full_matrix_passed"])
        self.assertEqual(gate["hardware_acceptance"], "not_run")
        self.assertEqual(len(snapshots), 31)
        self.assertEqual(len(snapshots[0]), 1)
        self.assertFalse(runner.verdict(snapshots[0], self.targets, "all")["full_matrix_passed"])

    def test_missing_gui_never_blocks_independent_kernel_jobs(self):
        results, executed, _, gate = self.execute({"baseline-el8": "failed"})
        self.assertEqual(len([n for n in executed if n.startswith("kernel-")]), 10)
        self.assertEqual(len(executed), 11)
        self.assertEqual(results["runtime-debian13"]["status"], "blocked")
        self.assertFalse(gate["requested_scope_passed"])
        self.assertFalse(gate["full_matrix_passed"])

    def test_one_target_failure_does_not_skip_others(self):
        results, executed, _, gate = self.execute({"kernel-debian11": RuntimeError("compiler failed"),
                                                  "desktop-el9": "failed"})
        self.assertEqual(results["kernel-debian11"]["status"], "failed")
        self.assertIn("compiler failed", results["kernel-debian11"]["reason"])
        self.assertNotIn("runtime-el9", executed)
        self.assertIn("runtime-debian13", executed)
        self.assertFalse(gate["full_matrix_passed"])

    def test_runtime_failure_is_a_release_failure(self):
        _, _, _, gate = self.execute({"runtime-el10": "failed"})
        self.assertFalse(gate["full_matrix_passed"])

    def test_partial_success_cannot_claim_complete_matrix(self):
        for scope, targets in (("kernel", self.targets), ("all", self.targets[-3:])):
            _, _, _, gate = self.execute(scope=scope, targets=targets)
            self.assertTrue(gate["requested_scope_passed"])
            self.assertFalse(gate["full_matrix_passed"])

    def test_snapshot_extraction_rejects_escape_and_links(self):
        for name, kind, link in (("../outside", tarfile.REGTYPE, ""),
                                 ("/absolute", tarfile.REGTYPE, ""),
                                 ("octool/link", tarfile.SYMTYPE, "../../outside"),
                                 ("octool/link", tarfile.LNKTYPE, "octool/regular")):
            with tempfile.TemporaryDirectory() as tmp:
                archive = Path(tmp) / "source.tar.gz"
                with tarfile.open(archive, "w:gz") as source:
                    entry = tarfile.TarInfo(name)
                    entry.type, entry.linkname = kind, link
                    source.addfile(entry)
                with self.assertRaises(ValueError):
                    runner.extract_snapshot(archive, Path(tmp) / "snapshot")

    def test_regular_snapshot_is_readable(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as source:
                entry = tarfile.TarInfo("octool-2.0.1/README.md")
                entry.size = 6
                source.addfile(entry, io.BytesIO(b"source"))
            root = runner.extract_snapshot(archive, Path(tmp) / "snapshot")
            self.assertEqual((root / "README.md").read_bytes(), b"source")


if __name__ == "__main__":
    unittest.main()
