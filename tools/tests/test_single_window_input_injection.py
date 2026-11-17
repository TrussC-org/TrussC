#!/usr/bin/env python3
"""Run single-window bootstrap, injection and native delivery without mobile SDKs."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(os.environ.get('TC_SINGLE_INPUT_ROOT', Path(__file__).resolve().parents[2]))
runtime = (ROOT / 'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
bridge = (ROOT / 'core/platform/tcNativeWindowEvent.h').read_text(encoding='utf-8')
node = (ROOT / 'core/include/tcNode.h').read_text(encoding='utf-8')
app = (ROOT / 'core/include/tcBaseApp.h').read_text(encoding='utf-8')
# The sole-surface init/event/cleanup bodies occur in iOS, Android, Web order.
initializers = list(re.finditer(r'_SOKOL_PRIVATE void _sapp_tc_call_init\(void\) \{', native))
assert len(initializers) == 3
prefix = r'''
#include "sokol_app_tc.h"
#include "util/sokol_window_event.h"
#include "util/sokol_window_creation.h"
#include <cstdio>
#include <string>
#include <limits>
#include <vector>
int checks=0, failures=0;
void check(const char* label, bool ok) {
    ++checks;
    if (!ok) { ++failures; std::printf("single-input: FAIL: %s\n", label); }
}
'''
parts = []
for index, ((platform, defines), initializer) in enumerate(zip((
    ('ios', '#define __APPLE__ 1\n#define TARGET_OS_IPHONE 1'),
    ('android', '#define __ANDROID__ 1'),
    ('web', '#define __EMSCRIPTEN__ 1'),
), initializers)):
    section = native[initializer.start():initializers[index + 1].start() if index + 1 < len(initializers) else len(native)]
    code = r'''
#undef __linux__
#undef __APPLE__
#undef TARGET_OS_IPHONE
#undef __ANDROID__
#undef __EMSCRIPTEN__
''' + defines + '\nnamespace ' + platform + r''' {
struct {
    bool valid=true, init_called=false, constructing=false, cleanup_called=false, event_consumed=false;
    sapp_desc desc{};
    uint64_t frame_count=7;
    int window_width=80, window_height=40, framebuffer_width=160, framebuffer_height=80;
} _sapp_tc;
bool stopping=false;
bool runtimeEnding() { return stopping; }
bool sapp_isvalid() { return _sapp_tc.valid; }
int emscripten_run_script_int(const char*) { return 1; }
constexpr bool supportsSecondaryWindows() { return false; }
struct Operations { bool (*sendEvent)(sapp_tc_window, void*, sapp_event*); };
struct Binding { sapp_tc_window handle{}; const Operations* operations=nullptr; void* user=nullptr; bool bound=false, focused=false; };
struct Window { Binding binding; bool isOpen() const { return binding.bound; } } window;
struct WindowRequestAccess { static Binding& binding(Window& w) { return w.binding; } };
struct TouchEventArgs {};
struct DragDropEventArgs { float x=0, y=0; };
struct { bool bootstrapping=false; } coordinatorState;
auto& coordinator() { return coordinatorState; }
struct WindowEntryScope {
    Window& window;
    WindowEntryScope(Window& w, bool, bool) : window(w) {}
    operator bool() const { return window.isOpen() && !runtimeEnding(); }
};
struct TeardownSteps { template<class F> void run(const char*, F fn) { fn(); } };
struct Owner { bool ended=false, retiring=false; Window* window=nullptr; };
Owner* selectedOwner=nullptr;
Owner* currentAppOwner() { return selectedOwner; }
Window otherWindow;
std::vector<Window*> openWindows() { return window.isOpen() ? std::vector<Window*>{&window} : std::vector<Window*>{}; }
struct Logger { template<class T> Logger& operator<<(const T&) { return *this; } };
Logger logError(const char*) { return {}; }
Window* resolveWindow(bool report=false);
void windowEvent(const sapp_event*, sapp_tc_window, void*);
''' + body(section, '_SOKOL_PRIVATE bool _sapp_tc_call_event(') + '\n'
    code += body(runtime, 'Window* resolveWindow(') + '\n'
    code += body(bridge, 'bool sendNativeWindowEvent(') + '\n'
    code += body(runtime, 'bool sendEvent(') + r'''
const Operations& productionWindowOperations() { static Operations ops{sendEvent}; return ops; }
'''
    for signature in ('static inline bool injectedKeyDefined(', 'static inline bool validInjectedWindowEvent('):
        code += body(runtime, signature) + '\n'
    code += body(runtime, 'struct InjectedWindowEvent {') + ';\nthread_local InjectedWindowEvent* injectedWindowEvent=nullptr;\n'
    for signature in ('bool injectWindowEvent(', 'void singleWindowEvent(', 'void bootstrapWindow('):
        code += body(runtime, signature) + '\n'
    for signature in ('_SOKOL_PRIVATE void _sapp_tc_call_init(', '_SOKOL_PRIVATE void _sapp_tc_call_frame(', '_SOKOL_PRIVATE void _sapp_tc_call_cleanup('):
        code += body(section, signature) + '\n'
    # Extract both the setup-once entry and App update entry; only the Node tree is a seam.
    code += r'''
void setup();
void update();
struct Node {
    bool liveRoot_=false, setupCalled_=false;
    void* liveContext_=nullptr;
    void setupTree() { setupCalled_=true; setup(); }
    void updateTree() { update(); }
};
namespace internal {
struct Root { void* target=nullptr; Root lock() { return *this; } void* get() { return target; } };
struct Context { Root rootNode; } context;
auto& currentWindowContext() { return context; }
enum class AppEntry { Setup };
struct EntryStackGuard { EntryStackGuard(AppEntry) {} };
'''
    code += body(node, 'inline void setupNodeOnce(') + '\n}\nstruct TestApp : Node {\n'
    code += body(app, '    void handleUpdate(') + '\n} app;\n'
    code += r'''
int delivered=0;
bool inSetup=false;
void windowEvent(const sapp_event* event, sapp_tc_window handle, void* user) {
    check("bridge selects the open main Window with zero native ID", user==&window && handle.id==0);
    check("native metadata stamped", event->frame_count==7 && event->window_width==80 &&
          event->window_height==40 && event->framebuffer_width==160 && event->framebuffer_height==80);
    check("injection provenance reaches bridge", injectedWindowEvent && injectedWindowEvent->window==&window &&
          (injectedWindowEvent->sourceTag=="tuio" || injectedWindowEvent->sourceTag=="mcp"));
    if (inSetup) check("setup injection is synchronous", _sapp_tc.init_called && !_sapp_tc.constructing);
    ++delivered;
}
bool inject(const std::string& tag="tuio") {
    sapp_event event{}; event.type=SAPP_EVENTTYPE_MOUSE_DOWN; event.mouse_button=SAPP_MOUSEBUTTON_LEFT;
    event.mouse_x=20; event.mouse_y=30;
    const bool accepted=injectWindowEvent(window, &event, false, tag, nullptr, nullptr);
    check("injection context restored", !injectedWindowEvent);
    return accepted;
}
void setup() {
    inSetup=true;
    check("sole-surface bootstrap retains the legacy control binding", window.binding.bound && !window.binding.handle.id && !window.binding.operations);
    check("setup injection accepted", inject());
    check("setup listener ran before inject returned", delivered==1);
    inSetup=false;
}
void update() {
    check("first update injection accepted", inject());
    check("setup and update each deliver once", delivered==2);
}
void construct() {
    check("constructor runs during native construction", !_sapp_tc.init_called && _sapp_tc.constructing);
    check("native constructor injection rejected", !inject());
    check("native constructor injection not delivered", delivered==0);
    Owner candidate;
    selectedOwner=&candidate;
    check("unattached native constructor injection rejected", !inject());
    selectedOwner=nullptr;
}
void cleanup() {
    stopping=true; // Runtime marks teardown before invoking App cleanup.
    check("cleanup in progress rejects injection", !inject());
}
void run() {
    window.binding.operations=&productionWindowOperations();
    window.binding.bound=true;
    check("before init rejected", !inject());
    window.binding={};
    _sapp_tc.desc.init_cb=[] { bootstrapWindow(window, construct); };
    _sapp_tc.desc.event_cb=singleWindowEvent;
    _sapp_tc.desc.cleanup_cb=cleanup;
    internal::context.rootNode.target=&app;
    _sapp_tc.desc.frame_cb=[] { app.handleUpdate(); };
    _sapp_tc_call_init();
    Owner attached;
    attached.window=&window;
    selectedOwner=&attached;
    check("setup waits for first update", !app.setupCalled_ && delivered==0);
    _sapp_tc_call_frame();
    check("setup entered through setupNodeOnce", app.setupCalled_ && app.liveRoot_ && app.liveContext_==&internal::context);
    sapp_event key{}; key.type=SAPP_EVENTTYPE_KEY_DOWN; key.key_code=SAPP_KEYCODE_A;
    check("MCP key path accepted", injectWindowEvent(window, &key, false, "mcp", nullptr, nullptr));
    check("MCP mouse path accepted", inject("mcp"));
    check("MCP events delivered", delivered==4);
    // setApp selects an unattached candidate owner during its constructor.
    Owner candidate;
    selectedOwner=&candidate;
    check("replacement constructor injection rejected", !inject());
    candidate.window=&otherWindow;
    check("different resolved destination rejected", !inject());
    candidate.window=&window;
    candidate.retiring=true;
    check("retiring owner rejects injection", !inject());
    candidate.retiring=false;
    candidate.ended=true;
    check("ended owner rejects injection", !inject());
    selectedOwner=nullptr;
    check("unreceivable injections not delivered", delivered==4);
    const int before=delivered;
    key.key_code=SAPP_KEYCODE_INVALID;
    check("invalid key rejected", !injectWindowEvent(window, &key, false, "mcp", nullptr, nullptr));
    key.type=SAPP_EVENTTYPE_MOUSE_MOVE; key.mouse_x=std::numeric_limits<float>::infinity();
    check("invalid position rejected", !injectWindowEvent(window, &key, false, "mcp", nullptr, nullptr));
    check("null event rejected", !injectWindowEvent(window, nullptr, false, "mcp", nullptr, nullptr));
    window.binding.bound=false;
    check("closed Window rejects retained operations", !inject());
    window.binding.bound=true;
    window.binding.handle={99}; check("foreign native ID rejected", !inject()); window.binding.handle={};
    _sapp_tc.valid=false; check("invalid native state rejected", !inject()); _sapp_tc.valid=true;
    _sapp_tc_call_cleanup();
    stopping=false;
    check("completed native cleanup rejects injection", !inject());
    check("rejected events never delivered", delivered==before);
}
}
'''
    parts.append(code.replace('_SOKOL_PRIVATE ', ''))
code = prefix + ''.join(parts) + r'''
int main() {
    ios::run(); android::run(); web::run();
    std::printf("single-input: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-single-input-') as directory:
    cpp = Path(directory) / 'probe.cpp'
    binary = Path(directory) / 'probe'
    cpp.write_text(code, encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    '-I' + str(ROOT / 'core/include/sokol'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
