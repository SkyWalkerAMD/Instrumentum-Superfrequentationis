"""Keep original limit labels and the second platform flag tied to code bytes."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('amd_bindings_test', Path(__file__).resolve().parents[1] / 'tools/legacy-amd-limit-bindings.py')
bindings = importlib.util.module_from_spec(spec); spec.loader.exec_module(bindings)


class AmdLimitBindings(unittest.TestCase):
    def test_input_and_label_share_the_original_layout_row(self):
        machine = bindings.BindingMachine(bindings.load_fixture(), {})
        for phase in (bindings.SETUP, bindings.TEXT):
            machine.outcome, machine.steps = None, 0
            self.assertEqual(machine.run(phase, instruction_limit=8000)['outcome'], 'region-end')
        for offset, label in zip(bindings.OFFSETS, bindings.LABELS):
            widget = machine.u64(machine.ui + offset)
            row = next(row for row in machine.rows.values() if widget in row)
            self.assertEqual(len(row), 2)
            self.assertEqual(machine.labels[row[0]], label)
            self.assertEqual(row[1], widget)

    def test_shimada_identity_alone_does_not_set_second_flag(self):
        fixture = bindings.load_fixture()
        for device, granite, expected in ((0x153a, 0, 0), (0x14b5, 0, 0), (0x14d8, 0, 1), (0x14a4, 0, 1), (0x153a, 255, 1)):
            machine = bindings.BindingMachine(fixture, dict(vendor=0x1022, device=device, granite=granite))
            self.assertEqual(machine.run(bindings.FLAG)['outcome'], 'region-end')
            self.assertEqual(bytes(machine.uc.mem_read(machine.this + 0xf89, 1))[0], expected)
