#!/usr/bin/env python3
"""Run the mac sheet completion/Runtime ending code with counted native seams."""
from pathlib import Path
import os
import subprocess
import tempfile
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]
mac = (ROOT / 'core/platform/mac/tcFileDialog_mac.mm').read_text(encoding='utf-8')
runtime = (ROOT / 'core/include/tc/app/tcWindowRuntime.cpp').read_text(encoding='utf-8')
helpers = mac[mac.index('namespace {'):mac.index('// -----------------------------------------------------------------------------', mac.index('namespace {'))]
message = '[pending->sheet.sheetParent endSheet:pending->sheet returnCode:NSModalResponseCancel]'
assert helpers.count(message) == 1
helpers = helpers.replace(message, 'endSheet(pending->sheet, NSModalResponseCancel)')
# All four public async functions must use the same once-only completion path.
assert mac.count('auto completion = trackDialogSheet(') == 4
assert mac.count('completionHandler:^(NSModalResponse response) { completion(response); }') == 4

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
struct NSWindow { bool attached = true; int ends = 0; std::function<void(int)> nativeReply; };
std::vector<int> trace;
int failures = 0, reports = 0;
void check(const char* label, bool ok) {
    std::printf("sheetquit: %s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}
void endSheet(NSWindow* sheet, int response) {
    check("native dismissal uses cancel", response == NSModalResponseCancel);
    sheet->attached = false; ++sheet->ends; trace.push_back(1);
    if (sheet->nativeReply) sheet->nativeReply(response);
}
namespace trussc {
'''+helpers+r'''
namespace internal {
void reportTeardownFailure(const char*) noexcept { ++reports; }
struct App {};
struct Window {};
struct Context { struct { void reset() {} } rootNode; };
struct Owner { Window* window = nullptr; bool ended = false; Context* context = nullptr; };
std::shared_ptr<Owner> appOwner(App&) { return {}; }
Context* nonWindowContextIfInitialized() { return nullptr; }
void endAppAttachment(std::shared_ptr<App>, Context&, bool) {}
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
int main() {
    using namespace trussc;
    using namespace trussc::internal;
    // Normal dismissal unregisters the sheet and preserves its result.
    NSWindow normal;
    int normalCalls = 0;
    auto normalReply = trackDialogSheet(&normal, [&](int response) {
        ++normalCalls; check("ordinary completion preserves native result", response == 42);
    });
    normalReply(42);
    cancelMacDialogSheets();
    check("dismissed sheet is no longer pending", normalCalls == 1 && normal.ends == 0);

    NSWindow alert, confirm, load, save;
    std::vector<std::function<void(int)>> lateReplies;
    int callbacks = 0;
    for (NSWindow* sheet : {&alert, &confirm, &load, &save}) {
        lateReplies.push_back(trackDialogSheet(sheet, [&, sheet](int response) {
            ++callbacks; trace.push_back(2);
            check("cancel arrives before owner cleanup", state.runToken->load() && !sheet->attached);
            check("callback receives cancel", response == NSModalResponseCancel);
        }));
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
    shutdownRuntime();
    check("late AppKit replies and repeated shutdown do not redeliver", callbacks == 4 && dialogSheets().empty());

    if (failures) return 1;

    // Idle session commit enters this same common ending, without a veto.
    state = Runtime{}; state.sessionCommitted = true;
    NSWindow session;
    int sessionCalls = 0;
    auto sessionReply = trackDialogSheet(&session, [&](int) { ++sessionCalls; });
    shutdownRuntime(); sessionReply(42);
    check("idle session common ending cancels its sheet once", sessionCalls == 1 && session.ends == 1);

    NSWindow throwing, survivor;
    auto throwingReply = trackDialogSheet(&throwing, [](int) { throw std::runtime_error("callback"); });
    int survivorCalls = 0;
    auto survivorReply = trackDialogSheet(&survivor, [&](int) { ++survivorCalls; });
    cancelMacDialogSheets(); throwingReply(42); survivorReply(42);
    check("throwing callback cannot block other cancellations", reports == 1 && survivorCalls == 1);
    return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-sheetquit-') as folder:
    source, binary = Path(folder) / 'probe.cpp', Path(folder) / 'probe'
    variants = [('baseline', code),
                ('missing-cancel', code.replace('    cancelMacDialogSheets();\n#endif', '\n#endif')),
                ('duplicate-reply', code.replace('std::exchange(sheet->completion, {})', 'sheet->completion'))]
    for name, text in variants:
        source.write_text(text, encoding='utf-8')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        '-I'+str(ROOT / 'core/include'), str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8", timeout=15)
        print(name, result.stdout, result.stderr, flush=True)
        expected = result.returncode == 0 if name == 'baseline' else result.returncode == 1 and ': FAIL' in result.stdout
        if not expected:
            raise SystemExit(f'{name}: unexpected exit {result.returncode}')
