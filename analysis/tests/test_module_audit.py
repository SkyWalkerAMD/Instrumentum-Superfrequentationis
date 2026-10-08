"""Check section-relative relocation evidence against a real compiler object."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('cc'), 'needs a Linux C compiler')
class ModuleAuditTest(unittest.TestCase):
    def test_equal_offsets_in_distinct_sections_and_dwarf_layout(self):
        path = Path(__file__).resolve().parents[1] / 'tools/audit-legacy-mailbox.py'
        spec = importlib.util.spec_from_file_location('module_audit', path)
        audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(audit)
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'fixture.c'
            obj = source.with_suffix('.o')
            source.write_text('''
struct fixture { char pad; void *private_data; };
extern int outside_first(void *);
extern int outside_second(void *);
int first(struct fixture *p) { return outside_first(p->private_data); }
int second(struct fixture *p) { return outside_second(p->private_data); }
''', encoding='utf-8')
            subprocess.run(['cc', '-g', '-O0', '-ffunction-sections', '-c', str(source), '-o', str(obj)], check=True)
            evidence = audit.module_evidence(obj.read_bytes(), re.compile(r'^(first|second)$'),
                                             {'fixture': {'private_data'}})
        rows = {r['symbol']: r for r in evidence['functions']}
        self.assertEqual(set(rows), {'first', 'second'})
        self.assertEqual(rows['first']['offset'], 0)
        self.assertEqual(rows['second']['offset'], 0)
        self.assertNotEqual(rows['first']['section'], rows['second']['section'])
        for name in rows:
            relocations = [r['symbol'] for i in rows[name]['instructions'] for r in i['relocations']]
            self.assertIn('outside_' + name, relocations)
            self.assertNotIn('outside_' + ('second' if name == 'first' else 'first'), relocations)
        self.assertTrue(evidence['layout_fields'])
        self.assertEqual({r['offset'] for r in evidence['layout_fields']}, {8})


if __name__ == '__main__':
    unittest.main()
