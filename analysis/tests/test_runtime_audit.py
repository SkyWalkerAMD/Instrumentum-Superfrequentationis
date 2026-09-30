"""Real ELF fixtures: symbol versions, PLT/CET and -fno-plt GOT references."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/elf-runtime-audit.py'
spec = importlib.util.spec_from_file_location('runtime_audit', TOOL)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


@unittest.skipUnless(shutil.which('gcc'), 'requires Linux x86-64 gcc/binutils')
class AuditTest(unittest.TestCase):
    def test_plt_cet_and_got_versions(self):
        with tempfile.TemporaryDirectory(prefix='octool-elf-audit-') as directory:
            root = Path(directory)
            (root/'provider.c').write_text('int fixture_data=7; int fixture_func(int x){return x+1;}\n')
            (root/'versions.map').write_text('AUDIT_1 { global: fixture_func; fixture_data; local: *; };\n')
            (root/'main.c').write_text('extern int fixture_data; extern int fixture_func(int);\n'
                                       'int main(void){return fixture_func(fixture_data);}\n')
            subprocess.check_call(['gcc', '-shared', '-fPIC', 'provider.c', '-o', 'libfixture.so',
                                   '-Wl,--version-script=versions.map,-soname,libfixture.so'], cwd=root)
            for options in ([], ['-fcf-protection=full', '-Wl,-z,ibtplt'], ['-fno-plt']):
                subprocess.check_call(['gcc', '-fPIC', '-pie', 'main.c', '-L.', '-lfixture',
                                       '-o', 'fixture'] + options, cwd=root)
                result = audit.inspect(root/'fixture', re.compile('^main$'))
                for symbol in ('fixture_func', 'fixture_data'):
                    imported = next(s for s in result['imports'] if s['symbol'] == symbol)
                    self.assertEqual(imported['provider'], 'libfixture.so')
                    self.assertEqual(imported['version'], 'AUDIT_1')
                    self.assertTrue(any(s['function'] == 'main' for s in imported['sites']), options)
                self.assertFalse(result['executed'])
                self.assertFalse(result['incomplete_decodes'])


if __name__ == '__main__':
    unittest.main()
