"""Keep imported file length and read-result counterexamples explicit."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('profile', Path(__file__).resolve().parents[1] / 'tools/legacy-nvl-profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class NvlProfileTest(unittest.TestCase):
    def machine(self, inputs):
        return profile.ProfileMachine(profile.load_fixture(), inputs)

    def test_exporter_writes_6386_but_importer_has_no_upper_bound(self):
        m = self.machine({})
        self.assertEqual(m.run(profile.SAVE_SLICE)['outcome'], 'export-write-boundary')
        self.assertEqual(m.save_request['count'], 6386)
        for size in (6387, 6393, 8192):
            m = self.machine(dict(tell_last=size))
            self.assertEqual(m.run(profile.LOAD)['outcome'], 'oversize-read-boundary')
            self.assertEqual(m.read_request['requested_bytes'], size)
            self.assertIsNone(m.first_msr_write)

    def test_short_read_does_not_prevent_first_original_msr_command(self):
        m = self.machine(dict(actual_read=0, initial_buffer_byte=0xa5))
        self.assertEqual(m.run(profile.LOAD)['outcome'], 'first-msr-write-boundary')
        self.assertEqual(m.first_msr_write['request_hex'], '0000000014000080')
        self.assertEqual(bytes(m.uc.mem_read(m.profile_buffer, 16)), b'\xa5'*16)
        self.assertTrue(m.read_request['unchecked_short_read'])

    def test_short_file_and_failed_reopen_return_without_msr(self):
        for inputs in (dict(tell_last=6385), dict(qt_open=False), dict(second_open=False)):
            m = self.machine(inputs)
            self.assertEqual(m.run(profile.LOAD)['outcome'], 'returned')
            self.assertIsNone(m.read_request)
            self.assertIsNone(m.first_msr_write)

    def test_failed_tell_can_become_unsigned_large_read_request(self):
        m = self.machine(dict(tell_first=0, tell_last=-1))
        self.assertEqual(m.run(profile.LOAD)['outcome'], 'oversize-read-boundary')
        self.assertEqual(m.read_request['requested_bytes'], profile.base.MASK)
        # The harness stops before this signed streamsize reaches real C++ I/O.
        self.assertIsNone(m.first_msr_write)


if __name__ == '__main__':
    unittest.main()
