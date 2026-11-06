#!/usr/bin/env python3
"""Check sapp call/definition coverage without SDKs, downloads, or a C parser.

Run directly (also runs the embedded unit tests), optionally with --root TREE.
Assumptions: function signatures use ordinary C return types and balanced
parentheses, followed by ';' or '{'. Preprocessor expressions use defined,
identifiers, integer literals, !, &&, || and parentheses. Other expressions and
non-platform feature macros are unknown: callers explore both branches, but
only definitely active definitions count. No include expansion or arbitrary
#define evaluation is performed. The explicit guards below model implementation
inclusion; X11 GLCORE and GLES3 are checked separately.
Platform source directories model the build's source selection. Comments and
quoted literals (including C++ raw strings) are blanked, preserving line numbers.
"""
import argparse
from bisect import bisect_right
from contextlib import redirect_stdout
import io
from pathlib import Path
import re
import unittest
from tempfile import TemporaryDirectory

PLATFORMS = ('macOS', 'Win32', 'X11', 'iOS', 'Android', 'Web')
CONFIGURATIONS = ('macOS', 'Win32', 'X11 GLCORE', 'X11 GLES3', 'iOS', 'Android', 'Web')

# Model the implementation inclusion used by core/platform/*/sokol_impl.*.
# INCLUDED is active at the implementation body; DONE and include guards are
# absent on first entry. These are structural guards, not optional features.
IMPLEMENTATION_GUARDS = dict.fromkeys((
    'SOKOL_IMPL', 'SOKOL_APP_TC_IMPL', 'SOKOL_APP_TC_IMPL_INCLUDED',
    'SOKOL_APP_IMPL_INCLUDED'), True)
IMPLEMENTATION_GUARDS.update(dict.fromkeys((
    'SOKOL_APP_TC_IMPL_DONE', 'SOKOL_APP_TC_INCLUDED', 'SOKOL_APP_INCLUDED',
    'SOKOL_WINDOW_NATIVE_INCLUDED'), False))


def platform_for(configuration):
    return 'X11' if configuration.startswith('X11 ') else configuration


def configurations(platforms):
    return {c for c in CONFIGURATIONS if platform_for(c) in platforms}


FOLDERS = dict(zip(('mac', 'win', 'linux', 'ios', 'android', 'web'), PLATFORMS))

# Intentional native APIs: restrict coverage to their actual native target.
# These are exact names, not prefixes, so new native APIs require review.
PLATFORM_ONLY = {
    'sapp_macos_get_window': {'macOS'},  # NSWindow access for Mac geometry/dialogs.
    'sapp_window_macos_get_window': {'macOS'},  # Secondary NSWindow access.
    'sapp_win32_get_hwnd': {'Win32'},  # HWND access for Windows geometry.
    'sapp_window_win32_get_hwnd': {'Win32'},  # Secondary HWND/dialog owner.
    'sapp_x11_get_display': {'X11'},  # Xlib display connection.
    'sapp_x11_get_window': {'X11'},  # Main Xlib window ID.
    'sapp_window_x11_get_window': {'X11'},  # Secondary Xlib window ID.
    'sapp_ios_get_window': {'iOS'},  # UIWindow for the iOS dialog presenter.
    'sapp_ios_set_supported_orientations': {'iOS'},  # UIKit orientation mask.
    'sapp_android_get_native_activity': {'Android'},  # JNI native activity bridge.
}


