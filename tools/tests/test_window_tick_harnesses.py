"""Offline dependency-boundary regressions for the windowTick fixtures."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'core/tests/windowTick'
NATIVE = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')


class NativeFixtureTests(unittest.TestCase):
    def test_current_production_dependencies_are_accounted_for(self):
        for name in ('native-event-fixture.py', 'native-admission.py'):
            with self.subTest(generator=name):
                runpy.run_path(str(FIXTURES / name))['generate'](NATIVE)

    def test_new_helper_call_fails_with_symbol_and_fixture_name(self):
        # Both fixtures copy X11 code containing this call. Simulate the next
        # production helper dependency without adding a matching fixture stub.
        changed = NATIVE.replace('_sapp_tc_x11_update_pacer();',
                                 '_sapp_tc_future_helper();')
        for name in ('native-event-fixture.py', 'native-admission.py'):
            with self.subTest(generator=name):
                generate = runpy.run_path(str(FIXTURES / name))['generate']
                with self.assertRaisesRegex(ValueError, r'native-.*_sapp_tc_future_helper'):
                    generate(changed)

    def test_copied_body_or_explicit_declaration_is_required(self):
        check = runpy.run_path(str(FIXTURES / 'native-fixture-helpers.py'))['check_helpers']
        code = 'void _sapp_tc_helper() {}\nvoid run() { _sapp_tc_helper(); }'
        with self.assertRaisesRegex(ValueError, '_sapp_tc_helper'):
            check(code, copied='', fixture='test', stubs=())
        check(code, copied=code, fixture='test', stubs=())
        check(code, copied='', fixture='test', stubs=('_sapp_tc_helper',))
        check('void run() { _sapp_tc_helper(); }', copied='', fixture='test',
              stubs=(), declarations=('_sapp_tc_helper',))
        with self.assertRaisesRegex(ValueError, 'listed stubs have no definition'):
            check('', copied='', fixture='test', stubs=('_sapp_tc_helper',))

    def test_condition_calls_are_not_mistaken_for_copied_definitions(self):
        check = runpy.run_path(str(FIXTURES / 'native-fixture-helpers.py'))['check_helpers']
        code = 'void run() { if (_sapp_tc_future_helper()) {} }'
        with self.assertRaisesRegex(ValueError, '_sapp_tc_future_helper'):
            check(code, copied=code, fixture='test', stubs=())
        with self.assertRaisesRegex(ValueError, '_sapp_tc_future_helper'):
            check('auto callback = &_sapp_tc_future_helper;', copied='', fixture='test', stubs=())
        with self.assertRaisesRegex(ValueError, '_sapp_tc_future_helper'):
            check('void run() { register_callback(_sapp_tc_future_helper); }', copied='', fixture='test', stubs=())
        check('// _sapp_tc_comment();\nconst char* text = "_sapp_tc_literal()";',
              copied='', fixture='test', stubs=())


if __name__ == '__main__':
    unittest.main()
