"""Host-only reload routing and session freeze contract (runtime fixture opts in)."""
from pathlib import Path
import os, subprocess, unittest
ROOT=Path(__file__).resolve().parents[2]
class ReloadEntryTests(unittest.TestCase):
    def test_no_initial_window_helpers(self):
        text=(ROOT/'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
        for name in ['attachMainApp','detachMainApp','mainWindowDispatchActive','resolveWindow(']:
            self.assertNotIn(name,text)
    @unittest.skipUnless(os.environ.get('TC_RELOAD_CHECK_BINARY'),'own-binary integration supplied by C4 focused lane')
    def test_own_binary(self):
        for arg in ['--c4-checks','--c4-forced']:
            subprocess.run([os.environ['TC_RELOAD_CHECK_BINARY'],arg],cwd=ROOT/'core/tests/hotReloadLifecycle',check=True)
if __name__=='__main__': unittest.main()
