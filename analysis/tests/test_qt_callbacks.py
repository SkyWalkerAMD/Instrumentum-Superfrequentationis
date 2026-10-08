"""Original Qt routers: jump tables, virtual closeEvent and boundary refusal."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/qt-callback-map.py'
spec = importlib.util.spec_from_file_location('qt_callbacks',TOOL)
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
FIXTURE = TOOL.parents[1]/'fixtures/legacy-qt-callbacks.json'


class CallbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.classes={r['class']:r for r in qt.load_fixture(FIXTURE)['classes']}

    def test_jump_table_and_virtual_event_match_metadata(self):
        for cls,method in [('MainWindow','on_actionControls_triggered'),
                           ('cpufunctions','refresh_pstates'),('intel_ctl5','closeEvent')]:
            row=self.classes[cls]
            index=next(m['index'] for m in row['methods'] if m['method']==method)
            observed=qt.route(row,index)
            self.assertEqual(observed['outcome'],'callback-boundary')
            self.assertTrue(any(method in symbol for symbol in observed['symbols']))

    def test_invalid_indices_return_without_callback(self):
        row=self.classes['MainWindow']
        for index in (-1,row['method_count'],0x7fffffff):
            self.assertEqual(qt.route(row,index)['outcome'],'returned-without-external-body')

    def test_changed_fixture_or_router_data_rejected(self):
        with tempfile.TemporaryDirectory(prefix='octool-qt-') as directory:
            path=Path(directory)/'fixture.json'
            path.write_bytes(FIXTURE.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                qt.load_fixture(path)
        row=copy.deepcopy(self.classes['MainWindow'])
        row['data'][0]['bytes_hex']='00'
        with self.assertRaisesRegex(ValueError,'data hash mismatch'):
            qt.route(row,0)

    def test_absent_callback_boundary_is_not_executed(self):
        row=copy.deepcopy(self.classes['MainWindow'])
        row['boundaries']=[]
        with self.assertRaises(ValueError):
            qt.route(row,0)


if __name__=='__main__':
    unittest.main()
