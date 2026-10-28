// Native Windows regression: real HWND/D3D11 resources, with fault/visibility
// injection at the Win32 and COM boundaries. No synthetic OS input.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_3.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <new>
#include <stdexcept>
#include <string>
#include <initializer_list>

namespace {
bool failAllocation, failTitle, failHwnd, destroyOnShow;
int failClientAt, clientCalls, liveHwnds;
BOOL WINAPI testShowWindow(HWND, int);
bool fakeCounter;
LONGLONG fakeCounterValue;
BOOL WINAPI testCounter(LARGE_INTEGER* value) {
    if (!fakeCounter) return QueryPerformanceCounter(value);
    value->QuadPart = fakeCounterValue;
    return TRUE;
}
void* testCalloc(size_t n, size_t size) { return failTitle ? nullptr : std::calloc(n, size); }
BOOL WINAPI testClientRect(HWND hwnd, LPRECT rect) {
    ++clientCalls;
    if (failClientAt && clientCalls == failClientAt) return FALSE;
    return GetClientRect(hwnd, rect);
}
HWND WINAPI testCreateWindow(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style,
        int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data) {
    if (failHwnd) return nullptr;
    HWND hwnd = CreateWindowExW(ex, cls, title, style, x, y, width, height, parent, menu, instance, data);
    if (hwnd) ++liveHwnds;
    return hwnd;
}
BOOL WINAPI testDestroyWindow(HWND hwnd) {
    const BOOL result = DestroyWindow(hwnd);
    if (result) --liveHwnds;
    return result;
}
}
void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return failAllocation ? nullptr : std::malloc(size);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

#define calloc testCalloc
#define GetClientRect testClientRect
#define CreateWindowExW testCreateWindow
#define DestroyWindow testDestroyWindow
#define ShowWindow testShowWindow
#define QueryPerformanceCounter testCounter
#define SOKOL_NO_ENTRY
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#undef calloc
#undef GetClientRect
#undef CreateWindowExW
#undef DestroyWindow
#undef ShowWindow
#undef QueryPerformanceCounter

namespace trussc::internal {
int& appExitCode() { static int value; return value; }
bool& appSetupCalled() { static bool value = true; return value; }
void setExitReason(const char*) {}
void setExitReasonIfEmpty(const char*) {}
void clearExitReason() {}
void setExitBlockReason(const std::string&) {}
bool throwReason;
std::string exitBlockReason() {
    if (throwReason) throw std::bad_alloc();
    return "nativeWindow session veto";
}
}

namespace {
int failures, ticks, events[64], closed, quitRequests, cleanupCalls;
int eventSerial, resumedAt, resizedAt;
bool recordVisibility;
sapp_event_type visibilityOrder[16];
int visibilityCount;
bool sessionEnd;
bool noHighDpi, pixelSizes, launchCovered, runLaunch, initQuit;
int legacyFrames;
uint64_t cleanupFrames;
uint32_t lastLaunchId;
int launchPolicy;
_sapp_tc_window_t* first;
sapp_window second, third;
double started;
int loopStage;
unsigned loopTicks[3];
bool vetoQuit, throwQuit;
void check(const char* label, bool passed) {
    std::printf("nativeWindow/Win32: %s: %s\n", label, passed ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!passed) ++failures;
}
void event(const sapp_event* ev, sapp_window win, void*) {
    const auto* w = _sapp_tc_lookup(win);
    check("event callback holds the native reentry guard", w && w->in_tick);
    if (ev->type < 64) ++events[ev->type];
    if (recordVisibility && (ev->type == SAPP_EVENTTYPE_ICONIFIED || ev->type == SAPP_EVENTTYPE_RESTORED ||
            ev->type == SAPP_EVENTTYPE_SUSPENDED || ev->type == SAPP_EVENTTYPE_RESUMED)) {
        if (visibilityCount < 16) visibilityOrder[visibilityCount] = ev->type;
        ++visibilityCount;
    }
    ++eventSerial;
    if (ev->type == SAPP_EVENTTYPE_RESUMED) resumedAt = eventSerial;
    if (ev->type == SAPP_EVENTTYPE_RESIZED) resizedAt = eventSerial;
    if (ev->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        ++quitRequests;
        if (throwQuit) throw std::runtime_error("native quit callback fault");
        if (vetoQuit) sapp_cancel_quit();
    }
}
void legacyEvent(const sapp_event* ev) { event(ev, {_sapp_tc.app.main->win_id}, nullptr); }
void tick(sapp_window win, void*) {
    ++ticks;
    auto* w = _sapp_tc_lookup(win);
    check("every callback holds the native reentry guard", w && w->in_tick);
    if (!w) return;
    const auto before = w->frame_count;
    _sapp_tc_win32_tick(w);
    check("nested native tick is rejected", w->frame_count == before);
}
void close(sapp_window win, void*) { ++closed; sapp_destroy_window(win); }

// A forwarding COM probe keeps real render/resize resources while counting and
// injecting only Present outcomes; device removal here is deliberately simulated.
struct SwapProbe : IDXGISwapChain1 {
    IDXGISwapChain1* real;
    HRESULT result = S_OK;
    bool inject = false;
    int presents = 0, probes = 0, resizes = 0;
    explicit SwapProbe(IDXGISwapChain1* value) : real(value) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override { return real->QueryInterface(id, out); }
    ULONG STDMETHODCALLTYPE AddRef() override { return real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return real->Release(); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT n, const void* p) override { return real->SetPrivateData(g,n,p); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* p) override { return real->SetPrivateDataInterface(g,p); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* n, void* p) override { return real->GetPrivateData(g,n,p); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id, void** p) override { return real->GetParent(id,p); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** p) override { return real->GetDevice(id,p); }
    HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override {
        if (flags & DXGI_PRESENT_TEST) ++probes; else ++presents;
        return inject ? result : real->Present(interval, flags);
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT n, REFIID id, void** p) override { return real->GetBuffer(n,id,p); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL b, IDXGIOutput* p) override { return real->SetFullscreenState(b,p); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* b, IDXGIOutput** p) override { return real->GetFullscreenState(b,p); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* p) override { return real->GetDesc(p); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) override {
        ++resizes; return real->ResizeBuffers(n,w,h,f,flags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* p) override { return real->ResizeTarget(p); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** p) override { return real->GetContainingOutput(p); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { return real->GetFrameStatistics(p); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* p) override { return real->GetLastPresentCount(p); }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* p) override { return real->GetDesc1(p); }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p) override { return real->GetFullscreenDesc(p); }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* p) override { return real->GetHwnd(p); }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id, void** p) override { return real->GetCoreWindow(id,p); }
    HRESULT STDMETHODCALLTYPE Present1(UINT i, UINT f, const DXGI_PRESENT_PARAMETERS* p) override { return real->Present1(i,f,p); }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return real->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** p) override { return real->GetRestrictToOutput(p); }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* p) override { return real->SetBackgroundColor(p); }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* p) override { return real->GetBackgroundColor(p); }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override { return real->SetRotation(r); }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* r) override { return real->GetRotation(r); }
};

