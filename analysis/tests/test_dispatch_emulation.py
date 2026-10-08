"""Fail-closed boundaries and counterexamples to misleading function names."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/emulate-legacy-dispatch.py'
spec = importlib.util.spec_from_file_location('dispatch_emulation', TOOL)
emu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(emu)


class DispatchTest(unittest.TestCase):
    def test_tampered_fixture_rejected_before_emulation(self):
        with tempfile.TemporaryDirectory(prefix='octool-dispatch-') as directory:
            path = Path(directory)/'fixture.json'
            path.write_bytes(emu.FIXTURE.read_bytes() + b' ')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                emu.load_fixture(path)

    def test_unknown_call_and_execution_escape_rejected(self):
        machine = emu.Machine(emu.load_fixture(), {})
        with self.assertRaisesRegex(ValueError, 'unexpected external'):
            machine.external('unexpected_device_access')
        with self.assertRaisesRegex(ValueError, 'escaped'):
            machine.hook(machine.uc, emu.HEAP, 1, None)

    def test_pci_entry_count_is_not_cpu_model(self):
        fixture = emu.load_fixture()
        for count in (0, 1, 2):
            rows = [[0, 0, 0, 0x8086, 0x3258]] * count
            spr = emu.Machine(fixture, {'pci_rows': rows}).run('_Z8isit_sprv')
            gnr = emu.Machine(fixture, {'pci_rows': rows}).run('_Z11isit_gnr_spv')
            self.assertEqual(spr['return_al'], int(count == 1))
            self.assertEqual(gnr['return_al'], int(count > 1))

    def test_controls_stops_before_constructor(self):
        machine = emu.Machine(emu.load_fixture(), {'vendor':0x8086, 'gnr':True})
        result = machine.run(emu.CONTROLS)
        self.assertEqual(result['outcome'], 'panel-constructor-boundary')
        self.assertIn('intel_ctl5C', result['panel'])
        self.assertIsNone(result['return_al'])
        self.assertNotIn('_Z8checkvrmv', result['calls'])


if __name__ == '__main__':
    unittest.main()
