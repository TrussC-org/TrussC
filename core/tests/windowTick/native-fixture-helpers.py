"""Check the helper boundary of SDK-free, copied production fixtures."""
import re


def definitions(code):
    # Ignore comments and literals so diagnostics describe actual dependencies.
    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  ' ', code, flags=re.S)
    return code, set(re.findall(
        r'\b(_sapp_tc_\w+)\s*\([^;{}()]*\)\s*(?:noexcept\s*)?(?::[^;{}]*)?\{', code))


class FixtureBodies:
    def __init__(self, extract):
        self.extract = extract
        self.copied = []

    def __call__(self, source, marker):
        return self.record(self.extract(source, marker))

    def record(self, code):
        self.copied.append(code)
        return code

    def check(self, code, *, fixture, stubs, declarations=()):
        return check_helpers(code, copied='\n'.join(self.copied), fixture=fixture,
                             stubs=stubs, declarations=declarations)


def check_helpers(code, *, copied, fixture, stubs, declarations=()):
    clean, defined = definitions(code)
    _, copied_definitions = definitions(copied)
    calls = set(re.findall(r'\b(_sapp_tc_\w+)\s*\(', clean))
    # Bare references (function pointers/callbacks), excluding type aliases.
    calls.update(name for name in re.findall(r'\b(_sapp_tc_\w+)\s*(?=[;,)])', clean)
                 if not name.endswith('_t'))
    # The explicit stub list documents the replacement boundary; everything
    # else must have a body copied from production or come from a named header.
    missing_stubs = set(stubs) - defined
    if missing_stubs:
        raise ValueError(f'{fixture}: listed stubs have no definition: ' + ', '.join(sorted(missing_stubs)))
    missing = calls - copied_definitions - set(stubs) - set(declarations)
    if missing:
        raise ValueError(f'{fixture}: unhandled native helpers: ' + ', '.join(sorted(missing))
                         + '; copy the production body or add an explicit stub/declaration')
    return code
