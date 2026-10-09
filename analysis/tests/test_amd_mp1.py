"""Negative transport/UI behaviors from original instructions, never hardware."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('amd_mp1', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-mp1.py')
mp1 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mp1)


class Mp1Test(unittest.TestCase):
    def machine(self, inputs):
        return mp1.Mp1Machine(mp1.load_fixture(), inputs)

    def test_find_ignores_index_and_never_scans_pci(self):
        m = self.machine(dict(vendor=0x1022, device=0x1480, query=[0x1022, 0x1480, 0xffffffff]))
        self.assertEqual(m.run(mp1.FIND)['outcome'], 'returned')
        self.assertEqual(m.reg('rax'), 0)
        self.assertEqual([x['offset'] for x in m.identity_reads], [0, 2])
        self.assertTrue(all(x['bdf'] == [0, 0, 0] and x['domain'] == 0 for x in m.identity_reads))

    def test_unmatched_device_still_writes_and_timeout_returns_output(self):
        m = self.machine(dict(vendor=0xffff, device=0xffff, statuses=[0]))
        self.assertEqual(m.run(mp1.MP1)['outcome'], 'returned')
        self.assertEqual(m.status_reads, 21)
        self.assertEqual(m.pci_reads, 22)
        self.assertEqual(m.reg('rax'), 0)
        self.assertEqual(len([x for x in m.pci if x['op'] == 'write']), 14)
        self.assertEqual([x['value'] for x in m.trace if 'port_write' in x], [1, 0])
        marker = next(i for i, x in enumerate(m.trace) if x.get('sleep_us') == 100000)
        self.assertFalse(any('sleep_us' in x for x in m.trace[marker + 1:]))

    def test_failed_command_still_displays_applied_in_both_routes(self):
        for flag in (0, 1):
            with self.subTest(flag=flag):
                m = self.machine(dict(object_flag=flag, mode='read-error'))
                result = m.run(mp1.SLOT3, instruction_limit=20000)
                self.assertEqual(result['outcome'], 'returned')
                self.assertIn('Applied!', result['qt_texts'])
                self.assertTrue(all(x['backend_result'] == 0 for x in m.pci if x['op'] == 'read'))

    def test_negative_slot_input_reaches_both_argument_dwords(self):
        m = self.machine(dict(parsed_int=-1, lengths=[1, 1], refcount=1))
        self.assertEqual(m.run(mp1.SLOT19)['outcome'], 'returned')
        writes = {x['attempted_index']: x['value'] for x in m.pci if x['op'] == 'write' and x['offset'] == 0xfc}
        self.assertEqual(writes[m.config['SMU_ARG0']], 0xffffffff)
        self.assertEqual(writes[m.config['SMU_ARG1']], 0xffffffff)
        self.assertEqual(len(m.deleted), 2)
        self.assertEqual(m.conversions, [dict(base=10, ok_pointer=0, synthetic_result=-1)])

    def test_copied_channel_flag_uses_gpt_not_shimada(self):
        for flags, expected in ((dict(gpt=1, shimada=0), 1), (dict(gpt=0, shimada=1), 0)):
            m = self.machine(dict(vendor=0x1022, device=0x1480, **flags))
            for phase in (mp1.FLAG, mp1.COPY):
                m.outcome, m.steps = None, 0
                self.assertEqual(m.run(phase)['outcome'], 'region-end')
            self.assertEqual(bytes(m.uc.mem_read(m.this + 0xf88, 1)), bytes([expected]))


if __name__ == '__main__':
    unittest.main()
