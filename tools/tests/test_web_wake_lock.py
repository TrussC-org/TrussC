"""Exercise the production EM_JS state machine without a browser or network."""
import pathlib
import json
import shutil
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class WebWakeLockTest(unittest.TestCase):
    def test_notice_is_outside_synchronous_proxy(self):
        source = (ROOT / "core/platform/web/tcPlatform_web.cpp").read_text(encoding="utf-8")
        js_start = source.index("EM_JS(int, applyWebWakeLock,")
        js_end = source.index("\n});", js_start)
        js = source[js_start:js_end]
        self.assertNotIn("getWasmTableEntry", js)
        self.assertNotIn("logNotice", js)
        self.assertNotIn("wakeLockUnavailable", js)
        proxy_start = source.index("int webWakeLockOnMain(")
        proxy = source[proxy_start:source.index("\n}", proxy_start)]
        self.assertIn("return applyWebWakeLock(enabled != 0);", proxy)
        self.assertNotIn("wakeLockUnavailable", proxy)
        self.assertNotIn("logNotice", proxy)
        caller_start = source.index("void applyKeepScreenOn(", proxy_start)
        caller = source[caller_start:source.index("\n}", caller_start)]
        self.assertIn("const int notice = emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_II, webWakeLockOnMain, int(enabled));", caller)
        self.assertIn("#endif\n    if (notice) wakeLockUnavailable();", caller)
        notice_start = source.index("void wakeLockUnavailable() noexcept")
        notice = source[notice_start:source.index("\n}", notice_start)]
        self.assertIn("try { logNotice", notice)
        self.assertIn("catch (...) {}", notice)

    @unittest.skipUnless(shutil.which("node"), "node is unavailable")
    def test_state_machine(self):
        source = (ROOT / "core/platform/web/tcPlatform_web.cpp").read_text(encoding="utf-8")
        start = source.index("EM_JS(int, applyWebWakeLock,")
        start = source.index("{\n", start) + 2
        body = source[start:source.index("\n});", start)]
        fixture = pathlib.Path(__file__).with_name("fixtures") / "web_wake_lock.js"
        script = "const productionBody = " + json.dumps(body) + ";\n"
        script += fixture.read_text(encoding="utf-8")
        result = subprocess.run([shutil.which("node"), "--unhandled-rejections=strict"],
                                input=script, text=True, encoding="utf-8", capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS 23 wake-lock scenarios", result.stdout)


if __name__ == "__main__":
    unittest.main()
