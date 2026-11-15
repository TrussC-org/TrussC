"""Compile the production Android control flow against deterministic native seams."""
from pathlib import Path
import sys

header = Path(sys.argv[1]).read_text(encoding="utf-8")
# Scope to the actual Android implementation, excluding other platform copies.
header = header[header.index('_SOKOL_PRIVATE bool _sapp_tc_android_init_egl('):]

def body(name):
    start = header.index('_SOKOL_PRIVATE ' + name)
    opening = header.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (header[end] == '{') - (header[end] == '}')
        end += 1
    return header[start:end] + '\n'

names = ('bool _sapp_tc_android_app_event(', 'void _sapp_tc_android_cleanup(',
         'void _sapp_tc_android_shutdown(', 'bool _sapp_tc_android_key_event(',
         'bool _sapp_tc_android_should_update(')
code = ''.join(body(name) for name in names)
code += '#if __ANDROID_API__ >= 29\n' + body('void _sapp_tc_android_frame_callback(') + '#endif\n'
code += body('void* _sapp_tc_android_loop(')
Path(sys.argv[2]).write_text(code, encoding="utf-8")
