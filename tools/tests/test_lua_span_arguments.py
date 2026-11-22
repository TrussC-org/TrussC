"""Keep non-owning C++ spans out of generated Lua member bindings."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LuaSpanArgumentsTests(unittest.TestCase):
    def test_member_argument_filter(self):
        source = (ROOT / 'addons/tcxLua/tools/luagen-types/luagen-types.js').read_text(encoding='utf-8')
        helper = source[source.index('const PRIM ='):source.index('// types-only param list')]
        subprocess.run(['node', '-e', helper + r'''
const assert = require('assert');
for (const type of ['std::span<const unsigned char>', 'std::span<unsigned char>',
                    'std::span<float>', 'std::span<const uint8_t>']) {
    assert.equal(argBindable({ type }), false, type);
}
assert.equal(argBindable({ type: 'const std::string &', isRef: true, isConst: true }), true);
assert.equal(argBindable({ type: 'void *', isPointer: true }), false);
'''], check=True)


if __name__ == '__main__':
    unittest.main()