def macros(configuration):
    """Known platform values; missing keys mean unknown, not undefined."""
    platform = platform_for(configuration)
    return {
        **IMPLEMENTATION_GUARDS,
        '__cplusplus': True,
        '__APPLE__': platform in ('macOS', 'iOS'),
        'TARGET_OS_OSX': platform == 'macOS',
        'TARGET_OS_IPHONE': platform == 'iOS',
        'TARGET_OS_IOS': platform == 'iOS',
        '_WIN32': platform == 'Win32',
        '__linux__': platform in ('X11', 'Android'),
        '__ANDROID__': platform == 'Android',
        '__EMSCRIPTEN__': platform == 'Web',
        '_SAPP_IOS': platform == 'iOS',
        '_SAPP_ANDROID': platform == 'Android',
        '_SAPP_EMSCRIPTEN': platform == 'Web',
        'SOKOL_METAL': platform in ('macOS', 'iOS'),
        'SOKOL_D3D11': platform == 'Win32',
        'SOKOL_GLCORE': configuration == 'X11 GLCORE',
        # Web backend selection stays unknown; X11 has two explicit builds.
        'SOKOL_GLES3': (None if platform == 'Web' else
                        configuration == 'X11 GLES3' or platform == 'Android'),
        'SOKOL_WGPU': None if platform == 'Web' else False,
    }


def negate(value):
    return None if value is None else not value


def conjunction(a, b):
    if a is False or b is False:
        return False
    return None if a is None or b is None else True


def condition(expr, known):
    """Three-valued evaluation; unsupported atoms remain unknown."""
    def defined(match):
        name = match[1] or match[2]
        # TargetConditionals.h defines both true and false TARGET_OS_* values.
        value = known.get('__APPLE__') if name.startswith('TARGET_OS_') else known.get(name)
        return '__unknown_condition' if value is None else str(int(value))

    expr = re.sub(r'\bdefined\s*(?:\(\s*(\w+)\s*\)|(\w+))',
                  defined, expr).strip()
    # Split only at top level, respecting C's || / && precedence.
    for operator in ('||', '&&'):
        depth = 0
        for i, char in enumerate(expr):
            depth += (char == '(') - (char == ')')
            if depth == 0 and expr.startswith(operator, i):
                a = condition(expr[:i], known)
                b = condition(expr[i + 2:], known)
                if operator == '&&':
                    return conjunction(a, b)
                return negate(conjunction(negate(a), negate(b)))
    if expr.startswith('('):
        depth = 0
        for i, char in enumerate(expr):
            depth += (char == '(') - (char == ')')
            if depth == 0:
                if i == len(expr) - 1:
                    return condition(expr[1:-1], known)
                break
    if expr.startswith('!') and not expr.startswith('!='):
        return negate(condition(expr[1:], known))
    if re.fullmatch(r'\d+', expr):
        return bool(int(expr))
    return known.get(expr)


def blank_literals(source):
    pattern = r'R"([^\s()\\]{0,16})\([\s\S]*?\)\1"|/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\''
    return re.sub(pattern, lambda m: re.sub(r'[^\n]', ' ', m[0]), source)


