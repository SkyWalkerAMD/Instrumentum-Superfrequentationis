"""A display startup delay must not mask or retry a failed GUI."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('gui_smoke',Path(__file__).resolve().parents[1]/'ci/gui-smoke.py')
smoke=importlib.util.module_from_spec(spec);spec.loader.exec_module(smoke)


class GuiSmokePrerequisites(unittest.TestCase):
    def test_wait_requires_successful_display_connection(self):
        unavailable=subprocess.CompletedProcess([],1,'','window-probe: cannot open DISPLAY\n')
        ready=subprocess.CompletedProcess([],0,'','')
        with patch.object(smoke.subprocess,'run',side_effect=[unavailable,ready]) as run, patch.object(smoke.time,'sleep'):
            self.assertEqual(smoke.wait_for_display(Path('/probe')),2)
            self.assertEqual(run.call_count,2)

    def invoke(self, directory):
        config=directory/'config.json';config.write_text(json.dumps({'window_title_regex':'OCTool'}))
        argv=['gui-smoke.py','--config',str(config),'--log',str(directory/'gui.log')]
        with patch.object(sys,'argv',argv),patch.object(smoke.os,'geteuid',return_value=1000,create=True):
            return smoke.main()

    def test_unavailable_display_never_starts_gui(self):
        unavailable=subprocess.CompletedProcess([],1,'','window-probe: cannot open DISPLAY\n')
        with tempfile.TemporaryDirectory() as tmp, patch.object(smoke.subprocess,'run',return_value=unavailable), \
                patch.object(smoke.subprocess,'Popen') as launch, \
                patch.object(smoke.time,'monotonic',side_effect=[0,0,11,11]),patch.object(smoke.time,'sleep'):
            with self.assertRaisesRegex(RuntimeError,'DISPLAY did not become connectable'):
                self.invoke(Path(tmp))
            launch.assert_not_called()
            self.assertIn('PRECONDITION FAILED',(Path(tmp)/'gui.log').read_text())

    def test_real_gui_abort_is_not_retried(self):
        process=Mock();process.poll.return_value=-6;process.returncode=-6
        with tempfile.TemporaryDirectory() as tmp, patch.object(smoke,'wait_for_display',return_value=1), \
                patch.object(smoke.subprocess,'Popen',return_value=process) as launch:
            with self.assertRaisesRegex(RuntimeError,'rc=-6'):
                self.invoke(Path(tmp))
            launch.assert_called_once()


if __name__=='__main__':unittest.main()
