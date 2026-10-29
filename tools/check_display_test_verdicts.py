#!/usr/bin/env python3
"""Reject display-test verdicts computed after a non-returning launcher (#831).

This is deliberately a source check, including inactive preprocessor branches.
Every launcher call must be the entire returned expression. Tests using
runDisplayTest put their final checks in its callback, before exitApp(code).
The few tests of returning startup failure, fatal abort, atexit verdicts or
Linux-only post-run lifetime must annotate the individual call and explain why
it is safe.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
LAUNCHERS = {"runApp", "TC_RUN_APP", "runDisplayTest"}
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".m", ".mm"}
# Exceptions are individual annotated calls, never an excluded test file.
EXCEPTIONS = {
    ("core/tests/startupExit/src/main.cpp", "returning-startup-failure"),
    ("core/tests/loggerThreadSafety/src/main.cpp", "expected-abort"),
    ("core/tests/hotReloadLifecycle/src/main.cpp", "atexit-verdict"),
    ("core/tests/windowTick/late-window.cpp", "linux-returning-lifetime"),
}
NON_CODE = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)
TOKEN = re.compile(r'[A-Za-z_]\w*|::|[^\s]')
ANNOTATION = re.compile(r'// runapp-verdict: ([a-z-]+) -- (\S[^\n]*)')


def mask_non_code(source):
    return NON_CODE.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), source)


def matching(tokens, start, left, right):
    depth = 0
    for index in range(start, len(tokens)):
        value = tokens[index].group()
        if value == left:
            depth += 1
        elif value == right:
            depth -= 1
            if depth == 0:
                return index
    return None


def violations(source, path):
    """Return (line, diagnostic) pairs for non-terminal launcher calls."""
    tokens = list(TOKEN.finditer(mask_non_code(source)))
    lines = source.splitlines()
    result = []
    for index, token in enumerate(tokens):
        name = token.group()
        if name not in LAUNCHERS:
            continue
        end = index + 1
        if end < len(tokens) and tokens[end].group() == '<':
            end = matching(tokens, end, '<', '>')
            if end is None:
                continue
            end += 1
        if end >= len(tokens) or tokens[end].group() != '(':
            continue
        end = matching(tokens, end, '(', ')')
        if end is None:
            continue
        after = tokens[end + 1].group() if end + 1 < len(tokens) else ''
        # A function definition, rather than an invocation (the shared helper).
        if after == '{':
            continue
        before = index - 1
        # Walk back over a namespace qualifier, including leading ::.
        while before >= 0 and tokens[before].group() == '::':
            before -= 1
            if (before >= 0 and tokens[before].group() != 'return'
                    and re.fullmatch(r'[A-Za-z_]\w*', tokens[before].group())):
                before -= 1
        if before >= 0 and tokens[before].group() == 'return' and after == ';':
            continue
        line = source.count('\n', 0, token.start()) + 1
        previous = lines[line - 2] if line > 1 else ''
        annotation = ANNOTATION.search(previous)
        if annotation and (path, annotation[1]) in EXCEPTIONS:
            continue
        result.append((line, f'{name} must be returned directly; move final checks '
                       'into tcCoreTest::runDisplayTest(..., failures, finalChecks)'))
    return result


def test_sources(root):
    roots = [root / 'core/tests', root / 'examples/tests']
    roots.extend(path for path in (root / 'addons').glob('*/tests*') if path.is_dir())
    for test_root in roots:
        for path in sorted(test_root.rglob('*')):
            if (path.is_file() and path.suffix in SUFFIXES
                    and not any(part == 'build' or part.startswith(('build-', 'cmake-build-'))
                                or part in {'bin', 'emscripten', '_deps'}
                                for part in path.relative_to(test_root).parts)):
                yield path


def check(root=ROOT):
    errors = []
    for path in test_sources(root):
        relative = path.relative_to(root).as_posix()
        errors.extend(f'{relative}:{line}: {message}' for line, message in
                      violations(path.read_text(encoding='utf-8'), relative))
    return errors


def main():
    errors = check()
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Display test verdicts: all launchers return directly or have an audited exception.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