SwapProbe* launchProbe;
BOOL WINAPI testShowWindow(HWND hwnd, int command) {
    auto* w = (_sapp_tc_window_t*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if (destroyOnShow && w) {
        destroyOnShow=false;
        sapp_destroy_window({w->win_id});
        return FALSE;
    }
    if (runLaunch && command == SW_SHOW && w && w->swap_chain && !launchProbe) {
        launchProbe = new SwapProbe(w->swap_chain);
        launchProbe->inject = true;
        launchProbe->result = DXGI_STATUS_OCCLUDED;
        w->occluded = true;
        w->swap_chain = launchProbe;
    }
    return ShowWindow(hwnd,command);
}

void sizeClient(_sapp_tc_window_t* w, int width, int height) {
    RECT rect{0,0,width,height};
    const auto style = DWORD(GetWindowLongPtrW(w->hwnd, GWL_STYLE));
    const auto ex = DWORD(GetWindowLongPtrW(w->hwnd, GWL_EXSTYLE));
    if (_sapp_tc.AdjustWindowRectExForDpi)
        _sapp_tc.AdjustWindowRectExForDpi(&rect, style, FALSE, ex, _sapp_tc.GetDpiForWindow(w->hwnd));
    else AdjustWindowRectEx(&rect, style, FALSE, ex);
    SetWindowPos(w->hwnd,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}
_sapp_tc_native_desc nativeDesc() {
    _sapp_tc_native_desc d{};
    d.window = _sapp_tc_normalize_window(&first->desc);
    d.window.x = 140; d.window.y = 140;
    d.window.width = 101; d.window.height = 77;
    d.window._tc_pixel_width = d.window._tc_pixel_height = 0;
    d.window.tick_cb = tick; d.window.event_cb = event; d.window.close_cb = close;
    d.window.title = "Win32 native probe";
    d.swap_interval = 1;
    d.color_format = SAPP_PIXELFORMAT_BGRA8;
    d.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL;
    return d;
}
UINT WINAPI halfDpi(HWND) { return 48; }
void faults() {
    auto d = nativeDesc();
    const int live = liveHwnds;
    for (int stage = 0; stage != 5; ++stage) {
        failAllocation = stage == 0; failTitle = stage == 1; failHwnd = stage == 2;
        failClientAt = stage == 3 ? (first->window_scale == 1 ? 1 : 2) : 0; clientCalls = 0;
        d.window.sample_count = stage == 4 ? 128 : 1;
        auto* w = _sapp_tc_win32_create_window(&d);
        check("allocation/title/HWND/dimensions/GPU failure rejects construction", w == nullptr);
        if (w) _sapp_tc_win32_unwind(w);
        failAllocation = failTitle = failHwnd = false; failClientAt = 0;
        int count = 0;
        for (auto* entry : _sapp_tc.windows) if (entry) ++count;
        check("every construction failure unwinds HWND and registry", liveHwnds == live && count == 3);
    }
    d.window.sample_count = 1;
    destroyOnShow=true;
    auto* revoked=_sapp_tc_win32_create_window(&d);
    check("synchronous show-time disposal revokes construction safely", !revoked && !destroyOnShow && liveHwnds==live);
    _sapp_tc_win32_unwind(revoked);
    _sapp_tc_window_t placeholder{};
    for (auto& entry : _sapp_tc.windows) if (!entry) entry = &placeholder;
    const auto oldId = _sapp_tc.next_id;
    check("registry exhaustion rejects without an identity", !_sapp_tc_win32_create_window(&d) && _sapp_tc.next_id == oldId);
    for (auto& entry : _sapp_tc.windows) if (entry == &placeholder) entry = nullptr;
    _sapp_tc.next_id = UINT32_MAX - 1;
    auto* last = _sapp_tc_win32_create_window(&d);
    check("last nonzero identity is valid", last && last->win_id == UINT32_MAX);
    auto* wrapped = _sapp_tc_win32_create_window(&d);
    check("identity exhaustion cannot wrap", !wrapped && _sapp_tc.next_id == UINT32_MAX);
    _sapp_tc_win32_unwind(wrapped); _sapp_tc_win32_unwind(last);
    _sapp_tc.next_id = oldId;
    // Real Windows monitor scales are >= 1. Exercise the quotient overflow
    // boundary with an injected DPI sampler, before any resize/GPU allocation.
    const auto savedDpi = _sapp_tc.GetDpiForWindow;
    _sapp_tc.GetDpiForWindow = halfDpi;
    d.window.no_high_dpi = false;
    d.window._tc_pixel_width = INT_MAX; d.window._tc_pixel_height = 77;
    clientCalls = 0; failClientAt = 1;
    auto* invalid = _sapp_tc_win32_create_window(&d);
    check("unrepresentable pixel quotient is rejected before native sizing", !invalid && clientCalls == 0);
    _sapp_tc_win32_unwind(invalid);
    failClientAt = 0; _sapp_tc.GetDpiForWindow = savedDpi;
    d = nativeDesc();
    for (bool missingWidth : {false,true}) for (int missing : {0,-1,INT_MIN}) {
        d.window.width = missingWidth ? missing : 301;
        d.window.height = missingWidth ? 177 : missing;
        auto* w = _sapp_tc_win32_create_window(&d);
        check("main-style defaults preserve the other positive dimension", w &&
            (missingWidth ? w->win_height == 177 : w->win_width == 301));
        _sapp_tc_win32_unwind(w);
    }
}

void titles() {
    const char* borrowed = sapp_query_desc().window_title;
    char longTitle[401]; std::memset(longTitle,'L',400); longTitle[400]=0;
    sapp_set_window_title(longTitle);
    check("legacy title retains stable borrowed 255-byte storage", borrowed == first->title &&
        sapp_query_desc().window_title == borrowed && std::strlen(borrowed) == 255);
    sapp_window_set_title({first->win_id},"Handle first");
    sapp_window_set_title(second,"Handle secondary");
    WCHAR text[512]; GetWindowTextW(first->hwnd,text,512);
    check("positive first handle title targets first without replacing storage", !std::wcscmp(text,L"Handle first") && sapp_query_desc().window_title == borrowed);
    sapp_window_set_title({0},"Zero is not a title handle");
    sapp_window_set_title({UINT32_MAX},"Unknown is not first");
    sapp_window_set_title({first->win_id},longTitle);
    GetWindowTextW(first->hwnd,text,512);
    check("invalid handles and overflowing native title conversion do not change first", !std::wcscmp(text,L"Handle first"));
    char utf8[361];
    for (int i=0;i<120;++i) { utf8[i*3]=char(0xE3); utf8[i*3+1]=char(0x81); utf8[i*3+2]=char(0x82); }
    utf8[360]=0;
    auto d=nativeDesc(); d.window.title=utf8;
    auto* w=_sapp_tc_win32_create_window(&d);
    check("secondary title owns uncapped UTF-8 within native wide conversion bound", w && std::strlen(w->title)==360 && GetWindowTextLengthW(w->hwnd)==120);
    _sapp_tc_win32_unwind(w);
    sapp_set_window_title("Win32 native first");
}

void visibilityTransitions(_sapp_tc_window_t* w, SwapProbe& spy) {
    for (bool trust : {true,false}) {
        w->trust_hidden_signals = trust;
        w->draw_while_hidden = false;
        auto begin = [&] {
            w->iconified = w->occluded = false;
            visibilityCount = 0;
            recordVisibility = true;
        };
        auto expect = [&](const char* label, std::initializer_list<sapp_event_type> order) {
            bool matches = visibilityCount == (trust ? int(order.size()) : 0);
            int i = 0;
            if (trust) for (auto type : order) matches &= i < 16 && visibilityOrder[i++] == type;
            check(label, matches);
        };
        begin();
        SendMessageW(w->hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
        expect("minimize emits ICONIFIED then SUSPENDED only when trusted",
            {SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_SUSPENDED});
        // Keep every observation and notification across the DXGI probe.
        _sapp_tc_win32_observe_occlusion(w, true);
        spy.result = S_OK;
        _sapp_tc_win32_tick(w);
        expect("DXGI clearing occlusion while minimized never emits RESUMED",
            {SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_SUSPENDED});
        check("cleared DXGI observation retains effective minimized visibility",
            !w->occluded && sapp_window_occluded({w->win_id}) == trust);
        SendMessageW(w->hwnd, WM_SIZE, SIZE_RESTORED, 0);
        expect("restore emits RESTORED then one RESUMED only when trusted",
            {SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_RESTORED, SAPP_EVENTTYPE_RESUMED});

        // Exercise both orders of clearing overlapping observations. Reset only
        // before each complete sequence, never between minimize and occlusion.
        for (bool clearCoverFirst : {true,false}) {
            begin();
            _sapp_tc_win32_observe_occlusion(w, true);
            SendMessageW(w->hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
            if (clearCoverFirst) _sapp_tc_win32_observe_occlusion(w, false);
            else SendMessageW(w->hwnd, WM_SIZE, SIZE_RESTORED, 0);
            if (clearCoverFirst)
                expect("overlap stays suspended after only occlusion clears",
                    {SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_ICONIFIED});
            else
                expect("overlap stays suspended after only minimize clears",
                    {SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_RESTORED});
            if (clearCoverFirst) SendMessageW(w->hwnd, WM_SIZE, SIZE_RESTORED, 0);
            else _sapp_tc_win32_observe_occlusion(w, false);
            expect("overlap resumes exactly once after both observations clear",
                {SAPP_EVENTTYPE_SUSPENDED, SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_RESTORED, SAPP_EVENTTYPE_RESUMED});
        }
        recordVisibility = false;
    }
}

bool inFocusHandler, exerciseFocus;
int focusTicks, focusCloseCalls, focusTimers;
void callbackReentry() {
    auto d = nativeDesc();
    d.window.tick_cb = [](sapp_window, void*) {
        if (inFocusHandler) ++focusTicks;
        if (_sapp_tc_now() - started > 2) {
            check("event deferred-close native boundary completed before timeout", false);
            PostQuitMessage(0);
        }
    };
    d.window.close_cb = [](sapp_window, void*) {
        ++focusCloseCalls;
        check("nested event close runs only after the handler returns", !inFocusHandler);
        if (!inFocusHandler) PostQuitMessage(0);
    };
    d.window.event_cb = [](const sapp_event* ev, sapp_window win, void*) {
        event(ev, win, nullptr);
        if (ev->type != SAPP_EVENTTYPE_FOCUSED || !exerciseFocus) return;
        exerciseFocus = false;
        auto* w = _sapp_tc_lookup(win);
        inFocusHandler = true;
        PostMessageW(w->hwnd, WM_CLOSE, 0, 0);
        const ULONGLONG deadline = GetTickCount64() + 120;
        do {
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_TIMER && msg.hwnd == w->hwnd && msg.wParam == _SAPP_TC_MODAL_TIMER) {
                    ++focusTimers;
                    w->credit_held = true;
                    w->earliest_next = 0;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } while (GetTickCount64() < deadline);
        check("FOCUSED modal loop dispatches active size/move timer", focusTimers >= 2);
        check("FOCUSED modal loop permits zero same-window ticks", focusTicks == 0);
        check("FOCUSED nested WM_CLOSE is deferred", w->close_requested && focusCloseCalls == 0);
        inFocusHandler = false;
    };
    auto* w = _sapp_tc_win32_create_window(&d);
    check("event reentry test window constructed", w != nullptr);
    if (!w) return;
    SendMessageW(w->hwnd, WM_ENTERSIZEMOVE, 0, 0);
    exerciseFocus = true;
    SendMessageW(w->hwnd, WM_SETFOCUS, 0, 0);
    SendMessageW(w->hwnd, WM_EXITSIZEMOVE, 0, 0);
    check("event guard restores the outer native state", !w->in_tick);
    // The real native loop, rather than a copied deferred-close implementation,
    // must deliver the pending close outside the callback.
    started = _sapp_tc_now();
    _sapp_tc_win32_run_loop();
    check("native boundary processes the event's deferred close exactly once", focusCloseCalls == 1);
    _sapp_tc_win32_unwind(w);
    // Detailed WM_SIZE clients may explicitly revoke their secondary before
    // the aggregate event. Neither event-scope restoration nor WM_SIZE may use it.
    for (bool restore : {false,true}) {
        d = nativeDesc();
        d.window.event_cb = [](const sapp_event* ev, sapp_window win, void*) {
            event(ev, win, nullptr);
            if (ev->type == SAPP_EVENTTYPE_ICONIFIED || ev->type == SAPP_EVENTTYPE_RESTORED)
                sapp_destroy_window(win);
        };
        w = _sapp_tc_win32_create_window(&d);
        check("WM_SIZE callback lifetime test window constructed", w != nullptr);
        if (!w) continue;
        const sapp_window win = {w->win_id};
        w->iconified = restore;
        const int aggregates = events[SAPP_EVENTTYPE_SUSPENDED] + events[SAPP_EVENTTYPE_RESUMED];
        SendMessageW(w->hwnd, WM_SIZE, restore ? SIZE_RESTORED : SIZE_MINIMIZED, 0);
        check("detailed WM_SIZE callback may destroy its window before aggregate delivery",
            !sapp_window_valid(win) && events[SAPP_EVENTTYPE_SUSPENDED] + events[SAPP_EVENTTYPE_RESUMED] == aggregates);
    }
}

void policies(_sapp_tc_window_t* w) {
    auto savedTick = w->desc.tick_cb;
    auto savedEvent = w->desc.event_cb;
    auto savedGate = w->events_enabled;
    w->desc.tick_cb = tick; w->desc.event_cb = event; w->events_enabled = nullptr;
    SwapProbe spy(w->swap_chain); w->swap_chain = &spy;
    spy.inject = true;
    visibilityTransitions(w, spy);
    for (bool trust : {true,false}) for (bool draw : {false,true}) for (bool minimized : {false,true}) {
        w->trust_hidden_signals = trust;
        w->draw_while_hidden = draw;
        w->iconified = minimized; w->occluded = !minimized;
        spy.result = DXGI_STATUS_OCCLUDED;
        spy.presents = spy.probes = 0;
        const int before = ticks;
        _sapp_tc_win32_tick(w);
        check("hidden observation query honors trust independently of draw", sapp_window_occluded({w->win_id}) == trust);
        check("hidden drawing admission is per-window policy", spy.presents == int(draw || !trust));
        check("untrusted observations do not probe DXGI", spy.probes == int(trust && !minimized));
        check("hidden update-policy callback still runs once", ticks == before + 1);
        check("no-present tick retains credit and a timer deadline", w->credit_held && w->earliest_next > 0);
        events[SAPP_EVENTTYPE_ICONIFIED] = events[SAPP_EVENTTYPE_RESTORED] = 0;
        w->iconified = false;
        SendMessageW(w->hwnd, WM_SIZE, SIZE_MINIMIZED, 0);
        SendMessageW(w->hwnd, WM_SIZE, SIZE_RESTORED, 0);
        check("WM_SIZE transition events pass through trust", events[SAPP_EVENTTYPE_ICONIFIED] == int(trust) && events[SAPP_EVENTTYPE_RESTORED] == int(trust));
        events[SAPP_EVENTTYPE_SUSPENDED] = events[SAPP_EVENTTYPE_RESUMED] = 0;
        w->occluded = false;
        _sapp_tc_win32_observe_occlusion(w,true); _sapp_tc_win32_observe_occlusion(w,false);
        check("DXGI transition events pass through trust", events[SAPP_EVENTTYPE_SUSPENDED] == int(trust) && events[SAPP_EVENTTYPE_RESUMED] == int(trust));
    }
    w->trust_hidden_signals = false; w->draw_while_hidden = false;
    w->iconified = w->occluded = true;
    spy.inject = false; spy.presents = spy.probes = 0;
    UINT presentsBefore=0, presentsAfter=0;
    const HRESULT countBefore=spy.real->GetLastPresentCount(&presentsBefore);
    _sapp_tc_win32_tick(w);
    const HRESULT countAfter=spy.real->GetLastPresentCount(&presentsAfter);
    check("untrusted stale observations allow addressed real DXGI presentation", spy.presents == 1 && spy.probes == 0 &&
        !w->credit_held && !sapp_window_occluded({w->win_id}) && SUCCEEDED(countBefore) && SUCCEEDED(countAfter) && presentsAfter == presentsBefore+1);
    spy.inject = true;
    w->trust_hidden_signals = true; w->draw_while_hidden = false;
    w->iconified = false; w->occluded = true;
    spy.result = S_OK; spy.presents = 0;
    sizeClient(w, 357, 231);
    resumedAt = resizedAt = 0;
    _sapp_tc_win32_tick(w);
    check("common resume event precedes resize", resumedAt > 0 && resizedAt > resumedAt);
    check("resume probe restores presentation and credit", !w->occluded && spy.presents == 1 && !w->credit_held);
    w->skip_present = true; spy.presents = 0;
    _sapp_tc.app.device_lost_notified = true;
    _sapp_tc_win32_tick(w);
    check("device loss consumes completed tick's skip one-shot", !w->skip_present && spy.presents == 0);
    _sapp_tc.app.device_lost_notified = false;
    // Modal source uses the same tick, leaves resize to the next ordinary tick.
    sizeClient(w, 313, 219);
    spy.resizes = 0;
    SendMessageW(w->hwnd,WM_ENTERSIZEMOVE,0,0);
    w->credit_held=true; w->earliest_next=0;
    SendMessageW(w->hwnd,WM_TIMER,_SAPP_TC_MODAL_TIMER,0);
    SendMessageW(w->hwnd,WM_EXITSIZEMOVE,0,0);
    check("modal timer source defers ResizeBuffers", spy.resizes == 0 && !w->from_modal);
    _sapp_tc_win32_tick(w);
    check("ordinary tick coalesces modal resize", spy.resizes == 1);
    // Advance an injected clock inside the client: no wall-time/speed assertion.
    const double realPeriod=_sapp_tc.qpc_period;
    const auto realTiming=w->timing;
    const double realDuration=w->frame_duration, realPresent=w->last_present;
    fakeCounter=true; fakeCounterValue=100; _sapp_tc.qpc_period=1;
    w->desc.tick_cb=[](sapp_window,void*) { fakeCounterValue+=3; };
    w->skip_present=true;
    _sapp_tc_win32_tick(w);
    check("skipped-present deadline follows completed client work", std::fabs(w->earliest_next-(103+w->refresh_period*w->swap_interval))<1e-9);
    const HANDLE waitable=w->frame_wait; w->frame_wait=nullptr;
    fakeCounterValue=100;
    _sapp_tc_win32_tick(w);
    check("non-waitable present deadline follows completed client work", std::fabs(w->earliest_next-(103+w->refresh_period*w->swap_interval))<1e-9);
    w->frame_wait=waitable;
    fakeCounter=false; _sapp_tc.qpc_period=realPeriod;
    w->timing=realTiming; w->frame_duration=realDuration; w->last_present=realPresent;
    w->desc.tick_cb=tick;
    spy.result = DXGI_ERROR_DEVICE_REMOVED;
    events[SAPP_EVENTTYPE_TC_DEVICE_LOST] = 0;
    _sapp_tc_win32_tick(w);
    check("device loss is process-owned for either presenting window", _sapp_tc.app.device_lost_notified && events[SAPP_EVENTTYPE_TC_DEVICE_LOST] == 1);
    _sapp_tc_win32_tick(w);
    check("device loss notification is delivered once", events[SAPP_EVENTTYPE_TC_DEVICE_LOST] == 1);
    _sapp_tc.app.device_lost_notified = false;
    _sapp_tc.app.device_lost_event_pending = false;
    w->swap_chain = spy.real;
    w->desc.tick_cb = savedTick; w->desc.event_cb = savedEvent; w->events_enabled = savedGate;
}

void initialized() {
    first = _sapp_tc.app.main;
    lastLaunchId = first->win_id;
    if (initQuit) { sapp_skip_present(); sapp_quit(); return; }
    if (sessionEnd) {
        trussc::internal::appExitCode() = 37;
        SendMessageW(first->hwnd, WM_ENDSESSION, TRUE, 0);
        check("forced session termination returned unexpectedly", false);
        return;
    }
    if (launchCovered) {
        check("covered first launch policy comes from zero/+1/-1 caller data", first->draw_while_hidden == (launchPolicy >= 0));
        check("covered first launch initializes once before frame", first->frame_count == 0);
        return;
    }
    check("first is ready with dimensions before init/first frame", first->ready && first->frame_count == 0 && first->fb_width > 0 && first->fb_height > 0);
    check("default hidden-signal trust is enabled", first->trust_hidden_signals);
    auto d = nativeDesc().window;
    d.width = 1001; d.height = 667;
    if (pixelSizes) { d._tc_pixel_width = 1001; d._tc_pixel_height = 667; }
    second = sapp_create_window(&d); d.x = 700; third = sapp_create_window(&d);
    auto* other = _sapp_tc_lookup(second);
    check("one main and two secondaries constructed", other && sapp_window_valid(third) && liveHwnds == 3);
    if (!other || !sapp_window_valid(third)) { sapp_quit(); return; }
    const float expected = noHighDpi ? 1 : float(_sapp_tc.GetDpiForWindow(first->hwnd)) / 96;
    const int width = pixelSizes ? 1001 : int(1001 * expected + .5f);
    const int height = pixelSizes ? 667 : int(667 * expected + .5f);
    std::printf("nativeWindow/Win32: dpi=%g pixelPerfect=%d noHighDpi=%d main=%dx%d secondary=%dx%d\n", expected,pixelSizes,noHighDpi,first->fb_width,first->fb_height,other->fb_width,other->fb_height);
    check("pre-first-frame dimensions and target-monitor DPI agree", first->dpi_scale == expected && other->dpi_scale == expected &&
        first->fb_width == width && first->fb_height == height && other->fb_width == width && other->fb_height == height &&
        first->win_width > 0 && first->win_height > 0 && first->win_width == other->win_width && first->win_height == other->win_height);
    check("initial drawing is descriptor data for every window", first->draw_while_hidden && !other->draw_while_hidden);
    check("formats remain RGB10A2 first and BGRA8 secondary", first->color_format == SAPP_PIXELFORMAT_RGB10A2 && other->color_format == SAPP_PIXELFORMAT_BGRA8);
    started = _sapp_tc_now();
}

void nativeChecks() {
    auto* other = _sapp_tc_lookup(second);
    faults();
    titles();
    const auto consoleDesc = _sapp_tc.app.desc.win32;
    const bool allocatedConsole = AllocConsole() != FALSE;
    if (allocatedConsole) ShowWindow(GetConsoleWindow(), SW_HIDE);
    const UINT oldInput = GetConsoleCP(), oldOutput = GetConsoleOutputCP();
    _sapp_tc.app.desc.win32.console_utf8 = true;
    _sapp_tc_win32_init_console();
    check("console UTF-8 setup retains original codepages", GetConsoleCP() == CP_UTF8 && GetConsoleOutputCP() == CP_UTF8);
    _sapp_tc_win32_restore_console();
    check("console restoration retains original input/output codepages", GetConsoleCP() == oldInput && GetConsoleOutputCP() == oldOutput);
    _sapp_tc.app.desc.win32 = consoleDesc;
    if (allocatedConsole) FreeConsole();
    policies(first); policies(other);
    callbackReentry();
    // Zero is an alias only for the explicitly shared seams.
    SwapProbe a(first->swap_chain), b(other->swap_chain);
    a.inject = b.inject = true;
    first->swap_chain = &a; other->swap_chain = &b;
    auto saved = first->desc.tick_cb; first->desc.tick_cb = tick;
    sapp_tc_window_skip_present({0}); _sapp_tc_win32_tick(other);
    check("zero skip addresses first while secondary still presents", first->skip_present && b.presents == 1);
    _sapp_tc_win32_tick(first);
    check("zero skip suppresses only first presentation", a.presents == 0 && !first->skip_present);
    sapp_tc_window_skip_present(second); _sapp_tc_win32_tick(other);
    check("secondary skip is independent of first", !first->skip_present && !other->skip_present && b.presents == 1);
    first->desc.tick_cb = saved; first->swap_chain = a.real; other->swap_chain = b.real;
    // Process-wide fullscreen suppression, regardless of the message recipient.
    sapp_tc_window_set_fullscreen(second,true);
    check("secondary-only fullscreen suppresses screensaver on every HWND", _sapp_tc_win32_any_fullscreen() &&
        SendMessageW(first->hwnd,WM_SYSCOMMAND,SC_SCREENSAVE,0) == 0 && SendMessageW(other->hwnd,WM_SYSCOMMAND,SC_MONITORPOWER,0) == 0);
    sapp_tc_window_set_fullscreen(second,false);
    check("fullscreen restoration is per-window", !other->fullscreen && !first->fullscreen);
    // Reproduce the outgoing adapter's native style operation without changing
    // or linking that adapter. Its requested fullscreen survives minimize.
    const LONG_PTR windowedStyle=GetWindowLongPtrW(other->hwnd,GWL_STYLE);
    const LONG_PTR windowedEx=GetWindowLongPtrW(other->hwnd,GWL_EXSTYLE);
    RECT windowedRect; GetWindowRect(other->hwnd,&windowedRect);
    MONITORINFO mi{}; mi.cbSize=sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(other->hwnd,MONITOR_DEFAULTTONEAREST),&mi);
    SetWindowLongPtrW(other->hwnd,GWL_STYLE,(windowedStyle & ~WS_OVERLAPPEDWINDOW)|WS_POPUP);
    SetWindowLongPtrW(other->hwnd,GWL_EXSTYLE,windowedEx & ~(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE));
    SetWindowPos(other->hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
        mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOACTIVATE);
    check("outgoing-adapter fullscreen suppresses process power requests", _sapp_tc_win32_any_fullscreen());
    ShowWindow(other->hwnd,SW_MINIMIZE);
    check("outgoing-adapter fullscreen suppression survives minimize", _sapp_tc_win32_any_fullscreen());
    ShowWindow(other->hwnd,SW_RESTORE);
    SetWindowLongPtrW(other->hwnd,GWL_STYLE,windowedStyle);
    SetWindowLongPtrW(other->hwnd,GWL_EXSTYLE,windowedEx);
    SetWindowPos(other->hwnd,nullptr,windowedRect.left,windowedRect.top,
        windowedRect.right-windowedRect.left,windowedRect.bottom-windowedRect.top,SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOZORDER);
    check("restoring outgoing-adapter style clears power suppression", !_sapp_tc_win32_any_fullscreen());
    const int previousQueries=quitRequests;
    check("secondary does not own process session query", SendMessageW(other->hwnd,WM_QUERYENDSESSION,0,0)==TRUE && quitRequests==previousQueries);
    SendMessageW(other->hwnd,WM_ENDSESSION,TRUE,0);
    check("secondary session end cannot terminate the process", !_sapp_tc.app.quit_ordered);
    vetoQuit = true; quitRequests = 0;
    SendMessageW(first->hwnd,WM_QUERYENDSESSION,0,0);
    check("session query preserves close veto", quitRequests == 1 && !_sapp_tc.app.quit_ordered);
    trussc::internal::throwReason = true;
    check("session allocation failure cannot escape wndproc", SendMessageW(first->hwnd,WM_QUERYENDSESSION,0,0) == FALSE);
    trussc::internal::throwReason = false;
    SendMessageW(first->hwnd,WM_ENDSESSION,FALSE,0);
    vetoQuit = false;
    const auto savedClose=other->desc.close_cb;
    other->desc.close_cb=[](sapp_window,void*) { throw std::runtime_error("native close callback fault"); };
    SendMessageW(other->hwnd,WM_CLOSE,0,0);
    check("throwing close client cannot bypass native disposal ownership", IsWindow(other->hwnd) && sapp_window_valid(second));
    other->desc.close_cb=savedClose;
    throwQuit=true;
    SendMessageW(first->hwnd,WM_CLOSE,0,0);
    check("throwing process quit client safely vetoes without a retry loop", !_sapp_tc.app.quit_ordered && !_sapp_tc.app.quit_requested && IsWindow(first->hwnd));
    throwQuit=false;
    // The remaining loop is a real DXGI check, not an injected status test.
    first->occluded = other->occluded = false;
    first->iconified = other->iconified = false;
    first->trust_hidden_signals = other->trust_hidden_signals = true;
    first->draw_while_hidden = true;
    sizeClient(first,501,333); sizeClient(other,401,277);
    SetWindowPos(first->hwnd,nullptr,20,20,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(other->hwnd,HWND_TOP,250,180,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    auto* last = _sapp_tc_lookup(third);
    sizeClient(last,301,211);
    SetWindowPos(last->hwnd,HWND_TOP,500,250,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    first->desc.tick_cb = [](sapp_window,void*) { ++loopTicks[0]; };
    other->desc.tick_cb = [](sapp_window,void*) { ++loopTicks[1]; };
    _sapp_tc_lookup(third)->desc.tick_cb = [](sapp_window,void*) { ++loopTicks[2]; };
    events[SAPP_EVENTTYPE_SUSPENDED] = 0;
    started = _sapp_tc_now();
}
void frame() { ++legacyFrames; if (runLaunch) sapp_quit(); }
void cleanup() {
    ++cleanupCalls;
    cleanupFrames = sapp_frame_count();
    if (sessionEnd) {
        char path[1024];
        std::snprintf(path,sizeof(path),"%s\\native-win32-session.txt",std::getenv("TEMP"));
        if (FILE* file = std::fopen(path,"w")) { std::fprintf(file,"cleanup=%d\n",cleanupCalls); std::fclose(file); }
    }
}
}

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1],"--display")) {
        std::puts("SKIP: nativeWindow --display needs a real Windows desktop/D3D11 device"); return 0;
    }
    for (int i=2;i<argc;++i) {
        if (!std::strcmp(argv[i],"--init-quit")) initQuit = true;
        if (!std::strcmp(argv[i],"--session-end")) sessionEnd = true;
        if (!std::strcmp(argv[i],"--pixels")) pixelSizes = true;
        if (!std::strcmp(argv[i],"--no-high-dpi")) noHighDpi = true;
        if (!std::strcmp(argv[i],"--covered")) {
            if (i + 1 >= argc || (std::strcmp(argv[i+1],"-1") && std::strcmp(argv[i+1],"0") && std::strcmp(argv[i+1],"1"))) {
                std::fputs("nativeWindow/Win32: --covered requires a value of -1, 0, or 1\n", stderr);
                return 1;
            }
            launchCovered = true;
            launchPolicy = std::atoi(argv[++i]);
        }
    }
    sapp_desc d{};
    d.width = 1001; d.height = 667; d.high_dpi = !noHighDpi;
    if (pixelSizes) { d._tc_pixel_width=1001; d._tc_pixel_height=667; }
    d.window_title = "Win32 native first";
    d.init_cb=initialized; d.frame_cb=frame; d.event_cb=legacyEvent; d.cleanup_cb=cleanup;
    d._tc_draw_while_hidden = launchCovered ? launchPolicy : 1;
    if (launchCovered || initQuit) {
        runLaunch = true;
        sapp_run(&d);
        if (initQuit) check("init quit cancels its native tick before frame/count/present", legacyFrames == 0 && cleanupFrames == 0 && launchProbe && launchProbe->presents == 0);
        else check("covered sapp_run presentation honors zero/+1/-1 admission", launchProbe && launchProbe->presents == int(launchPolicy >= 0));
        check("sapp_run cleanup/unwind executes once", cleanupCalls == 1 && liveHwnds == 0);
        delete launchProbe; launchProbe = nullptr;
        if (launchCovered && launchPolicy == 0) {
            const uint32_t previousId = lastLaunchId;
            sapp_run(&d);
            check("repeat sapp_run preserves monotonic creation identities", lastLaunchId > previousId && !sapp_window_valid({previousId}));
            check("repeat sapp_run initializes and cleans up independently", cleanupCalls == 2 && liveHwnds == 0);
            delete launchProbe; launchProbe = nullptr;
        }
        std::printf("nativeWindow/Win32: failures=%d\n",failures);
        return failures ? 1 : 0;
    }
    // Inspect the real launch constructor before its first tick, then exercise
    // ordinary native admission and the real multi-window message loop.
    _sapp_tc.app.desc=d;
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq); _sapp_tc.qpc_period=1.0/double(freq.QuadPart);
    _sapp_tc_win32_init_dpi(); _sapp_tc_init_keytable();
    check("D3D11 process device created", _sapp_tc_d3d11_create_device());
    if (!_sapp_tc.app.device) return 1;
    if (_sapp_tc.app.desc._tc_draw_while_hidden == 0) _sapp_tc.app.desc._tc_draw_while_hidden = 1;
    check("first uses common constructor", _sapp_tc_win32_create_main_window());
    if (!_sapp_tc.app.main) return 1;
    const auto* created = _sapp_tc.app.main;
    const float launchScale = noHighDpi ? 1.f : float(_sapp_tc.GetDpiForWindow(created->hwnd))/96.f;
    check("first dimensions are initialized before any tick", created->frame_count == 0 &&
        created->fb_width == (pixelSizes ? 1001 : int(1001*launchScale+.5f)) &&
        created->fb_height == (pixelSizes ? 667 : int(667*launchScale+.5f)) && created->dpi_scale == launchScale);
    _sapp_tc_win32_tick(_sapp_tc.app.main);
    nativeChecks();
    // Init installed independent loop callbacks; use the real run loop,
    // driven by per-window clients to advance scripted stages.
    first->desc.tick_cb = [](sapp_window,void*) {
        ++loopTicks[0];
        const double elapsed = _sapp_tc_now()-started;
        if (elapsed > 12) { check("scripted native loop completed",false); sapp_quit(); return; }
        auto* other = _sapp_tc_lookup(second);
        if (loopStage == 0 && elapsed > .5) {
            std::printf("nativeWindow/Win32: real cover observations: suspended=%d ticks=%u,%u,%u elapsed=%.3f\n",
                events[SAPP_EVENTTYPE_SUSPENDED], loopTicks[0], loopTicks[1], loopTicks[2], elapsed);
            check("partly covered first and secondary keep drawing without SUSPENDED", events[SAPP_EVENTTYPE_SUSPENDED] == 0 && loopTicks[0]>2 && loopTicks[1]>2);
            check("three real waitable surfaces keep ticking", loopTicks[1]>2 && loopTicks[2]>2);
            ShowWindow(other->hwnd,SW_MINIMIZE); loopStage=1;
        } else if (loopStage == 1 && elapsed > .9) {
            check("real minimize reports hidden", sapp_window_occluded(second));
            ShowWindow(other->hwnd,SW_RESTORE); loopStage=2;
        } else if (loopStage == 2 && elapsed > 1.5) {
            check("real restore resumes addressed presentation", !sapp_window_occluded(second) && other->last_present>started+.9);
            ShowWindow(first->hwnd,SW_MINIMIZE); loopStage=3;
        } else if (loopStage == 3 && elapsed > 1.9) {
            check("real first-window minimize reports hidden", sapp_window_occluded({0}));
            ShowWindow(first->hwnd,SW_RESTORE); loopStage=4;
        } else if (loopStage == 4 && elapsed > 2.5) {
            check("real first-window restore resumes presentation", !sapp_window_occluded({0}) && first->last_present>started+1.9);
            sapp_window_request_close(second); sapp_window_request_close(third); loopStage=5;
        } else if (loopStage == 5 && !sapp_window_valid(second) && !sapp_window_valid(third)) {
            check("two scripted secondary closes use common close", closed==2);
            vetoQuit=true; sapp_window_request_close({first->win_id}); loopStage=6;
        } else if (loopStage == 6 && quitRequests>=3) {
            check("first close reaches process quit veto", !_sapp_tc.app.quit_ordered);
            vetoQuit=false; sapp_destroy_window({first->win_id}); loopStage=7;
        }
    };
    _sapp_tc_win32_run_loop();
    check("script closed first after veto and acceptance", loopStage==7 && _sapp_tc.app.quit_ordered);
    _sapp_tc_win32_cleanup();
    for (int i=_SAPP_TC_MAX_WINDOWS-1;i>=0;--i) _sapp_tc_win32_unwind(_sapp_tc.windows[i]);
    _SAPP_TC_RELEASE(_sapp_tc.app.device_context); _SAPP_TC_RELEASE(_sapp_tc.app.device);
    check("ordinary disposal shares the failure unwinder", liveHwnds==0);
    check("process cleanup runs once", cleanupCalls==1);
    std::printf("nativeWindow/Win32: failures=%d\n",failures);
    return failures ? 1 : 0;
}
