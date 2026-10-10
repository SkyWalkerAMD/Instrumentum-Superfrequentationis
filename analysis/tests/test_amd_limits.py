"""Pin significant behaviors of the original AMD limits slot, not host hardware."""
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('legacy_limits_test', ROOT / 'analysis/tools/legacy-amd-limits.py')
limits = importlib.util.module_from_spec(spec)
spec.loader.exec_module(limits)


class AmdLimits(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = limits.FIXTURE.read_bytes()
        if hashlib.sha256(raw).hexdigest() != limits.FIXTURE_SHA:
            raise AssertionError('original fixture identity mismatch')
        cls.fixture = json.loads(raw)

    def run_slot(self, **changes):
        inputs = dict(f88=0, f89=0, gpt=0, fields=[None] * 6)
        inputs.update(changes)
        machine = limits.LimitsMachine(self.fixture, inputs)
        result = machine.run(limits.SLOT, instruction_limit=20000)
        self.assertEqual(result['outcome'], 'returned')
        self.assertFalse(machine.pci)
        return machine, result

    def test_scaled_input_wraps_in_original_code(self):
        machine, result = self.run_slot(fields=[4294968, None, None, None, None, None])
        self.assertEqual(machine.command_calls, [dict(transport='MP1_C2PMSG', command=0x53, argument=704)])
        self.assertIn('Applied!', result['qt_texts'])

    def test_mobile_current_fields_share_one_message(self):
        machine, result = self.run_slot(f88=1, fields=[None, 10, 20, None, 5400, None])
        self.assertEqual(machine.command_calls, [
            dict(transport='MP1_C2PMSG', command=0x39, argument=10000),
            dict(transport='MP1_C2PMSG', command=0x3a, argument=10000),
            dict(transport='MP1_C2PMSG', command=0x3a, argument=20000),
            dict(transport='MP1_C2PMSG', command=0x3b, argument=20000)])
        self.assertIn('Applied!', result['qt_texts'])

    def test_failed_returns_do_not_prevent_original_success_message(self):
        machine, result = self.run_slot(fields=[1, None, None, None, None, 2], reply=0xfe)
        self.assertEqual(machine.command_calls, [
            dict(transport='MP1_C2PMSG', command=0x53, argument=1000),
            dict(transport='smu_cmd2', command=0x2f, argument=2)])
        self.assertIn('Applied!', result['qt_texts'])


if __name__ == '__main__':
    unittest.main()
