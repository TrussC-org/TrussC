"""Small #239 D-min production-source smokes; run through tcbuild.sh.

TC_DMIN_REVISION=<revision> replays production-source checks at that revision.
Native checks include real X11 execution and AppKit/Win32 seams, not device verification.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from test_window_pixel_size import body, between

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    revision = os.environ.get('TC_DMIN_REVISION')
    if revision:
        return subprocess.check_output(['git', 'show', f'{revision}:{path}'], cwd=ROOT, text=True, encoding='utf-8')
    return (ROOT / path).read_text(encoding='utf-8')


def execute(source):
    with tempfile.TemporaryDirectory(prefix='dmin-') as scratch:
        cpp = Path(scratch) / 'smoke.cpp'
        cpp.write_text(source, encoding='utf-8')
        binary = Path(scratch) / 'smoke'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT / 'core/include'),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


class WindowRuntimeSettings(unittest.TestCase):
    def test_mac_drop_registration(self):
        native = read('core/include/sokol/sokol_app_tc.h')
        constructor = body(native, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window_ex(')
        registration = '[w->view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];'
        self.assertTrue(registration in constructor, 'secondary views must register file URLs')
        self.assertLess(constructor.index(registration), constructor.index('[window makeKeyAndOrderFront:nil]'))
        # Replay the actual registration and handler delivery suffix. Only the
        # native pasteboard decoding/coordinate conversion is stubbed here.
        handler = body(native, '- (BOOL)performDragOperation:')
        # Compile the untouched Objective-C methods and registration against
        # declarations of the AppKit seam, rather than translating their syntax.
        objc = r'''
#include "sokol/sokol_app_tc.h"
#include <cstring>
#define nil ((id)0)
using NSUInteger=unsigned long; using BOOL=bool;
constexpr BOOL NO=false,YES=true;
struct NSPoint { double x,y; };
__attribute__((objc_root_class)) @interface NSObject @end
@interface NSString : NSObject
@property(readonly) const char* UTF8String;
@end
@interface NSArray : NSObject
+ (instancetype)arrayWithObjects:(const id*)objects count:(NSUInteger)count;
- (BOOL)containsObject:(id)object;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
@property(readonly) NSUInteger count;
@end
@interface NSPasteboardItem : NSObject
- (NSString*)stringForType:(NSString*)type;
@end
@interface NSPasteboard : NSObject
@property(readonly) NSArray* types;
@property(readonly) NSArray* pasteboardItems;
@end
@interface NSURL : NSObject
+ (NSURL*)URLWithString:(NSString*)string;
@property(readonly) NSURL* standardizedURL;
@property(readonly) NSString* path;
@end
@protocol NSDraggingInfo
@property(readonly) NSPasteboard* draggingPasteboard;
@property(readonly) NSPoint draggingLocation;
@end
@class NSEvent;
@interface _sapp_tc_view : NSObject
@property(assign) struct _sapp_tc_window_t* w;
- (NSPoint)convertPoint:(NSPoint)point fromView:(id)view;
- (void)registerForDraggedTypes:(NSArray*)types;
@end
struct _sapp_tc_window_t { _sapp_tc_view* view; float dpi_scale; };
struct { struct { bool drop_enabled,mouse_shown; char* drop_buffer;
int drop_max_files,drop_max_path_length,drop_num_files; sapp_mouse_cursor current_cursor; } app; } _sapp_tc;
extern NSString* NSPasteboardTypeFileURL;
static void _sapp_tc_send(_sapp_tc_window_t*,sapp_event*) {}
static void _sapp_tc_apply_cursor(sapp_mouse_cursor,bool) {}
void registerView(_sapp_tc_window_t* w) { REGISTRATION }
@implementation _sapp_tc_view
METHODS
@end
'''.replace('REGISTRATION', registration).replace('METHODS', handler + '\n' + body(native, '- (void)cursorUpdate:'))
        with tempfile.TemporaryDirectory(prefix='dmin-objc-') as scratch:
            path = Path(scratch) / 'drop-cursor.mm'
            path.write_text(objc, encoding='utf-8')
            subprocess.run([shutil.which('clang++') or 'clang++', '-std=c++20', '-fsyntax-only',
                            '-fobjc-arc', '-fobjc-runtime=macosx-10.13', '-Wno-incomplete-implementation',
                            '-I', str(ROOT / 'core/include'), str(path)], check=True)
        delivery = handler[handler.index('    sapp_event e;'):].removesuffix('}')
        delivery = delivery.replace('[self convertPoint:sender.draggingLocation fromView:nil]', 'point')
        registration = registration.replace(registration, 'w->view->registerFileURLs();')
        execute(r'''
#include "sokol/sokol_app_tc.h"
#include <cassert>
#include <cstring>
struct View { bool registered=false; void registerFileURLs(){registered=true;} };
struct Window { View* view; float dpi_scale=2; unsigned id; int drops=0; };
struct NSPoint { float x=3,y=4; } point;
constexpr bool YES=true;
void _sapp_tc_send(Window* w,sapp_event* e) {
    assert(w->view->registered && e->type==SAPP_EVENTTYPE_FILES_DROPPED);
    assert(e->mouse_x==6 && e->mouse_y==8); ++w->drops;
}
bool create(Window* w) { ''' + registration + r''' return true; }
bool drop(Window* w) { ''' + delivery + r''' }
int main(){ View a,b; Window first{&a,2,11},second{&b,2,22};
create(&first);create(&second);assert(drop(&second));assert(second.drops==1 && first.drops==0); }
''')

    def test_app_cursor_and_imgui(self):
        native = read('core/include/sokol/sokol_app_tc.h')
        mac = body(native, '- (void)cursorUpdate:')
        self.assertIn('_sapp_tc.app.current_cursor, _sapp_tc.app.mouse_shown', mac)
        win = between(body(native, 'static LRESULT _sapp_tc_wndproc_impl('),
                      '        case WM_SETCURSOR:', '        case WM_DPICHANGED:')
        self.assertIn('_sapp_tc.app.current_cursor', win)
        self.assertIn('_sapp_tc.app.mouse_shown', win)
        header = read('core/include/TrussC.h')
        wrappers = '\n'.join(body(header, signature) for signature in (
            'inline void showCursor(', 'inline void hideCursor(',
            'inline void setCursor(', 'inline Cursor getCursor('))
        execute(r'''
#include <cassert>
enum class Cursor { Default, Hand }; using sapp_mouse_cursor=int;
bool sapp_isvalid(){return true;}
int resolutions=0,current=0; bool shown=true;
namespace internal { void* findWindowApiContext(){++resolutions;return nullptr;} }
void sapp_show_mouse(bool value){shown=value;}
void sapp_set_mouse_cursor(sapp_mouse_cursor value){current=value;}
sapp_mouse_cursor sapp_get_mouse_cursor(){return current;}
''' + wrappers + r'''
int main(){ setCursor(Cursor::Hand);assert(getCursor()==Cursor::Hand);
hideCursor();assert(!shown);showCursor();assert(shown);
assert(resolutions==0); }
''')
        wrapper = read('addons/tcxImGui/src/tcImGuiTargetImpl.h')
        condition = between(wrapper, '    _simgui.desc.disable_set_mouse_cursor = ', ';')
        execute(r'''
#include <cassert>
namespace tc { int getWindowWidth(){return 100;} int getWindowHeight(){return 80;} }
struct Target { bool isFbo=false; } target;
struct Window { bool pointerValid=false; float mouseX=0,mouseY=0; } a,b;
struct Frame { Target* target; Window* window; } frame{&target,&a};
bool disabled(bool savedCursorControl=false) { return ''' + condition + r'''; }
int main(){ assert(disabled()); a.pointerValid=b.pointerValid=true;
a.mouseX=10;a.mouseY=20;b.mouseX=b.mouseY=-1;
assert(!disabled());frame.window=&b;assert(disabled());
frame.window=&a;a.mouseX=a.mouseY=-1;b.mouseX=20;b.mouseY=10;
assert(disabled());frame.window=&b;assert(!disabled());
assert(disabled(true));target.isFbo=true;assert(disabled());
target.isFbo=false;b.mouseX=100;assert(disabled()); }
''')

    def test_cursor_native_lifetime(self):
        # Actual X11 implementation: no fake cursor setters or fake display.
        header = read('core/include/TrussC.h')
        wrappers = '\n'.join(body(header, signature) for signature in (
            'inline void showCursor(', 'inline void hideCursor(', 'inline void setCursor(',
            'inline Cursor getCursor(', 'inline void bindCursorImage(Cursor cursor, int width,',
            'inline void unbindCursorImage('))
        source = r'''
#define SOKOL_APP_TC_IMPL
#define SOKOL_NO_ENTRY
#include "sokol/sokol_app_tc.h"
#include <cassert>
namespace trussc {
enum class Cursor { Default=SAPP_MOUSECURSOR_DEFAULT, Hand=SAPP_MOUSECURSOR_POINTING_HAND,
Custom0=SAPP_MOUSECURSOR_CUSTOM_0 };
''' + wrappers + r'''
void helpers() {
    unsigned char pixel[4]={255,255,255,255};
    setCursor(Cursor::Hand); hideCursor(); showCursor();
    bindCursorImage(Cursor::Custom0,1,1,pixel); unbindCursorImage(Cursor::Custom0);
    (void)getCursor();
}
}
using namespace trussc;
int main() {
    assert(!sapp_isvalid()); helpers();
    sapp_desc desc={}; desc.width=64; desc.height=64;
    desc.init_cb=[] { assert(sapp_isvalid()); helpers(); };
    desc.frame_cb=[] { sapp_quit(); };
    sapp_run(&desc);
    assert(!sapp_isvalid()); helpers();
}
'''
        with tempfile.TemporaryDirectory(prefix='dmin-cursor-') as scratch:
            cpp = Path(scratch) / 'cursor.cpp'; cpp.write_text(source, encoding='utf-8')
            binary = Path(scratch) / 'cursor'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DSOKOL_GLCORE',
                            '-I', str(ROOT / 'core/include'), str(cpp), '-lGL', '-lX11',
                            '-lXi', '-lXcursor', '-lpthread', '-ldl', '-o', str(binary)], check=True)
            subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x720x24 -noreset',
                            str(binary)], check=True, env=dict(os.environ,
                            LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe',
                            __GLX_VENDOR_LIBRARY_NAME='mesa',
                            __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json'))

    def test_window_query_globals(self):
        header = read('core/include/TrussC.h')
        self.assertTrue('const std::string& getWindowTitle()' in header, 'getWindowTitle is missing')
        self.assertTrue('bool isWindowOccluded()' in header, 'isWindowOccluded is missing')
        globals_source = read('core/include/tc/app/tcGlobal.cpp')
        runtime = read('core/include/tc/app/tcWindowRuntime.cpp')
        execute(r'''
#include <cassert>
#include <string>
#include <vector>
struct Window {
    std::string title; bool hidden;
    const std::string& getTitle(){return title;}
    bool isOccluded(){return hidden;}
};
struct Owner { bool ended=false,retiring=false; Window* window; };
Owner* active=nullptr; std::vector<Window*> windows; int errors=0;
Owner* currentAppOwner(){return active;}
auto& openWindows(){return windows;}
struct Log { template<class T> Log& operator<<(T){return *this;} };
Log logError(const char*){++errors;return {};}
''' + body(runtime, 'Window* resolveWindow(bool report)') + r'''
namespace internal { Window* currentWindow(){return resolveWindow(true);} }
''' + body(globals_source, 'const std::string& getWindowTitle()') +
            body(globals_source, 'bool isWindowOccluded()') + r'''
int main(){
    assert(getWindowTitle().empty() && !isWindowOccluded() && errors==2);
    Window a{"First",false}, b{"Second",true}; windows={&a};
    assert(getWindowTitle()=="First" && !isWindowOccluded() && errors==2);
    windows.push_back(&b);
    assert(getWindowTitle().empty() && !isWindowOccluded() && errors==4);
    Owner first{false,false,&a},second{false,false,&b};
    active=&first; assert(getWindowTitle()=="First" && !isWindowOccluded());
    active=&second; assert(getWindowTitle()=="Second" && isWindowOccluded());
    assert(errors==4);
}
''')

    def test_runtime_settings(self):
        header = read('core/include/TrussC.h')
        settings = body(header, 'struct WindowSettings {') + ';\n'
        runtime = body(header, 'struct RuntimeSettings {') + ';\n'
        assignment = between(header, '    // Per-frame uniform reservation for sg_setup (consumed in internal::setup()).\n',
                             '    internal::configureGraphicsPools(settings);')
        # Keep the actual runApp forwarding/return expression (the old overload).
        forward = body(header[header.index('#endif\n\ntemplate<typename AppClass>\nint runApp('):],
                       'int runApp(')
        execute('#include <string>\n#include <cassert>\n#include <type_traits>\n' + settings + runtime + r'''
template<class T> concept HasWindowReserve = requires(T t) { t.uniformBufferReserve; t.reserveUniformBuffer(1); };
static_assert(!HasWindowReserve<WindowSettings>);
namespace internal { int gpuUniformBufferReserve=0; }
void configure(const RuntimeSettings& runtime) { ''' + assignment + r''' }
template<class T> int runApp(const WindowSettings&, const RuntimeSettings& r) { configure(r); return 23; }
template<class AppClass>
''' + forward + r'''
int main(){ RuntimeSettings runtime; assert(runtime.uniformBufferReserve==0);
assert(&runtime.reserveUniformBuffer(4096)==&runtime); configure(runtime);
assert(internal::gpuUniformBufferReserve==4096);
static_assert(std::is_aggregate_v<RuntimeSettings>);
assert(runApp<int>(WindowSettings{}, runtime)==23);
assert(runApp<int>()==23 && internal::gpuUniformBufferReserve==0); }
''')

    def test_fixed_clipboard(self):
        header = read('core/include/TrussC.h')
        settings = body(header, 'struct WindowSettings {') + ';\n'
        capacity = between(header, '    desc.clipboard_size = ', ';')
        execute('#include <string>\n#include <cassert>\n' + settings + r'''
template<class T> concept HasClipboardSize = requires(T t) { t.clipboardSize; };
template<class T> concept HasClipboardSetter = requires(T t) { t.setClipboardSize(1); };
static_assert(!HasClipboardSize<WindowSettings> && !HasClipboardSetter<WindowSettings>);
int main(){ struct { int clipboard_size=0; } desc; WindowSettings settings;
desc.clipboard_size = ''' + capacity + r'''; assert(desc.clipboard_size==65536); }
''')
        self.assertNotIn('setClipboardSize', body(header, 'inline void setClipboardString('))

    def test_mcp_window_id(self):
        source = read('core/include/tc/utils/tcStandardTools.cpp')
        listing = body(between(source, 'coreTool(server, "tc_list_windows"', '    // Lists and source lookup'), '[]() -> json')
        execute(r'''
#include <nlohmann/json.hpp>
#include <cassert>
using json=nlohmann::json;
struct Window { int id; const char* getTitle(){return "smoke";} int getWidth(){return 100;}
int getHeight(){return 80;} bool isOccluded(){return false;} } a{17},b{29};
namespace trussc::internal {
auto openWindows(){return std::vector<Window*>{&a,&b};}
struct WindowRequestAccess { static int creationId(const Window& w){return w.id;} };
}
json list() ''' + listing[listing.index('{'):] + r'''
int main(){auto rows=list().at("windows");assert(rows.size()==2);
assert(rows[0].at("id")==17 && rows[1].at("id")==29);
assert(!rows[0].contains("index") && !rows[1].contains("index"));}
''')


if __name__ == '__main__':
    unittest.main()
