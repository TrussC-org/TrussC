"""Compile the Web C entry points unchanged with a native browser boundary."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")

def function(signature):
    start = source.rindex(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]

parts = ['#define _SAPP_EMSCRIPTEN 1', '#define SOKOL_API_IMPL',
         '#define SOKOL_ASSERT assert',
         '#define _SAPP_STRUCT(type, name) type name = {}']
for name in ('_SAPP_TC_MAX_DROP_CONTENT_SIZE', '_SAPP_TC_MAX_DROP_READERS'):
    match = re.search(r'^#define ' + name + r' .+$', source, re.M)
    if match:
        parts.append(match[0])
parts.append(function('static inline void _sapp_tc_log'))
parts.append(function('EMSCRIPTEN_KEEPALIVE void _sapp_tc_emsc_invoke_fetch_cb').replace('EMSCRIPTEN_KEEPALIVE ', 'static '))
result = re.search(r'EM_JS\((\w+), _sapp_tc_js_dropped_file_size,', source)[1]
parts.append(f'using BrowserSize = {result};')
parts.append('static BrowserSize _sapp_tc_js_dropped_file_size(int index);')
if 'SOKOL_API_IMPL uint64_t sapp_html5_get_dropped_file_size64' in source:
    parts.append(function('SOKOL_API_IMPL uint64_t sapp_html5_get_dropped_file_size64'))
parts.append(function('SOKOL_API_IMPL uint32_t sapp_html5_get_dropped_file_size'))
parts.append(function('SOKOL_API_IMPL void sapp_html5_fetch_dropped_file'))
pathlib.Path(sys.argv[2]).write_text('\n\n'.join(parts) + '\n', encoding="utf-8")
