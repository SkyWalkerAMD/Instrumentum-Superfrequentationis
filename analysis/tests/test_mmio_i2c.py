"""Original I2C error returns, per-call budgets and unchecked table index."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('i2c', Path(__file__).resolve().parents[1] / 'tools/legacy-mmio-i2c.py')
i2c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(i2c)


class MmioI2cTest(unittest.TestCase):
    def machine(self, inputs):
        return i2c.I2cMachine(i2c.load_fixture(), inputs)

    def test_disable_timeout_returns_one_without_starting_transfer(self):
        m = self.machine(dict(disable_busy=201))
        m.run(i2c.ENTRY, instruction_limit=50000)
        self.assertEqual(m.reg('rax'), 1)
        self.assertEqual(m.counts, dict(disable=201))
        self.assertTrue(any('I2cDisable' in s for s in m.diagnostics))
        self.assertEqual([(r['address'], r['value']) for r in m.mail_io if r['opcode'] == 0x0d], [(0x6c, 0)])

    def test_receive_budget_is_shared_and_partial_output_remains_on_failure(self):
        m = self.machine(dict(rx_count=3, rx_busy_per_byte=[100, 100, 1]))
        m.run(i2c.ENTRY, instruction_limit=50000)
        self.assertEqual(m.reg('rax'), 0)
        self.assertEqual(bytes(m.uc.mem_read(m.rx_buffer, 4)), b'\xa0\xa1\xcc\xcc')
        self.assertEqual(m.rx_poll_counts, {0: 101, 1: 101, 2: 2})

    def test_outer_blocked_loop_does_not_consume_a_timeout_counter(self):
        m = self.machine(dict(outer_level_blocked=1000000))
        result = m.run(i2c.ENTRY, instruction_limit=5000, allow_instruction_bound=True)
        self.assertEqual(result['outcome'], 'instruction-limit')
        self.assertNotIn('abort', m.counts)
        self.assertFalse(m.diagnostics)

    def test_six_entry_index_is_stopped_by_harness_before_original_oob_read(self):
        m = self.machine(dict(bus=6))
        result = m.run(i2c.ENTRY)
        self.assertEqual(result['outcome'], 'out-of-table-index')
        self.assertEqual(m.table_access['row'], 6)
        self.assertFalse(m.mail_io)
        self.assertFalse(m.pci)

    def test_overflowed_count_sum_skips_transfer_but_returns_one(self):
        m = self.machine(dict(rx_count=i2c.base.MASK, tx_count=1))
        m.run(i2c.ENTRY)
        self.assertEqual(m.reg('rax'), 1)
        self.assertFalse(any(r['opcode'] == 0x0d and r['address'] == 0x10 for r in m.mail_io))
        self.assertEqual(bytes(m.uc.mem_read(m.rx_buffer, 4)), b'\xcc'*4)


if __name__ == '__main__':
    unittest.main()
