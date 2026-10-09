"""Preserve original caller defects without touching real physical memory."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('clients', Path(__file__).resolve().parents[1] / 'tools/legacy-mmio-clients.py')
clients = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clients)


class MmioClientsTest(unittest.TestCase):
    def machine(self, inputs):
        return clients.ClientMachine(clients.load_fixture(), inputs)

    def test_raw_slot_stores_user_address_but_requests_zero_in_both_widths(self):
        for data, opcode in ((0x89abcdef, 0x0d), (0x1234567887654321, 0x0b)):
            m = self.machine(dict(address=0xfed10000, data=data))
            result = m.run(clients.RAW)
            self.assertEqual(m.u64(m.raw_this + 0x48), 0xfed10000)
            self.assertEqual(struct.unpack('<12Q', bytes.fromhex(m.mail_io[0]['request_hex']))[:4],
                             (opcode, 71, 0, data))
            self.assertIn('Applied!', result['qt_texts'])

    def test_parse_failure_flag_is_ignored_and_timer_restarts(self):
        m = self.machine(dict(address=0, data=0, parse_ok=False))
        result = m.run(clients.RAW)
        self.assertTrue(all(not p['synthetic_ok'] for p in m.parse_calls))
        self.assertEqual(len(m.mail_io), 1)
        self.assertIn('Applied!', result['qt_texts'])
        self.assertEqual(m.qt_events[-1], dict(operation='timer-start', interval=2000))

    def test_nvl_read_still_writes_command_and_returns_busy_data(self):
        m = self.machine(dict(pre_busy=1000, post_busy=1000, command=0xffffffff, reply=0x89abcdef))
        m.run(clients.NVL_READ, instruction_limit=20000)
        self.assertEqual(m.mmio_reads, dict(pre=101, post=101))
        self.assertEqual([(r['address'], r['value']) for r in m.mail_io if r['opcode'] == 0x0d],
                         [(0x5da4, 0x9fffffff)])
        self.assertEqual(m.reg('rax'), 0x80000000)
        self.assertEqual(struct.unpack('<I', m.uc.mem_read(m.query_out, 4))[0], 0x89abcdef)

    def test_64bit_devmem_truncates_data_but_module_request_preserves_it(self):
        address, data = 0x1234567887654fff, 0x1234567887654321
        for loaded in (0, 1):
            m = self.machine(dict(module_loaded=loaded, address=address, data=data))
            m.run(clients.WRITE64)
            if loaded:
                self.assertEqual((m.mail_io[0]['address'], m.mail_io[0]['value']), (address, data))
                self.assertFalse(m.mapping_events)
            else:
                self.assertEqual([x for x in m.mapping_events if x['operation'] == 'mapped-store'],
                                 [dict(operation='mapped-store', offset=4095, width=8, value=0x87654321)])
                self.assertFalse(m.mail_io)


if __name__ == '__main__':
    unittest.main()
