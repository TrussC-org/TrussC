#!/usr/bin/env python3
"""Compile the fork's Web title and TrussC size bodies, run under Node.

Run through tcbuild.sh; this is a browser-free seam, not a WGPU device test.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    start = source.rindex(signature)
    opening = source.index('{', start)
    depth = 1
    cursor = opening + 1
    while depth:
        if source[cursor] == '{':
            depth += 1
        elif source[cursor] == '}':
            depth -= 1
        cursor += 1
    return source[start:cursor]


def main():
    compiler = os.environ.get('EMXX') or shutil.which('em++')
    if not compiler:
        raise SystemExit('em++ required (set EMXX)')
    sokol = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
    platform = (ROOT / 'core/platform/web/tcPlatform_web.cpp').read_text(encoding='utf-8')
    title = body(sokol, 'SOKOL_API_IMPL void sapp_set_window_title(')
    size = body(platform, 'void internal::setMainWindowSizeLogical(')
    initial = re.search(r'const char\* document_title = .*?;', sokol).group()
    init_start = sokol.index('    if (c_str_document_title !== 0)')
    init_end = sokol.index('    const target_selector_str', init_start)
    initial_js = sokol[init_start:init_end]
    source = r'''
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>
#define SOKOL_API_IMPL
#define SOKOL_ASSERT assert
#define _SAPP_EMSCRIPTEN
struct { char window_title[128]; } _sapp_tc;
void _sapp_tc_strcpy(const char* src, char* dst, size_t n) { std::strncpy(dst, src, n); }
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
int notices = 0;
Log logNotice(const char*) { ++notices; return {}; }
namespace internal { void setMainWindowSizeLogical(int, int); }
EM_JS(void, init_title, (const char* c_str_document_title), { INITIAL_JS });
TITLE_BODY
SIZE_BODY
int main() {
    EM_ASM({ globalThis.document = {title: '{{{ TITLE }}}'}; });
    std::strcpy(_sapp_tc.window_title, "Initial title");
    struct { struct { bool update_document_title = false; } html5; } descriptor;
    auto* desc = &descriptor;
    INITIAL_CPP
    init_title(document_title);
    assert(EM_ASM_INT({ return document.title === 'Initial title'; }));
    sapp_set_window_title("Unicode \xe9\x89\x84\xe5\xa1\x94 <literal>");
    assert(EM_ASM_INT({ return document.title === 'Unicode 鉄塔 <literal>'; }));
    EM_ASM({ globalThis.canvas = {}; canvas.width = 2000; canvas.height = 1314; canvas.style = {}; canvas.style.width = '100%'; canvas.style.height = '100%'; document.querySelector = (selector) => canvas; });
    internal::setMainWindowSizeLogical(800, 600);
    assert(EM_ASM_INT({ return canvas.width === 2000 && canvas.height === 1314 && canvas.style.width === '100%' && canvas.style.height === '100%'; }));
    assert(notices == 1);
    return 0;
}
'''.replace('INITIAL_JS', initial_js).replace('TITLE_BODY', title).replace('SIZE_BODY', size).replace('INITIAL_CPP', initial)
    with tempfile.TemporaryDirectory(prefix='web-window-controls-') as temp:
        path = Path(temp)
        (path / 'main.cpp').write_text(source, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', str(path / 'main.cpp'), '-o', str(path / 'test.js'), '-sENVIRONMENT=node', '-sSINGLE_FILE=1', '-sASSERTIONS=1'], check=True)
        subprocess.run(['node', str(path / 'test.js')], check=True)
    print('PASS: production Web title bodies and no-op size (Node)')


if __name__ == '__main__':
    main()