def platform_lines(source, guaranteed=False):
    """Track nested #if/elif/else/endif, including common code after #endif."""
    active = dict.fromkeys(CONFIGURATIONS, True)
    stack = []
    result = []
    # Join directive continuations without changing physical line numbering.
    lines = source.splitlines(keepends=True)
    pending = ''
    for line in lines:
        result.append({p for p, value in active.items() if value is True or (value is None and not guaranteed)})
        pending += line
        if pending.rstrip('\n').endswith('\\'):
            pending = pending.rstrip('\n')[:-1] + ' '
            continue
        match = re.match(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', pending, re.S)
        pending = ''
        if not match:
            continue
        kind, expr = match.groups()
        if kind in ('if', 'ifdef', 'ifndef'):
            if kind in ('ifdef', 'ifndef'):
                expr = f'defined({expr.strip()})'
            test = {p: condition(expr, macros(p)) for p in CONFIGURATIONS}
            if kind == 'ifndef':
                test = {p: negate(v) for p, v in test.items()}
            stack.append((active.copy(), {p: negate(v) for p, v in test.items()}))
            active = {p: conjunction(active[p], test[p]) for p in CONFIGURATIONS}
        elif kind in ('elif', 'else'):
            parent, remaining = stack[-1]
            test = {p: condition(expr, macros(p)) if kind == 'elif' else True
                    for p in CONFIGURATIONS}
            active = {p: conjunction(parent[p], conjunction(remaining[p], test[p]))
                      for p in CONFIGURATIONS}
            stack[-1] = (parent, {p: conjunction(remaining[p], negate(test[p]))
                                  for p in CONFIGURATIONS})
        else:
            active, _ = stack.pop()
    if stack:
        raise ValueError('unclosed preprocessor condition')
    return result


FUNCTION = re.compile(r'\bsapp_[a-z0-9_]+\s*\(')


def occurrences(source, guaranteed=False):
    """Yield (name, line, kind); signatures are distinguished from expressions."""
    source = blank_literals(source)
    masks = platform_lines(source, guaranteed)
    # Lines of #define bodies (with continuations). Their calls count, but an
    # unexpanded body defines nothing, so definition collection skips them.
    macro_lines = set()
    in_macro = False
    for number, text in enumerate(source.split('\n')):
        if in_macro or re.match(r'^\s*#\s*define\b', text):
            macro_lines.add(number)
            in_macro = text.rstrip().endswith('\\')
    source = re.sub(
        r'^\s*#\s*(?:define[ \t]+\w+(?:\([^\n]*?\))?|[^\n]*)',
        lambda m: re.sub(r'[^\n]', ' ', m[0]), source, flags=re.M)
    starts = [0] + [m.end() for m in re.finditer('\n', source)]
    for match in FUNCTION.finditer(source):
        line = bisect_right(starts, match.start()) - 1
        prefix = source[starts[line]:match.start()].strip()
        # Return types may wrap onto preceding lines. Stop at the first
        # non-type line rather than interpreting surrounding C++ statements.
        start = starts[line]
        while start > 0 and re.fullmatch(r'[\w\s*&]*', prefix):
            previous = source.rfind('\n', 0, start - 1) + 1
            part = source[previous:start].strip()
            if not part or not re.fullmatch(r'[\w\s*&]+', part):
                break
            prefix = part + ' ' + prefix
            start = previous
        cursor = match.end()
        depth = 1
        while cursor < len(source) and depth:
            depth += (source[cursor] == '(') - (source[cursor] == ')')
            cursor += 1
        tail = source[cursor:].lstrip()
        # Ordinary return types / storage qualifiers, no expression operators.
        signature = (bool(re.fullmatch(r'[\w\s*&]+', prefix))
                     and not re.search(r'\b(return|else|co_return|throw)\b', prefix)
                     and tail.startswith((';', '{')))
        kind = ('definition' if tail.startswith('{') else 'declaration') if signature else 'call'
        if guaranteed and line in macro_lines:
            continue
        yield match[0].split('(')[0].strip(), line + 1, kind, masks[line]


def check(root):
    definitions = {}
    for relative in ('core/include/sokol/sokol_app_tc.h',
                     'core/include/sokol/util/sokol_window_native.h'):
        for name, _, kind, platforms in occurrences((root / relative).read_text(encoding='utf-8'), guaranteed=True):
            if kind == 'definition':
                definitions.setdefault(name, set()).update(platforms)
    files = [root / 'core/include/TrussC.h']
    for directory in ('core/include/tc', 'core/platform'):
        files += sorted(p for p in (root / directory).rglob('*')
                        if p.suffix in ('.h', '.cpp', '.mm'))
    missing = {}
    calls = set()
    for path in files:
        relative = path.relative_to(root)
        scope = set(PLATFORMS)
        if relative.parts[:2] == ('core', 'platform'):
            folder = relative.parts[2]
            if folder in FOLDERS:
                scope = {FOLDERS[folder]}
            elif folder == 'metal':
                scope = {'macOS', 'iOS'}
            elif folder == 'gl':
                scope = {'X11', 'Android', 'Web'}
        source = path.read_text(encoding='utf-8')
        clean = blank_literals(source)
        desktop_calls = {clean.count('\n', 0, m.start(1)) + 1 for m in re.finditer(
            r'\bif\s+constexpr\s*\(\s*supportsSecondaryWindows\s*\(\s*\)\s*\)\s*'
            r'(sapp_tc_window_skip_present\s*\([^;]*\)\s*;)', clean)}
        for name, line, kind, platforms in occurrences(source):
            if kind != 'call':
                continue
            required = platforms & configurations(scope & PLATFORM_ONLY.get(name, set(PLATFORMS)))
            # Intentional desktop-only API: tcWindow.h makes this constexpr
            # predicate false on mobile/Web. Require the guard at the call, so
            # removing it cannot silently inherit a function-wide exemption.
            if name == 'sapp_tc_window_skip_present' and line in desktop_calls:
                required &= configurations({'macOS', 'Win32', 'X11'})
            if required:
                calls.add(name)
            for platform in required - definitions.get(name, set()):
                missing.setdefault((name, platform), (relative, line))
    for (name, platform), (path, line) in sorted(missing.items()):
        print(f'{path}:{line}: {name}: missing definition for {platform}')
    if not missing:
        print(f'PASS: {len(calls)} called sapp functions have definitions on reachable platforms')
    return bool(missing)


class TrackerTests(unittest.TestCase):
    def test_platform_select_and_common(self):
        header = '''#if defined(__APPLE__) && TARGET_OS_OSX
void sapp_mac(void) {}
#if UNKNOWN_FEATURE
void sapp_feature(void) {}
#else
void sapp_other_feature(void) {}
#endif
#elif defined(__APPLE__) && TARGET_OS_IPHONE
void sapp_ios(void) {}
#elif defined(_WIN32)
void sapp_win(void) {}
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
void sapp_x11(void) {}
#elif defined(__ANDROID__)
void sapp_android(void) {}
#else
void sapp_web(void) {}
#endif
void sapp_common(void) {}
'''
        found = {name: platforms for name, _, kind, platforms in occurrences(header, guaranteed=True)
                 if kind == 'definition'}
        for name, platform in zip(('mac', 'win', 'x11', 'ios', 'android', 'web'), PLATFORMS):
            self.assertEqual(found['sapp_' + name], configurations({platform}))
        self.assertEqual(found['sapp_common'], set(CONFIGURATIONS))
        self.assertEqual(found['sapp_feature'], set())
        self.assertEqual(found['sapp_other_feature'], set())
        self.assertTrue(condition('defined(TARGET_OS_IPHONE) && !TARGET_OS_IPHONE',
                                  macros('macOS')))
        self.assertFalse(condition('defined(TARGET_OS_IPHONE)', macros('Win32')))

    def probe(self, caller, header=''):
        with TemporaryDirectory(prefix='sapp-definition-test-') as directory:
            root = Path(directory)
            for relative, source in (
                    ('core/include/TrussC.h', caller),
                    ('core/include/sokol/sokol_app_tc.h', header),
                    ('core/include/sokol/util/sokol_window_native.h', '')):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding='utf-8')
            output = io.StringIO()
            with redirect_stdout(output):
                failed = check(root)
            return failed, output.getvalue()

    def test_macro_body_call(self):
        failed, output = self.probe(
            '#define TC_QUERY_DESC() sapp_query_desc()\n'
            'void run() { TC_QUERY_DESC(); }\n')
        self.assertTrue(failed)
        self.assertIn('TrussC.h:1: sapp_query_desc: missing definition for macOS', output)
        self.assertEqual([(n, k) for n, _, k, _ in occurrences(
            '#define sapp_macro() sapp_query_desc()\n')], [('sapp_query_desc', 'call')])

    def test_optional_definition(self):
        caller = '#if defined(__APPLE__) && TARGET_OS_OSX\nsapp_query_desc();\n#endif\n'
        header = ('#if defined(__APPLE__) && TARGET_OS_OSX && defined(SAPP_OPTIONAL_QUERY)\n'
                  'sapp_desc sapp_query_desc(void) { return {}; }\n#endif\n')
        failed, output = self.probe(caller, header)
        self.assertTrue(failed)
        self.assertIn('sapp_query_desc: missing definition for macOS', output)
        failed, _ = self.probe(caller, '#if defined(SOKOL_APP_TC_IMPL_INCLUDED) && '
                               '!defined(SOKOL_APP_TC_IMPL_DONE)\n'
                               'sapp_desc sapp_query_desc(void) { return {}; }\n#endif\n')
        self.assertFalse(failed)

    def test_macro_body_definition_is_not_a_definition(self):
        caller = 'void f() { sapp_query_desc(); }\n'
        failed, output = self.probe(caller, '#define UNUSED_QUERY() \\\n'
                                    '    sapp_desc sapp_query_desc(void) { return {}; }\n')
        self.assertTrue(failed)
        self.assertIn('sapp_query_desc: missing definition for macOS', output)

    def test_gles3_caller_and_definition(self):
        caller = ('#if defined(__linux__) && !defined(__ANDROID__) && '
                  '!defined(__EMSCRIPTEN__) && !defined(SOKOL_GLCORE)\n'
                  'void regression_probe() { sapp_missing_egl(); }\n#endif\n')
        for header in ('', '#if defined(SOKOL_GLCORE)\n'
                       'void sapp_missing_egl(void) {}\n#endif\n'):
            failed, output = self.probe(caller, header)
            self.assertTrue(failed)
            self.assertIn('sapp_missing_egl: missing definition for X11 GLES3', output)
            self.assertNotIn('X11 GLCORE', output)
        failed, _ = self.probe(caller, '#if defined(SOKOL_GLES3)\n'
                               'void sapp_missing_egl(void) {}\n#endif\n')
        self.assertFalse(failed)

    def test_ifdef_zero_value(self):
        failed, output = self.probe('#ifdef TARGET_OS_IPHONE\nsapp_query_desc();\n#endif\n')
        self.assertTrue(failed)
        self.assertIn('sapp_query_desc: missing definition for macOS', output)
        masks = platform_lines('#ifndef TARGET_OS_IPHONE\nsapp_query_desc();\n#endif\n')
        self.assertNotIn('macOS', masks[1])
        self.assertIn('Win32', masks[1])

    def test_wrapped_desktop_guard(self):
        header = ('#if (defined(__APPLE__) && TARGET_OS_OSX) || defined(_WIN32) || '
                  '(defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))\n'
                  'void sapp_tc_window_skip_present(int handle) {}\n#endif\n')
        call = 'sapp_tc_window_skip_present(another_handle);\n'
        failed, _ = self.probe('if constexpr (\n supportsSecondaryWindows( )\n)\n' + call, header)
        self.assertFalse(failed)
        failed, output = self.probe(call, header)
        self.assertTrue(failed)
        for platform in ('iOS', 'Android', 'Web'):
            self.assertIn('missing definition for ' + platform, output)

    def test_calls_and_literals(self):
        source = '''// sapp_comment();
/* sapp_comment(); */
const char* text = R"tag(sapp_string();)tag";
const void*
sapp_decl(void);
void
sapp_defined(int x) { sapp_real(x); }
void run() { auto value = sapp_value(); }
return sapp_returned();
'''
        self.assertEqual([(n, k) for n, _, k, _ in occurrences(source)], [
            ('sapp_decl', 'declaration'), ('sapp_defined', 'definition'),
            ('sapp_real', 'call'), ('sapp_value', 'call'), ('sapp_returned', 'call')])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()
    tests = unittest.TextTestRunner(verbosity=2 if args.verbose else 1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(TrackerTests))
    raise SystemExit(check(args.root.resolve()) if tests.wasSuccessful() else 1)
