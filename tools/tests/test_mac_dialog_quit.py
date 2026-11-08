#!/usr/bin/env python3
"""Check mac async sources and run real completion/quit code with native seams."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]
mac = (ROOT / 'core/platform/mac/tcFileDialog_mac.mm').read_text(encoding='utf-8')
runtime = (ROOT / 'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
native = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
native = native[:native.index('bool sapp_isvalid(void) {')]
platform = (ROOT / 'core/platform/mac/tcPlatform_mac.mm').read_text(encoding='utf-8')
assert '[_sapp_tc_application sharedApplication]' in body(native, 'void sapp_run(const sapp_desc* desc)')
assert '[NSApplication sharedApplication]' not in body(native, 'void sapp_run(const sapp_desc* desc)')
assert '@interface _sapp_tc_application : NSApplication' in native
launch = body(native, '- (void)applicationWillFinishLaunching:')
assert 'if ([NSApp isKindOfClass:[_sapp_tc_application class]])' in launch
assert '[[NSAppleEventManager sharedAppleEventManager] setEventHandler:self' in launch
assert 'andSelector:@selector(handleQuitAppleEvent:withReplyEvent:)' in launch
assert 'forEventClass:kCoreEventClass andEventID:kAEQuitApplication' in launch
quit_event = body(native, '- (void)handleQuitAppleEvent:')
assert '[NSApp terminate:nil]' in quit_event
assert quit_event.index('[NSApp terminate:nil]') < quit_event.index('setParamDescriptor:')
assert 'if (reply.descriptorType != typeNull)' in quit_event
assert 'descriptorWithInt32:userCanceledErr' in quit_event and 'forKeyword:keyErrorNumber' in quit_event
assert 'dispatch_async' not in quit_event and 'super' not in quit_event
assert 'currentAppleEvent' in body(native, 'static AEEventID _sapp_tc_mac_quit_reason(')
terminate = body(native, '- (void)terminate:(id)sender')
assert terminate.index('applicationShouldTerminate:self') < terminate.index('[super terminate:sender]')
delegate = body(native, '- (NSApplicationTerminateReply)applicationShouldTerminate:')
assert delegate.index('_sapp_tc_request_exit()') < delegate.index('trussc::internal::shutdownRuntime()')
assert 'if (!_sapp_tc.app.quit_ordered)' in body(native, 'static BOOL _sapp_tc_request_exit(')
assert '[NSApp terminate:nil]' in body(native, 'static void _sapp_tc_mac_process_deferred(')
assert '_sapp_tc_mac_process_deferred()' in body(native, 'static void _sapp_tc_mac_source_tick(')
assert 'sapp_request_quit()' in body(platform, '- (void)tcQuit:(id)sender')
assert 'sapp_request_quit()' in body(runtime, 'void processMaintenance()')
# Only Objective-C messaging is replaced. Execute the production preflight
# against the existing real Runtime cancellation probe below.
terminate = terminate.replace('- (void)terminate:(id)sender', 'void terminate(int sender)').replace(
    '[self.delegate applicationShouldTerminate:self]', 'delegateQuery()').replace(
    '[super terminate:sender]', 'appKitTerminate(sender)')
helpers = mac[mac.index('namespace {'):mac.index('namespace {\nNSWindow* visibleDialogOwner')]
# Replace only AppKit dismissal; all list/once-only/quit logic is production code.
native_start = helpers.index('    if (pending->sheet.sheetParent)')
native_end = helpers.index('    if (completion) completion(response);', native_start)
helpers = helpers[:native_start] + '    endSheet(pending->sheet, response);\n' + helpers[native_end:]
helpers = helpers.replace('    NSAlert* alert = nil;\n', '').replace(
    '    TCAsyncDialogButtonTarget* buttonTarget = nil;\n', '')
common = (ROOT / 'core/include/tc/utils/tcAsyncDialog.cpp').read_text(encoding='utf-8')
# Exercise the real common completion, including its macOS EXIT_PROTOCOL branch.
reply = body(common, '[request](FileDialogResult result) {')
for kind in ('alert', 'confirm', 'load', 'save'):
    async_body = body(mac, 'void ' + kind + 'DialogAsync(')
    assert 'internal::requestAsyncDialog(' in async_body
    assert 'runModal' not in async_body and 'appOwnedCallback' not in async_body
    assert 'runModal' in body(mac, ('bool ' if kind == 'confirm' else
        'void ' if kind == 'alert' else 'FileDialogResult ') + kind + 'Dialog(')
presenter = body(mac, 'internal::DialogDismiss presentDialog(')
assert 'runModal' not in presenter and 'runModal' not in helpers
assert 'beginWithCompletionHandler:handler' in presenter
assert '[owner beginSheet:alert.window' in presenter
assert 'beginSheetModalForWindow:owner' in presenter
assert 'makeKeyAndOrderFront:nil' in presenter
assert 'beginSheetModalForWindow' not in presenter.split('} else {\n            const bool save')[0]
owner = body(mac, 'NSWindow* visibleDialogOwner(')
assert owner.index('inputs.requestingWindow.lock()') < owner.index('openWindows()')
assert 'internal::openWindows()' in owner
assert 'window->isOpen()' in owner and 'native.isVisible && !native.isMiniaturized' in owner
assert 'keyWindow' not in owner
assert 'pending->completion = {};' in presenter

code = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "tc/utils/tcTeardown.h"
#define __APPLE__ 1
#define TARGET_OS_OSX 1
using NSModalResponse = int;
constexpr int NSModalResponseCancel = -1;
struct NSWindow {
    bool attached = true;
    int ends = 0;
    std::function<void(int)> nativeReply;
    std::function<void()> duringClose;
};
std::vector<int> trace;
int failures = 0, reports = 0;
void check(const char* label, bool ok) {
    std::printf("sheetquit: %s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}
void endSheet(NSWindow* sheet, int response) {
    check("native dismissal uses cancel", response == NSModalResponseCancel);
    sheet->attached = false; ++sheet->ends; trace.push_back(1);
    if (sheet->duringClose) sheet->duringClose();
    if (sheet->nativeReply) sheet->nativeReply(response);
}
namespace trussc {
'''+helpers+r'''
namespace internal {
struct FileDialogResult { bool success = false; };
struct Request {
    bool finished = false, valid = true;
    std::shared_ptr<void> cancellation;
    std::function<void(FileDialogResult)> callback;
    std::function<void()> dismiss;
    bool alive() const { return valid && !finished; }
};
std::vector<std::function<void()>> mailbox;
void enqueueResourceLoad(std::function<void()> work) { mailbox.push_back(std::move(work)); }
void prelude() {
    auto batch = std::exchange(mailbox, {});
    for (auto& work : batch) work();
}
auto commonReply(std::function<void(FileDialogResult)> callback) {
    auto request = std::make_shared<Request>(); request->callback = std::move(callback);
    return '''+reply+r''';
}

void reportTeardownFailure(const char*) noexcept { ++reports; }
struct App {};
struct Window {};
struct Context { struct { void reset() {} } rootNode; };
struct Owner { Window* window = nullptr; bool ended = false; Context* context = nullptr; };
std::shared_ptr<Owner> appOwner(App&) { return {}; }
Context* nonWindowContextIfInitialized() { return nullptr; }
void endAppAttachment(std::shared_ptr<App>, Context&) {}
struct Ledger {
    struct Entry { std::shared_ptr<App> app; Entry* previous = nullptr; };
    void appendAll(Ledger&) {} Entry* last() { return nullptr; }
    Entry* first() { return nullptr; } bool empty() { return true; }
    std::unique_ptr<int> extract(Entry*) { return {}; }
};
struct Runtime {
    bool stopping = false, quitPending = false, sessionCommitted = false;
    std::vector<std::shared_ptr<Window>> windows, endingWindows;
    Ledger attachments, endingAttachments;
    std::vector<std::weak_ptr<Owner>> owners;
    std::shared_ptr<std::atomic<bool>> runToken = std::make_shared<std::atomic<bool>>(true);
} state;
auto& runtime() { return state; }
struct WindowRequestAccess {
    static void endForShutdown(Window&) {}
    static void teardown(Window&) { trace.push_back(3); }
};
void retireAppOwner(std::shared_ptr<Owner>) {}
bool sg_isvalid() { return false; }
void sapp_tc_gl_activate_idle() {}
void cancelLogoutWatchdog() {}
void cancelAsyncDialogs() {}
int orders = 0, cancels = 0;
void sapp_quit() { ++orders; }
void sapp_cancel_quit() { ++cancels; }
bool allow = false;
bool dispatchExitRequested() { return allow; }
namespace headless {
    bool isActive() { return false; } bool running = false;
    std::atomic<bool> quitRequested{false};
}
'''+body(runtime, 'void shutdownRuntime() noexcept {')+body(runtime, 'bool processPendingQuit() noexcept {')+r'''
} // namespace internal
} // namespace trussc
namespace native {
constexpr int NSTerminateNow = 1;
bool session = false;
int queries = 0, appKitCalls = 0;
bool _sapp_tc_mac_is_session_end(int) { return session; }
int _sapp_tc_mac_quit_reason() { return 0; }
int delegateQuery() {
    ++queries;
    // The native delegate's acceptance/teardown boundary uses this same
    // Runtime veto and shutdown; leave the native message itself as a seam.
    trussc::internal::state.quitPending = true;
    return trussc::internal::processPendingQuit() ? NSTerminateNow : 0;
}
void appKitTerminate(int) {
    ++appKitCalls;
    if (!session) {
        check("AppKit process exit waits for outer cancellation and cleanup",
              !trussc::internal::state.runToken->load());
        trace.push_back(4);
        check("AppKit cannot touch tracked dialogs", trussc::dialogSheets().empty());
        delegateQuery(); // AppKit asks again after the preflight accepted.
    }
}
'''+terminate+r'''
} // namespace native
int main() {
    using namespace trussc;
    using namespace trussc::internal;
    // Normal dismissal unregisters the sheet and preserves its result.
    NSWindow normal;
    int normalCalls = 0;
    auto normalReply = trackDialogSheet(&normal, [&](int response) {
        ++normalCalls; check("ordinary completion preserves native result", response == 42);
    });
    completeDialogSheet(normalReply, 42);
    cancelMacDialogSheets();
    check("dismissed sheet is no longer pending", normalCalls == 1 && normal.ends == 0);

    int deferredCalls = 0;
    auto ordinary = commonReply([&](FileDialogResult result) {
        ++deferredCalls; check("ordinary common result preserved", result.success);
    });
    ordinary({true});
    check("ordinary completion waits for prelude", deferredCalls == 0);
    prelude();
    check("ordinary completion delivered once", deferredCalls == 1);

    NSWindow dropped;
    int droppedCalls = 0;
    auto dropping = trackDialogSheet(&dropped, [&](int) { ++droppedCalls; });
    dropping->completion = {};
    closeDialogSheet(dropping, NSModalResponseCancel);
    completeDialogSheet(dropping, 42);
    check("owner dismissal drops native and late callbacks", droppedCalls == 0 && dropped.ends == 1);
    trace.clear();

    NSWindow alert, confirm, load, save;
    std::vector<std::function<void(int)>> lateReplies;
    int callbacks = 0;
    for (NSWindow* sheet : {&alert, &confirm, &load, &save}) {
        auto deliver = commonReply([&, sheet](FileDialogResult result) {
            ++callbacks; trace.push_back(2);
            check("cancel arrives before owner cleanup", state.runToken->load() && !sheet->attached);
            check("callback receives cancel", !result.success);
        });
        auto pending = trackDialogSheet(sheet, [deliver](int response) {
            deliver({response == 42});
        });
        lateReplies.push_back([pending](int response) { completeDialogSheet(pending, response); });
        if (sheet == &confirm || sheet == &save) sheet->nativeReply = lateReplies.back();
    }
    state.windows.push_back(std::make_shared<Window>());
    state.quitPending = true;
    processPendingQuit();
    check("veto preserves every sheet and callback", callbacks == 0 && dialogSheets().size() == 4 &&
          alert.attached && confirm.attached && load.attached && save.attached && cancels == 1);
    allow = true; state.quitPending = true;
    processPendingQuit();
    check("accepted quit dismisses all four sheets once", callbacks == 4 && orders == 1 &&
          alert.ends == 1 && confirm.ends == 1 && load.ends == 1 && save.ends == 1);
    check("callbacks precede Window teardown", trace == std::vector<int>({1,2,1,2,1,2,1,2,3}));
    for (auto& reply : lateReplies) reply(42);
    shutdownRuntime(); prelude();
    check("late AppKit replies and repeated shutdown do not redeliver", callbacks == 4 && dialogSheets().empty());

    if (failures) return 1;

    // Idle session commit enters this same common ending, without a veto.
    state = Runtime{}; state.sessionCommitted = true;
    NSWindow session;
    int sessionCalls = 0;
    auto sessionReply = trackDialogSheet(&session, [&](int) { ++sessionCalls; });
    shutdownRuntime(); completeDialogSheet(sessionReply, 42);
    check("idle session common ending cancels its sheet once", sessionCalls == 1 && session.ends == 1);

    completeDialogSheet(normalReply, 42);
    check("duplicate native reply is ignored", normalCalls == 1);
    if (failures) return 1;

    NSWindow throwing, survivor;
    auto throwingReply = trackDialogSheet(&throwing, [](int) { throw std::runtime_error("callback"); });
    int survivorCalls = 0;
    auto survivorReply = trackDialogSheet(&survivor, [&](int) { ++survivorCalls; });
    cancelMacDialogSheets(); completeDialogSheet(throwingReply, 42); completeDialogSheet(survivorReply, 42);
    check("throwing callback cannot block other cancellations", reports == 1 && survivorCalls == 1);

    state = Runtime{}; allow = false;
    NSWindow routedAlert;
    int routedCalls = 0;
    auto routedReply = commonReply([&](FileDialogResult result) {
        ++routedCalls; trace.push_back(2);
        check("routed quit cancels before teardown", !result.success && state.runToken->load());
    });
    trackDialogSheet(&routedAlert, [routedReply](int response) { routedReply({response == 42}); });
    state.windows.push_back(std::make_shared<Window>());
    routedAlert.duringClose = [&] {
        trace.push_back(5);
        const int queries = native::queries, appKitCalls = native::appKitCalls;
        // Model repeated source ticks and a direct native quit while AppKit
        // spins the sheet-close loop, before the outer cancel is delivered.
        native::terminate(0);
        native::terminate(0);
        native::session = true;
        native::terminate(0);
        native::session = false;
        check("nested terminate never queries delegate or enters AppKit",
              native::queries == queries && native::appKitCalls == appKitCalls);
        check("nested terminate returns before outer cancel and cleanup",
              routedCalls == 0 && state.runToken->load());
        trace.push_back(6);
    };
    native::terminate(0);
    check("native veto never enters AppKit termination", native::queries == 1 &&
          native::appKitCalls == 0 && routedCalls == 0 && routedAlert.attached);
    allow = true;
    trace.clear();
    native::terminate(0);
    check("accepted native quit cancels once before AppKit", routedCalls == 1 &&
          routedAlert.ends == 1 && native::appKitCalls == 1 && native::queries == 3);
    check("outer quit finishes cancel and cleanup before AppKit termination",
          trace == std::vector<int>({1,5,6,2,3,4}));
    native::session = true;
    native::terminate(0);
    check("session termination retains its separate policy", native::appKitCalls == 2 && native::queries == 3);
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-sheetquit-') as folder:
    source, binary = Path(folder) / 'probe.cpp', Path(folder) / 'probe'
    variants = [('baseline', code),
                ('missing-cancel', code.replace('    cancelMacDialogSheets();\n#endif', '\n#endif')),
                ('duplicate-reply', code.replace('std::exchange(sheet->completion, {})', 'sheet->completion').replace(
                     'std::exchange(pending->completion, {})', 'pending->completion')),
                ('deferred-quit', code.replace('if (completingMacDialogsForQuit()) { deliver(); return; }', '')),
                ('missing-preflight', code.replace('if (delegateQuery() != NSTerminateNow) return;', '')),
                ('late-preflight', code.replace('if (delegateQuery() != NSTerminateNow) return;',
                     'appKitTerminate(sender); if (delegateQuery() != NSTerminateNow) return;')),
                ('missing-terminate-guard', code.replace('if (terminating) return;', ''))]
    for name, text in variants:
        source.write_text(text, encoding='utf-8')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        '-I'+str(ROOT / 'core/include'), str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8", timeout=15)
        print(name, result.stdout, result.stderr, flush=True)
        expected = result.returncode == 0 if name == 'baseline' else result.returncode == 1 and ': FAIL' in result.stdout
        if name == 'missing-terminate-guard':
            expected = expected and 'AppKit process exit waits for outer cancellation and cleanup: FAIL' in result.stdout
        if not expected:
            raise SystemExit(f'{name}: unexpected exit {result.returncode}')

    if clang := shutil.which('clang++'):
        # SDK-free syntax of the subclass and quit-event wiring. These stand-ins
        # check Objective-C++ syntax, not SDK availability or AppKit behavior.
        objc = r'''
#define nil ((id)0)
__attribute__((objc_root_class)) @interface NSObject
+ (Class)class;
- (bool)isKindOfClass:(Class)cls;
@end
using NSApplicationTerminateReply = unsigned long;
constexpr NSApplicationTerminateReply NSTerminateNow = 1;
@class NSApplication;
@protocol NSApplicationDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender;
@end
@interface NSApplication : NSObject
@property id<NSApplicationDelegate> delegate;
- (void)terminate:(id)sender;
@end
bool _sapp_tc_mac_is_session_end(int);
int _sapp_tc_mac_quit_reason();
'''+native[native.index('@interface _sapp_tc_application :'):native.index('@interface _sapp_tc_app_delegate :')]
        objc += r'''
using AEEventClass = unsigned int;
using AEEventID = unsigned int;
using AEKeyword = unsigned int;
using DescType = unsigned int;
using SInt32 = int;
constexpr AEEventClass kCoreEventClass = 'aevt';
constexpr AEEventID kAEQuitApplication = 'quit';
constexpr AEKeyword keyErrorNumber = 'errn';
constexpr DescType typeNull = 'null';
constexpr int userCanceledErr = -128;
@class NSNotification;
extern NSApplication* NSApp;
@interface NSAppleEventDescriptor : NSObject
@property (readonly) DescType descriptorType;
+ (NSAppleEventDescriptor*)descriptorWithInt32:(SInt32)value;
- (void)setParamDescriptor:(NSAppleEventDescriptor*)descriptor forKeyword:(AEKeyword)keyword;
@end
@interface NSAppleEventManager : NSObject
+ (NSAppleEventManager*)sharedAppleEventManager;
- (void)setEventHandler:(id)handler andSelector:(SEL)selector
    forEventClass:(AEEventClass)eventClass andEventID:(AEEventID)eventID;
@end
@interface _sapp_tc_app_delegate : NSObject @end
@implementation _sapp_tc_app_delegate
'''+launch+quit_event+'\n@end\n'
        source = Path(folder) / 'preflight.mm'
        source.write_text(objc, encoding='utf-8')
        subprocess.run([clang, '-std=c++20', '-fsyntax-only', '-fobjc-arc',
                        '-fobjc-runtime=macosx-10.13', '-Werror', str(source)], check=True)

print("mac async source/quit probe: PASS (six mutants rejected)")
