"""Original algorithm counterexamples at the request/libc boundary."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('services', Path(__file__).resolve().parents[1] / 'tools/legacy-mmio-services.py')
services = importlib.util.module_from_spec(spec)
spec.loader.exec_module(services)


class MmioServicesTest(unittest.TestCase):
    def machine(self, inputs):
        return services.ServiceMachine(services.load_fixture(), inputs)

    def test_poll_exhaustion_and_unsupported_kind_return_zero(self):
        for kind in (1, 3, 2):
            m = self.machine(dict(kind=kind, pre_busy=1000))
            m.run(services.POLL)
            self.assertEqual(m.reg('rax'), 0)
            self.assertEqual(m.delays, [1000]*10)

    def test_comparison_rejects_only_when_both_low_words_change(self):
        for kind in (1, 3):
            for change_status, change_data in ((False, False), (True, False), (False, True), (True, True)):
                m = self.machine(dict(kind=kind, status_first=0x12, status_second=0x13 if change_status else 0x12,
                                      data_first=0xabcdef01, data_second=0xabcdef02 if change_data else 0xabcdef01))
                m.run(services.QUERY)
                self.assertEqual(m.reg('rax'), 0x8000000000000002 if change_status and change_data else 0)
                if not (change_status and change_data):
                    self.assertEqual(services.u32_at(m, m.query_data), 0xabcdef01)
                    self.assertEqual(services.u32_at(m, m.query_status), 0x12)

    def test_msr_high_words_do_not_enter_busy_or_consistency_decisions(self):
        m = self.machine(dict(kind=3, poll_high=0x80000000, status_first=0x12, status_second=0xffffffff00000012,
                              data_first=0xabcdef01, data_second=0xffffffffabcdef01))
        m.run(services.QUERY)
        self.assertEqual(m.polls, dict(pre=1, post=1))
        self.assertEqual(m.reg('rax'), 0)

    def test_fivr_uses_low_base_and_spread_input_can_set_upper_bits(self):
        m = self.machine(dict(nvl=True, pci_high=0x1234, old=0, input=0x80000000))
        m.run(services.SPREAD)
        self.assertEqual(m.mail_io[0]['address'], 0xfed15a08)
        self.assertEqual((m.mail_io[1]['address'], m.mail_io[1]['value']), (0x5a08, 0x80000100))

    def test_full_width_fch_range_leaves_field_unchanged_but_sends_update(self):
        m = self.machine(dict(start=0, end=31, input=0, old=0xffffffff, old_update=0))
        m.run(services.FCH)
        self.assertEqual([(x['address'], x['value']) for x in m.mail_io if x['opcode'] == 0x0d],
                         [(0x84, 0xffffffff), (0x40, 0x40000000)])

    def test_fsw_quantization_boundary_and_uint16_wrap(self):
        seen = []
        for value in (5, 6, 65536):
            m = self.machine(dict(input=value, old=0))
            m.run(services.FSW)
            seen.append(m.mail_io[-1]['value'])
        self.assertEqual(seen, [0, 0x800, 0])


if __name__ == '__main__':
    unittest.main()
