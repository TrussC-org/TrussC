"""#239 growable local drops/clipboard. Run via tcbuild.sh; no device required.

TC_D3B_MUTANTS=1 also rebuilds the real X11 implementation with each old cap.
TC_D3B_BUILD_DIR keeps binaries under an existing checkout build-cache link.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from test_window_pixel_size import body

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'core/include/sokol/sokol_app_tc.h'
BUILD = Path(os.environ.get('TC_D3B_BUILD_DIR', ROOT / '.build/d3b'))


def compile_run(source):
    BUILD.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=BUILD, prefix='seam-') as scratch:
        cpp = Path(scratch) / 'test.cpp'
        cpp.write_text(source, encoding='utf-8')
        binary = Path(scratch) / 'test'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT / 'core/include'),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


class DropClipboardUnbounded(unittest.TestCase):
    def test_removed_descriptor_fields(self):
        compile_run(r'''
#include "sokol/sokol_app_tc.h"
template<class T> concept HasCount = requires(T t) { t.max_dropped_files; };
template<class T> concept HasLength = requires(T t) { t.max_dropped_file_path_length; };
template<class T> concept HasClipboard = requires(T t) { t.clipboard_size; };
static_assert(!HasCount<sapp_desc> && !HasLength<sapp_desc> && !HasClipboard<sapp_desc>);
int main() {}
''')

    def test_web_callbacks(self):
        native = HEADER.read_text(encoding='utf-8')
        web = native[native.index('EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_onpaste'):]
        callbacks = '\n'.join(body(web, signature) for signature in (
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_onpaste',
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_begin_drop',
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_drop',
            'EMSCRIPTEN_KEEPALIVE void _sapp_tc_emsc_end_drop'))
        setters = '\n'.join(body(web, signature) for signature in (
            'SOKOL_API_IMPL void sapp_set_clipboard_string',
            'SOKOL_API_IMPL const char* sapp_get_clipboard_string',
            'SOKOL_API_IMPL int sapp_get_num_dropped_files',
            'SOKOL_API_IMPL const char* sapp_get_dropped_file_path'))
        compile_run(r'''
#include "sokol/sokol_app_tc.h"
#include <string>
#include <vector>
#include <cassert>
#define EMSCRIPTEN_KEEPALIVE
#define SOKOL_API_IMPL
#define SOKOL_ASSERT assert
#define _SAPP_EMSCRIPTEN
struct { struct { bool enabled=true; } clipboard,drop;
struct { float x,y,dx,dy; } mouse; float dpi_scale=1; sapp_event event{}; } _sapp_tc;
std::string _sapp_tc_clipboard_text,copied;
std::vector<std::string> _sapp_tc_dropped_paths;
int pastes=0,drops=0;
bool _sapp_tc_events_enabled(){return true;}
void _sapp_tc_init_event(sapp_event_type type){_sapp_tc.event.type=type;}
void _sapp_tc_call_event(sapp_event* e){
    if(e->type==SAPP_EVENTTYPE_FILES_DROPPED) ++drops;
    if(e->type==SAPP_EVENTTYPE_CLIPBOARD_PASTED) ++pastes;
}
void _sapp_tc_clear_drop_buffer(){_sapp_tc_dropped_paths.clear();}
void _sapp_tc_emsc_set_clipboard_string(const char* text){copied=text;}
''' + callbacks + setters + r'''
int main(){
    std::string text; while(text.size()<200*1024) text+="012345-日本語\n";
    sapp_set_clipboard_string(text.c_str());
    assert(copied==text && sapp_get_clipboard_string()==text);
    _sapp_tc_emsc_onpaste(text.c_str()); assert(pastes==1 && sapp_get_clipboard_string()==text);
    _sapp_tc_emsc_begin_drop(20);
    for(int i=0;i<20;++i) _sapp_tc_emsc_drop(i,(std::to_string(i)+std::string(6000,'x')).c_str());
    _sapp_tc_emsc_end_drop(1,2,0); assert(drops==1 && sapp_get_num_dropped_files()==20);
    for(int i=0;i<20;++i) assert(sapp_get_dropped_file_path(i)==std::to_string(i)+std::string(6000,'x'));
    _sapp_tc_emsc_begin_drop(1);_sapp_tc_emsc_drop(0,"short");_sapp_tc_emsc_end_drop(0,0,0);
    assert(sapp_get_num_dropped_files()==1 && std::string(sapp_get_dropped_file_path(0))=="short");
    sapp_set_clipboard_string("");assert(std::string(sapp_get_clipboard_string()).empty());
}
''')

    def test_web_heap_bridge(self):
        native = HEADER.read_text(encoding='utf-8')
        paste = body(native, 'EM_JS(void, _sapp_tc_js_add_clipboard_listener')
        drop = body(native, 'EM_JS(void, _sapp_tc_js_add_dragndrop_listeners')
        javascript = r'''
const assert = require('assert');
const Module = {};
const text = '012345-日本語\n'.repeat(20000);
let allocations=0, frees=0, outOfMemory=false;
const pointers = new Map();
function stringToNewUTF8(value) {
    ++allocations;
    if(outOfMemory) return 0;
    pointers.set(allocations, value); return allocations;
}
function _free(pointer) { assert(pointers.delete(pointer)); ++frees; }
let received='', filenames=[];
function __sapp_tc_emsc_onpaste(pointer) { received=pointers.get(pointer); return 1; }
function __sapp_tc_emsc_begin_drop(count) { filenames=Array(count); return 1; }
function __sapp_tc_emsc_drop(index,pointer) { filenames[index]=pointers.get(pointer); return 1; }
function __sapp_tc_emsc_end_drop() {}
const window = {addEventListener(){}};
Module.sapp_emsc_target = {addEventListener(){}};
''' + paste[paste.index('{')+1:-1] + drop[drop.index('{')+1:-1] + r'''
Module.sokol_paste({clipboardData:{getData(){return text;}}});
assert.strictEqual(received,text);
const files=Array.from({length:20},(_,i)=>({name:i+text}));
Module.sokol_drop({stopPropagation(){},preventDefault(){},dataTransfer:{files}});
assert.deepStrictEqual(filenames,files.map(file=>file.name));
assert.strictEqual(allocations,21); assert.strictEqual(frees,21);
assert.strictEqual(pointers.size,0);
outOfMemory=true;
assert.throws(()=>Module.sokol_paste({clipboardData:{getData(){return text;}}}), /Out of memory/);
'''
        BUILD.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=BUILD, prefix='js-') as scratch:
            script = Path(scratch) / 'bridge.js'
            script.write_text(javascript.replace(r'\x2F', '/').replace(r'\x2A', '*'), encoding='utf-8')
            subprocess.run(['node', str(script)], check=True)

    def test_windows_paths_and_clipboard(self):
        native = HEADER.read_text(encoding='utf-8')
        start = native.index('static void _sapp_tc_win32_files_dropped')
        windows = native[start:native.index('/*== Linux', start)]
        functions = '\n'.join(body(windows, signature) for signature in (
            'static void _sapp_tc_win32_files_dropped',
            'void sapp_set_clipboard_string', 'const char* sapp_get_clipboard_string',
            'int sapp_get_num_dropped_files', 'const char* sapp_get_dropped_file_path'))
        compile_run(r'''
#include "sokol/sokol_app_tc.h"
#include <string>
#include <vector>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <codecvt>
#include <locale>
using WCHAR=wchar_t;using UINT=unsigned;using BOOL=bool;using SIZE_T=size_t;
using HDROP=int;using HANDLE=void*;
constexpr int CP_UTF8=65001,WC_ERR_INVALID_CHARS=1,MB_ERR_INVALID_CHARS=1;
constexpr int GMEM_MOVEABLE=1,CF_UNICODETEXT=1;
struct POINT {int x=1,y=2;};
struct _sapp_tc_window_t {int hwnd=1;float mouse_scale=1,mouse_x=0,mouse_y=0;};
_sapp_tc_window_t window;
struct {struct {bool drop_enabled=true,clipboard_enabled=true;_sapp_tc_window_t* main=&window;}app;}_sapp_tc;
std::string _sapp_tc_clipboard_text;
std::vector<std::string> _sapp_tc_dropped_paths;
std::vector<std::wstring> incoming;
std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
int queried=-1,finished=0,events=0;
UINT DragQueryFileW(HDROP,UINT i,WCHAR* target,UINT count){
    if(i==0xFFFFFFFF) return (UINT)incoming.size();
    if(!target){assert(count==0);queried=(int)i;return (UINT)incoming[i].size();}
    assert(queried==(int)i && count==incoming[i].size()+1);queried=-1;
    std::wcscpy(target,incoming[i].c_str());return (UINT)incoming[i].size();
}
BOOL DragQueryPoint(HDROP,POINT*){return true;}
void DragFinish(HDROP){++finished;}
int WideCharToMultiByte(int,int,const WCHAR* input,int,char* target,int size,void*,void*){
    std::string text=convert.to_bytes(input);if(!target)return (int)text.size()+1;
    assert(size==(int)text.size()+1);std::memcpy(target,text.c_str(),text.size()+1);return size;
}
int MultiByteToWideChar(int,int,const char* input,int,WCHAR* target,int size){
    std::wstring text=convert.from_bytes(input);if(!target)return (int)text.size()+1;
    assert(size==(int)text.size()+1);std::wcscpy(target,text.c_str());return size;
}
bool _sapp_tc_win32_wide_to_utf8(const WCHAR* input,char* output,int size){return WideCharToMultiByte(0,0,input,-1,output,size,nullptr,nullptr)>0;}
bool _sapp_tc_win32_utf8_to_wide(const char* input,WCHAR* output,int size){return MultiByteToWideChar(0,0,input,-1,output,size)>0;}
HANDLE clipboard=nullptr;size_t allocation=0;
HANDLE GlobalAlloc(int,size_t bytes){allocation=bytes;return std::calloc(1,bytes);}
void* GlobalLock(HANDLE object){return object;}
void GlobalUnlock(HANDLE){}
void GlobalFree(HANDLE object){std::free(object);}
bool OpenClipboard(int){return true;}
void CloseClipboard(){}
void EmptyClipboard(){std::free(clipboard);clipboard=nullptr;}
HANDLE SetClipboardData(int,HANDLE object){return clipboard=object;}
HANDLE GetClipboardData(int){return clipboard;}
unsigned _sapp_tc_win32_mods(){return 0;}
void _sapp_tc_send(_sapp_tc_window_t*,sapp_event* e){assert(e->type==SAPP_EVENTTYPE_FILES_DROPPED);++events;}
''' + functions + r'''
int main(){
    for(int i=0;i<20;++i) incoming.push_back(L"C:/日本語/"+std::to_wstring(i)+std::wstring(6000,L'x'));
    _sapp_tc_win32_files_dropped(&window,1);
    assert(finished==1 && events==1 && sapp_get_num_dropped_files()==20);
    for(int i=0;i<20;++i) assert(sapp_get_dropped_file_path(i)==convert.to_bytes(incoming[i]));
    std::string text;while(text.size()<200*1024)text+="012345-日本語\n";
    sapp_set_clipboard_string(text.c_str());
    assert(allocation==(convert.from_bytes(text).size()+1)*sizeof(WCHAR));
    _sapp_tc_clipboard_text="stale";
    assert(sapp_get_clipboard_string()==text);
    sapp_set_clipboard_string("");assert(std::string(sapp_get_clipboard_string()).empty());
    EmptyClipboard();
}
''')

    def test_windows_oom_boundary(self):
        native = HEADER.read_text(encoding='utf-8')
        boundary = body(native, 'static LRESULT CALLBACK _sapp_tc_wndproc(')
        compile_run(r'''
#include "sokol/sokol_app_tc.h"
#include <new>
#include <cassert>
#include <cstring>
#define CALLBACK
using LRESULT=int; using HWND=int; using UINT=unsigned; using WPARAM=int; using LPARAM=int;
constexpr UINT WM_CLOSE=1, WM_DROPFILES=2, WM_KEYDOWN=3;
struct { struct { sapp_desc desc{}; } app; } _sapp_tc;
int reports=0; const char* message=nullptr;
void _sapp_tc_log(const sapp_logger*, unsigned level, unsigned item, unsigned,
                  const char*, const char*, const char* text) {
    assert(level==1 && item==SAPP_LOGITEM_MALLOC_FAILED);
    ++reports; message=text;
}
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 42;}
LRESULT _sapp_tc_wndproc_impl(HWND,UINT,WPARAM,LPARAM){throw std::bad_alloc();}
''' + boundary + r'''
int main(){
    assert(_sapp_tc_wndproc(0,WM_DROPFILES,0,0)==42);
    assert(reports==1 && std::strstr(message,"File drop failed: out of memory"));
    assert(_sapp_tc_wndproc(0,WM_KEYDOWN,0,0)==42);
    assert(reports==2 && std::strstr(message,"clipboard handling failed: out of memory"));
    assert(_sapp_tc_wndproc(0,WM_CLOSE,0,0)==0 && reports==3);
}
''')

    def test_web_native_oom_wasm(self):
        emxx = shutil.which('em++') or '/home/toru/emsdk/upstream/emscripten/em++'
        if not Path(emxx).is_file():
            self.skipTest('installed Emscripten required for native bad_alloc -> JS Error seam')
        native = HEADER.read_text(encoding='utf-8')
        callbacks = '\n'.join(body(native, signature) for signature in (
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_onpaste',
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_begin_drop',
            'EMSCRIPTEN_KEEPALIVE int _sapp_tc_emsc_drop',
            'EMSCRIPTEN_KEEPALIVE void _sapp_tc_emsc_end_drop'))
        bridges = '\n'.join(body(native, signature) + ')' for signature in (
            'EM_JS(void, _sapp_tc_js_add_clipboard_listener',
            'EM_JS(void, _sapp_tc_js_add_dragndrop_listeners'))
        source = r'''
#include "sokol/sokol_app_tc.h"
#include <emscripten.h>
#include <string>
#include <vector>
#include <new>
#include <cstdlib>
EM_JS_DEPS(sokol_app_tc, "$stringToNewUTF8,$findCanvasEventTarget")
static bool fail_next=false;
void* operator new(size_t size) {
    if (fail_next) { fail_next=false; throw std::bad_alloc(); }
    if (void* p=std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p, size_t) noexcept {std::free(p);}
struct {struct {bool enabled=true;} clipboard,drop;
    struct {float x,y,dx,dy;} mouse; float dpi_scale=1; sapp_event event{};} _sapp_tc;
std::string _sapp_tc_clipboard_text;
std::vector<std::string> _sapp_tc_dropped_paths;
int events=0;
bool _sapp_tc_events_enabled(){return true;}
void _sapp_tc_init_event(sapp_event_type type){_sapp_tc.event.type=type;}
void _sapp_tc_call_event(sapp_event*){++events;}
void _sapp_tc_clear_drop_buffer(){_sapp_tc_dropped_paths.clear();}
extern "C" {
EMSCRIPTEN_KEEPALIVE void arm_oom(){fail_next=true;}
''' + callbacks + r'''
}
''' + bridges + r'''
EM_JS(void, checkNativeOomErrors, (), {
        const assert=require('assert');
        const text='large filename / clipboard '.repeat(10000);
        const drop={stopPropagation(){},preventDefault(){},dataTransfer:{files:[{name:text}]}};
        const oom=(fn, message)=>assert.throws(fn, error=>error instanceof Error && error.message===message);
        // JS malloc succeeds; the next C++ string allocation throws bad_alloc.
        _arm_oom();
        oom(()=>Module.sokol_paste({clipboardData:{getData(){return text;}}}),
            'Out of memory copying clipboard text');
        // Empty vector: fail its first allocation, before JS copies any names.
        _arm_oom();
        oom(()=>Module.sokol_drop(drop),'Out of memory copying dropped filenames');
        // Pre-size the vector so only the filename string needs an allocation.
        assert.strictEqual(__sapp_tc_emsc_begin_drop(1),1);
        _arm_oom();
        oom(()=>Module.sokol_drop(drop),'Out of memory copying dropped filename');
        console.log('C++ bad_alloc -> JS OOM Error: paste, drop vector, drop string PASS');
});
int main(){
    EM_ASM({
        globalThis.window={addEventListener(){}};
        Module.sapp_emsc_target={addEventListener(){}};
    });
    _sapp_tc_js_add_clipboard_listener();
    _sapp_tc_js_add_dragndrop_listeners();
    checkNativeOomErrors();
    return events != 0;
}
'''
        BUILD.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=BUILD, prefix='wasm-oom-') as scratch:
            cpp = Path(scratch) / 'test.cpp'
            js = Path(scratch) / 'test.js'
            cpp.write_text(source, encoding='utf-8')
            subprocess.run([str(emxx), '-std=c++20', '-O2', '-fexceptions',
                '-I', str(ROOT / 'core/include'), str(cpp), '-sENVIRONMENT=node',
                '-sEXIT_RUNTIME=1', '-sEXPORTED_FUNCTIONS=["_main","_malloc","_free"]',
                '-o', str(js)], check=True)
            subprocess.run(['node', str(js)], check=True)

    def test_x11_and_old_cap_mutants(self):
        BUILD.mkdir(parents=True, exist_ok=True)
        original = HEADER.read_text(encoding='utf-8')
        variants = {'current': original}
        if os.environ.get('TC_D3B_MUTANTS') == '1':
            parser = original.index('static bool _sapp_tc_x11_parse_dropped_files_list')
            insertion = original.index('    _sapp_tc_dropped_paths = std::move(paths);', parser)
            variants['16-files'] = original[:insertion] + '    if (paths.size() > 16) paths.resize(16);\n' + original[insertion:]
            variants['2048-path'] = original[:insertion] + '    for (auto& path : paths) if (path.size() >= 2048) path.resize(2047);\n' + original[insertion:]
            setter = original.index('void sapp_set_clipboard_string', parser)
            insertion = original.index('    _sapp_tc_clipboard_text = str;', setter)
            end = insertion + len('    _sapp_tc_clipboard_text = str;')
            variants['65536-clipboard'] = original[:insertion] + '    _sapp_tc_clipboard_text = std::string(str).substr(0, 65535);' + original[end:]
        for name, contents in variants.items():
            with self.subTest(variant=name), tempfile.TemporaryDirectory(dir=BUILD, prefix=name+'-') as scratch:
                include = Path(scratch)
                sokol = include / 'sokol'; sokol.mkdir()
                for path in HEADER.parent.iterdir():
                    if path.name != HEADER.name: (sokol / path.name).symlink_to(path)
                (include / 'tc').symlink_to(ROOT / 'core/include/tc')
                (sokol / HEADER.name).write_text(contents, encoding='utf-8')
                for backend, libraries in [('SOKOL_GLCORE', ['-lGL']), ('SOKOL_GLES3', ['-lEGL', '-lGLESv2'])]:
                    binary = include / backend
                    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-D'+backend,
                        '-I', str(include), str(ROOT / 'tools/tests/drop_clipboard_x11.cpp'),
                        *libraries, '-lX11', '-lXi', '-lXcursor', '-lpthread', '-ldl', '-o', str(binary)], check=True)
                    result = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x720x24 -noreset', str(binary)],
                        env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe',
                                 __GLX_VENDOR_LIBRARY_NAME='mesa',
                                 __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json'), text=True, encoding='utf-8',
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                    print(f'{name} {backend}: exit {result.returncode}\n{result.stdout}', flush=True)
                    if name == 'current': self.assertEqual(result.returncode, 0, result.stdout)
                    else:
                        self.assertNotEqual(result.returncode, 0, 'old cap mutant survived')
                        self.assertIn('Assertion', result.stdout, 'mutant must fail a data assertion')


if __name__ == '__main__':
    unittest.main()
