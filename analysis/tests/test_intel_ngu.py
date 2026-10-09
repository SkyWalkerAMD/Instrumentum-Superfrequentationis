"""Keep original address/timeout/input defects visible, with no real device."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('ngu', Path(__file__).resolve().parents[1] / 'tools/legacy-intel-ngu.py')
ngu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ngu)


class NguTest(unittest.TestCase):
    def machine(self, inputs):
        return ngu.NguMachine(ngu.load_fixture(), inputs)

    def test_original_request_read_adds_base_but_write_does_not(self):
        seen = []
        for symbol in (ngu.RW_READ, ngu.RW_WRITE):
            m = self.machine(dict(mmio_base=0x123450000, address=0x5da4, data=0x89abcdef))
            self.assertEqual(m.run(symbol)['outcome'], 'returned')
            request = struct.unpack('<12Q', bytes.fromhex(m.mail_io[0]['request_hex']))
            seen.append(request[:4])
        self.assertEqual(seen[0][:3], (0x0c, 71, 0x123455da4))
        self.assertEqual(seen[1], (0x0d, 71, 0x5da4, 0x89abcdef))

    def test_busy_exhaustion_still_writes_then_ui_reports_applied(self):
        m = self.machine(dict(nvl=True, pre_busy=1000, post_busy=1000))
        result = m.run(ngu.SLOTS[0], instruction_limit=20000)
        self.assertEqual(result['outcome'], 'returned')
        self.assertEqual(m.mmio_reads, dict(pre=101, post=101))
        self.assertEqual([r['address'] for r in m.mail_io if r['opcode'] == 0x0d], [0x5da0, 0x5da4])
        self.assertIn('Applied!', result['qt_texts'])

    def test_uint_high_bit_reaches_signed_clamp_and_full_width_payload(self):
        for parsed, expected in ((0x7fffffff, 255), (0x80000000, 0x80000000), (0xffffffff, 0xffffffff)):
            m = self.machine(dict(nvl=True, parsed_uint=parsed))
            self.assertEqual(m.run(ngu.SLOTS[0])['outcome'], 'returned')
            writes = [r for r in m.mail_io if r['opcode'] == 0x0d]
            self.assertEqual(writes[0]['value'], expected)
            self.assertEqual(m.qt_conversions[0]['ok_pointer'], 0)

    def test_second_msr_poll_read_failure_cannot_reach_mmio_or_applied(self):
        m = self.machine(dict(nvl=True, io_mode='short-read-7', fail_stage='commit'))
        result = m.run(ngu.SLOTS[0], instruction_limit=2000, allow_instruction_bound=True)
        self.assertEqual(result['outcome'], 'instruction-limit')
        self.assertEqual(len([r for r in m.io if r['op'] == 'write']), 2)
        self.assertFalse(m.mail_io)
        self.assertNotIn('Applied!', result['qt_texts'])


if __name__ == '__main__':
    unittest.main()
