"""Bindings are qualified by widget identity and original Qt method routing."""
import copy
import importlib.util
from pathlib import Path
import unittest

TOOL=Path(__file__).resolve().parents[1]/'tools/emulate-legacy-ui-connections.py'
spec=importlib.util.spec_from_file_location('ui_connections',TOOL)
ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)


class UiConnectionTest(unittest.TestCase):
    def test_one_sender_is_registered_to_both_xoc_slots(self):
        machine=ui.UiMachine(ui.load_fixture(),{})
        result=machine.run(ui.CONNECT)
        self.assertEqual(result['outcome'],'slice-end')
        xoc=[r for r in machine.operations if r['sender'].get('offset')==0x460]
        self.assertEqual([r['slot'] for r in xoc],['1on_pushButton_14_clicked()','1on_xoc_clicked()'])
        self.assertEqual(xoc[0]['sender'],xoc[1]['sender'])
        self.assertEqual([r['signal'] for r in xoc],['2clicked()']*2)

    def test_missing_optional_widget_skips_only_cu1_connection(self):
        sets=[]
        for null in (False,True):
            machine=ui.UiMachine(ui.load_fixture(),{'optional_null':null})
            machine.run(ui.CONNECT)
            sets.append([r['slot'] for r in machine.operations])
        self.assertEqual([r for r in sets[0] if r not in sets[1]],['1synch_cu1_v()'])
        self.assertIn('1gt_clicked()',sets[1]);self.assertIn('1npu_clicked()',sets[1])

    def test_unknown_translation_boundary_cannot_execute(self):
        fixture=copy.deepcopy(ui.load_fixture())
        for f in fixture['functions']:
            f['calls']=[c for c in f['calls'] if c['target']!=0x150f900]
        machine=ui.UiMachine(fixture,{})
        with self.assertRaises((ValueError,ui.unicorn.UcError)):
            machine.run(ui.TRANSLATE)


if __name__=='__main__':unittest.main()
