// Complete macOS native constructor, tick, visibility and close paths with real
// AppKit windows and Metal drawables. Only AppKit observations are injected.
#define SOKOL_NO_ENTRY
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include <objc/runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <initializer_list>
#include <string>
#include <stdexcept>
namespace trussc::internal {
void setExitReasonIfEmpty(const char*) {}
void setExitReason(const char*) {}
}
namespace {
int failures, callbacks, acquired, mainFrames, mainDrawables, showsChecked;
int iconifiedEvents, restoredEvents, suspendedEvents, quitRequests;
bool noHighDpi, coveredFirst, coveredReleased, didInitialize, mainDraws, allowQuit;
bool forceTinyScale, failLayer, secondClosed;
int launchHidden;
uint32_t lastAcquired;
sapp_window second;
id<MTLCommandQueue> queue;
NSTimer* driver;

void check(const char* name, bool passed) {
    std::printf("nativeWindow: %s: %s\n", name, passed ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!passed) ++failures;
}

void present(id<CAMetalDrawable> drawable) {
    if (!drawable) return;
    if (!queue) queue = [_sapp_tc.app.device newCommandQueue];
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> buffer = [queue commandBuffer];
    [[buffer renderCommandEncoderWithDescriptor:pass] endEncoding];
    [buffer presentDrawable:drawable];
    [buffer commit];
}

// Probe client: count ticks and draw only when the native tick admits it.
void probeTick(sapp_window win, void*) {
    ++callbacks;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)sapp_window_metal_current_drawable(win);
    if (drawable) { ++acquired; lastAcquired = win.id; present(drawable); }
}

void secondaryEvent(const sapp_event* event, sapp_window win, void*) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    static bool first = true;
    if (first) {
        first = false;
        check("secondary first event sees published dimensions", w && w->ready &&
            event->framebuffer_width > 0 && event->window_width > 0);
    }
    if (event->type == SAPP_EVENTTYPE_ICONIFIED) ++iconifiedEvents;
    if (event->type == SAPP_EVENTTYPE_RESTORED) ++restoredEvents;
    if (event->type == SAPP_EVENTTYPE_SUSPENDED) ++suspendedEvents;
}

void secondaryClosed(sapp_window win, void*) {
    secondClosed = true;
    sapp_destroy_window(win);
}

float expectedScale(NSWindow* window) {
    return noHighDpi ? 1.0f : (float)window.backingScaleFactor;
}

// AppKit seams: record show-time publication; inject scale, layer and occlusion.
void (*realShow)(id, SEL, id);
void recordShow(id self, SEL cmd, id sender) {
    // Judge the constructor's own show only; AppKit also re-shows windows later.
    if (_sapp_tc.constructing == 0) { realShow(self, cmd, sender); return; }
    _sapp_tc_window_t* found = nullptr;
    for (auto* w : _sapp_tc.windows) if (w && w->window == self) found = w;
    if (!found) std::printf("nativeWindow: show of unregistered window \"%s\"\n", ((NSWindow*)self).title.UTF8String);
    check("published window has initial scale and dimensions before it is shown", found && found->ready &&
        found->fb_width > 0 && found->fb_height > 0 && found->win_width > 0 && found->win_height > 0 &&
        found->dpi_scale == (found->desc.no_high_dpi ? 1.0f : (float)((NSWindow*)self).backingScaleFactor) &&
        found->fb_width == (int)(found->win_width * found->dpi_scale) &&
        found->fb_height == (int)(found->win_height * found->dpi_scale));
    ++showsChecked;
    realShow(self, cmd, sender);
}
CGFloat (*realScale)(id, SEL);
CGFloat injectScale(id self, SEL cmd) { return forceTinyScale ? 1e-8 : realScale(self, cmd); }
CAMetalLayer* (*realLayer)(id, SEL);
CAMetalLayer* injectLayer(id self, SEL cmd) { return failLayer ? nil : realLayer(self, cmd); }
NSWindowOcclusionState (*realOcclusion)(id, SEL);
NSWindow* forceHidden;      // one window AppKit reports as not visible
NSWindow* forceVisible;     // one window AppKit reports as visible
NSWindowOcclusionState injectOcclusion(id self, SEL cmd) {
    if (forceHidden && self == forceHidden) return 0;
    if (forceVisible && self == forceVisible) return NSWindowOcclusionStateVisible;
    return (coveredFirst && !coveredReleased) ? 0 : realOcclusion(self, cmd);
}
template<class F> void swap(Class cls, SEL sel, F replacement, F* original) {
    Method m = class_getInstanceMethod(cls, sel);
    *original = (F)method_getImplementation(m);
    method_setImplementation(m, (IMP)replacement);
}

int liveNativeWindows() {
    int count = 0;
    for (NSWindow* window in NSApp.windows) if ([window.contentView isKindOfClass:[_sapp_tc_view class]]) ++count;
    return count;
}

double now() { return CACurrentMediaTime(); }

// Synchronous checks on the common constructor and tick, outside every tick.
void synchronousChecks() {
    _sapp_tc_window_t* first = _sapp_tc.app.main;
    _sapp_tc_window_t* other = _sapp_tc_lookup(second);
    // Hidden admission x trust x observation, both windows, on the common tick.
    for (auto* target : {first, other}) {
        auto saved = target->desc.tick_cb;
        const bool savedAllow = target->draw_while_hidden;
        target->desc.tick_cb = probeTick;
        for (bool trust : {true, false}) for (bool iconified : {false, true}) for (bool allow : {false, true}) {
            target->trust_hidden_signals = trust;
            target->iconified = iconified; target->occluded = !iconified;
            sapp_tc_window_draw_while_hidden({target->win_id}, allow);
            callbacks = acquired = 0;
            _sapp_tc_mac_tick(target, nil);
            const bool shouldDraw = !trust || allow;
            check("hidden drawing and trust are independent for either window", callbacks == 1 &&
                acquired == int(shouldDraw) && (!acquired || lastAcquired == target->win_id));
            check("all visibility readers use effective state", sapp_window_occluded({target->win_id}) == trust &&
                _sapp_tc_native_iconified(target) == (trust && iconified) &&
                _sapp_tc_native_occluded(target) == (trust && !iconified) &&
                (target != first || sapp_window_occluded({0}) == trust));
            auto* peer = target == first ? other : first;
            check("hidden signal switch is per window", peer->trust_hidden_signals);
        }
        target->trust_hidden_signals = true;
        target->desc.tick_cb = saved;
        sapp_tc_window_draw_while_hidden({target->win_id}, savedAllow);
        _sapp_tc_mac_observe(target);
    }
    check("drawable is not acquirable outside its tick", sapp_window_metal_current_drawable(second) == nullptr &&
        sapp_window_metal_depth_stencil_texture(second) == nullptr);
    // One-shot skip requests belong to the addressed window.
    sapp_skip_present();
    check("first skip is isolated", first->skip_present && !other->skip_present);
    _sapp_tc_mac_tick(other, nil);
    check("other window's tick keeps first skip", first->skip_present);
    _sapp_tc_mac_tick(first, nil);
    check("first tick consumes its skip", !first->skip_present);
    sapp_tc_window_skip_present(second);
    check("secondary skip is isolated", other->skip_present && !first->skip_present);
    _sapp_tc_mac_tick(first, nil);
    check("first tick keeps secondary skip", other->skip_present);
    _sapp_tc_mac_tick(other, nil);
    sapp_tc_window_skip_present({0});
    check("zero skip addresses only the first window", first->skip_present && !other->skip_present);
    _sapp_tc_mac_tick(first, nil);
    // Representability is checked against the target window's backing scale.
    sapp_window_desc desc = other->desc;
    desc.title = "rejected"; desc.close_cb = nullptr; desc.event_cb = nullptr; desc.tick_cb = nullptr;
    desc.no_high_dpi = false; desc._tc_pixel_width = 640; desc._tc_pixel_height = 400;
    const int before = liveNativeWindows();
    @autoreleasepool {
        forceTinyScale = true;
        const sapp_window rejected = sapp_create_window(&desc);
        forceTinyScale = false;
        check("unrepresentable logical size rejected", rejected.id == 0);
        if (rejected.id) sapp_destroy_window(rejected);
        desc._tc_pixel_width = desc._tc_pixel_height = 0;
        failLayer = true;
        check("failed drawable layer rejects creation", sapp_create_window(&desc).id == 0);
        failLayer = false;
    }
    check("failure unwinds native window", liveNativeWindows() == before && sapp_window_valid(second));
    const uint32_t savedNextId = _sapp_tc.next_id;
    _sapp_tc.next_id = UINT32_MAX - 1;
    @autoreleasepool {
        const sapp_window last = sapp_create_window(&desc);
        check("last identity is valid", last.id == UINT32_MAX);
        if (last.id) sapp_destroy_window(last);
        check("identity exhaustion rejects cleanly", sapp_create_window(&desc).id == 0 &&
            _sapp_tc.next_id == UINT32_MAX);
    }
    int records = 0;
    for (auto* w : _sapp_tc.windows) records += w != nullptr;
    check("exhaustion leaves no native record", records == 2);
    // Later probes need identities; values between these were never issued.
    _sapp_tc.next_id = savedNextId;
}

// A client may destroy its window from any event it receives. Every native
// path that sends an event and then continues must re-resolve the window.
sapp_event_type destroyOn;
bool destroyedInCallback;
void victimEvent(const sapp_event* event, sapp_window win, void*) {
    if (event->type == destroyOn && sapp_window_valid(win)) { destroyedInCallback = true; sapp_destroy_window(win); }
}
sapp_window makeVictim() {
    sapp_window_desc d = {};
    d.x = d.y = -1; d.width = 120; d.height = 90; d.title = "victim"; d.no_high_dpi = noHighDpi;
    d.mtl_device = (__bridge const void*)_sapp_tc.app.device;
    d.event_cb = victimEvent;
    return sapp_create_window(&d);
}
void destroyInCallbackChecks() {
    enum Path { LinkTick, FallbackTick, Miniaturize, Deminiaturize, TickResize, KeyDown };
    struct Case { const char* name; Path path; sapp_event_type type; };
    const Case cases[] = {
        {"SUSPENDED from the display-link shim", LinkTick, SAPP_EVENTTYPE_SUSPENDED},
        {"SUSPENDED from the fallback-timer shim", FallbackTick, SAPP_EVENTTYPE_SUSPENDED},
        {"SUSPENDED from windowDidMiniaturize", Miniaturize, SAPP_EVENTTYPE_SUSPENDED},
        {"ICONIFIED from windowDidMiniaturize", Miniaturize, SAPP_EVENTTYPE_ICONIFIED},
        {"RESUMED from windowDidDeminiaturize", Deminiaturize, SAPP_EVENTTYPE_RESUMED},
        {"RESIZED from the tick", TickResize, SAPP_EVENTTYPE_RESIZED},
        {"KEY_DOWN before its CHAR events", KeyDown, SAPP_EVENTTYPE_KEY_DOWN},
    };
    for (const Case& c : cases) {
        @autoreleasepool {
            const sapp_window victim = makeVictim();
            _sapp_tc_window_t* w = _sapp_tc_lookup(victim);
            if (!w) { check("victim created", false); continue; }
            // Strong locals keep the AppKit objects alive across their own method.
            _sapp_tc_view* view = w->view;
            _sapp_tc_win_delegate* delegate = w->delegate;
            NSWindow* window = w->window;
            destroyOn = c.type; destroyedInCallback = false;
            switch (c.path) {
            case LinkTick: forceHidden = window; [view tick:nil]; break;
            case FallbackTick: forceHidden = window; [view fallbackTick:nil]; break;
            case Miniaturize: forceHidden = window; [delegate windowDidMiniaturize:nil]; break;
            case Deminiaturize:
                forceHidden = window; _sapp_tc_mac_observe(w);   // suspended: RESUMED follows
                forceHidden = nil; forceVisible = window; [delegate windowDidDeminiaturize:nil]; break;
            case TickResize: w->fb_width = 1; _sapp_tc_mac_tick(w, nil); break;
            case KeyDown: {
                NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                    timestamp:0 windowNumber:window.windowNumber context:nil characters:@"ab"
                    charactersIgnoringModifiers:@"ab" isARepeat:NO keyCode:0];
                [view keyDown:key];
                break;
            }
            }
            forceHidden = nil; forceVisible = nil;
            check((std::string("destroying the window from ") + c.name + " is safe").c_str(),
                destroyedInCallback && !sapp_window_valid(victim));
            if (sapp_window_valid(victim)) sapp_destroy_window(victim);
        }
    }
    // The constructor's own startup steps notify: showing a window makes it key.
    @autoreleasepool {
        destroyOn = SAPP_EVENTTYPE_FOCUSED; destroyedInCallback = false;
        int before = 0;
        for (auto* r : _sapp_tc.windows) before += r != nullptr;
        const sapp_window victim = makeVictim();
        int after = 0;
        for (auto* r : _sapp_tc.windows) after += r != nullptr;
        std::printf("nativeWindow: destroy on first FOCUSED during creation: destroyed=%d returned id=%u\n",
            destroyedInCallback, victim.id);
        check("destroying the window from its first FOCUSED during creation is safe and returns invalid",
            destroyedInCallback && victim.id == 0 && after == before);
        if (victim.id && sapp_window_valid(victim)) sapp_destroy_window(victim);
    }
}

// Nested run loop inside an in-tick event callback: the window must not run a
// nested frame, and a close requested inside stays deferred until it returns.
int nestTicks, nestNested = -1, nestKind;
bool nestArmed, nestInCallback, nestCloseDuringCallback, nestClosed;
sapp_event_type nestOn;
sapp_window nestWindow;
void nestTick(sapp_window, void*) { ++nestTicks; }
void nestEvent(const sapp_event* event, sapp_window win, void*) {
    if (!nestArmed || event->type != nestOn) return;
    nestArmed = false; nestInCallback = true;
    const int before = nestTicks;
    sapp_window_request_close(win);
    const double until = CACurrentMediaTime() + 0.1;
    while (CACurrentMediaTime() < until) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    nestNested = nestTicks - before;
    nestInCallback = false;
}
void nestClose(sapp_window win, void*) {
    nestClosed = true;
    if (nestInCallback) nestCloseDuringCallback = true;
    sapp_destroy_window(win);
}

// Throwing clients: the native boundary contains the exception and recovers.
int throwTicks, throwEventTicks;
bool throwClosed, eventThrown, mainThrowOnce, mainThrew;
sapp_window throwWindow;
void throwTick(sapp_window, void*) { if (++throwTicks == 3) throw std::runtime_error("tick_cb test throw"); }
void countTick(sapp_window, void*) { ++throwEventTicks; }
bool keyThrown;
void throwEvent(const sapp_event* event, sapp_window, void*) {
    if (event->type == SAPP_EVENTTYPE_RESIZED && !eventThrown) { eventThrown = true; throw 7; }
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && !keyThrown) { keyThrown = true; throw std::runtime_error("key event test throw"); }
}
void throwClose(sapp_window win, void*) {
    throwClosed = true;
    sapp_destroy_window(win);
    throw std::runtime_error("close_cb test throw");
}
sapp_window makeProbe(void (*tick)(sapp_window, void*), void (*event)(const sapp_event*, sapp_window, void*),
                      void (*close)(sapp_window, void*), const char* title) {
    sapp_window_desc d = {};
    d.x = d.y = -1; d.width = 120; d.height = 90; d.title = title; d.no_high_dpi = noHighDpi;
    d.mtl_device = (__bridge const void*)_sapp_tc.app.device;
    d.tick_cb = tick; d.event_cb = event; d.close_cb = close;
    return sapp_create_window(&d);
}
_sapp_tc_window_t* fsCreated;

// A native close (performClose:) or terminate fired from a timer while a client
// callback spins the run loop must wait until that callback returns.
// Kinds 0/1: performClose: (goes through windowShouldClose:). Kinds 2/3: a direct
// -[NSWindow close], which AppKit delivers without windowShouldClose:.
int ccCloses, ccKind, ccTicksAfterNativeClose;
bool ccArmed, ccInCallback, ccClosedInCallback, ccSpinDone, ccNativeClosed;
sapp_window ccWindow;
void spinFor(double seconds) {
    const double until = CACurrentMediaTime() + seconds;
    while (CACurrentMediaTime() < until) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
}
void ccSpinWithClose(sapp_window win) {
    _sapp_tc_window_t* w = _sapp_tc_lookup(win);
    NSWindow* window = w ? w->window : nil;
    ccInCallback = true;
    const bool direct = ccKind >= 2;
    NSTimer* t = [NSTimer timerWithTimeInterval:0.02 repeats:NO block:^(NSTimer*) {
        if (direct) [window close]; else [window performClose:nil];
        ccNativeClosed = true;
    }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    spinFor(0.15);
    ccInCallback = false;
    ccSpinDone = true;
}
void ccEvent(const sapp_event* event, sapp_window win, void*) {
    if (ccArmed && ccKind % 2 == 0 && event->type == SAPP_EVENTTYPE_KEY_DOWN) { ccArmed = false; ccSpinWithClose(win); }
}
void ccTick(sapp_window win, void*) {
    if (ccNativeClosed && ccCloses == 0) ++ccTicksAfterNativeClose;   // closed but not yet torn down
    if (ccArmed && ccKind % 2 == 1) { ccArmed = false; ccSpinWithClose(win); }
}
void ccClose(sapp_window win, void*) {
    ++ccCloses;
    if (ccInCallback) ccClosedInCallback = true;
    sapp_destroy_window(win);
}

// Cross-window cases, from window X's key callback while it spins the run loop:
// kind 4: performClose: of the FIRST window (its vetoable QUIT_REQUESTED must come
// after the callback); kind 5: a direct close of another window Y, which must not
// tick between its native close and its deferred close handling.
int xKind, xQuitBefore, yTicksAfterClose, yCloses;
bool xArmed, xSpinDone, xQuitInside, yNativeClosed, yClosedInside;
sapp_window xWindow, yWindow;
void yTick(sapp_window, void*) { if (yNativeClosed && yCloses == 0) ++yTicksAfterClose; }
void yClose(sapp_window win, void*) {
    ++yCloses;
    if (ccInCallback) yClosedInside = true;
    sapp_destroy_window(win);
}
void xEvent(const sapp_event* event, sapp_window, void*) {
    if (!xArmed || event->type != SAPP_EVENTTYPE_KEY_DOWN) return;
    xArmed = false;
    _sapp_tc_window_t* y = _sapp_tc_lookup(yWindow);
    NSWindow* target = xKind == 4 ? _sapp_tc.app.main->window : (y ? y->window : nil);
    const bool direct = xKind == 5;
    ccInCallback = true;
    NSTimer* t = [NSTimer timerWithTimeInterval:0.02 repeats:NO block:^(NSTimer*) {
        if (direct) { [target close]; yNativeClosed = true; } else [target performClose:nil];
    }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    spinFor(0.3);
    ccInCallback = false;
    xSpinDone = true;
}

// --terminate-in-callback (run as a child process): [NSApp terminate:] fired
// inside the first window's frame callback, vetoed once, then allowed.
bool terminateMode, termInCallback, termSpinReturned, quitInsideCallback, cleanupInsideCallback;
int termRound, cleanups;
void spinWithTerminate() {
    termInCallback = true;
    NSTimer* t = [NSTimer timerWithTimeInterval:0.02 repeats:NO block:^(NSTimer*) { [NSApp terminate:nil]; }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
    spinFor(0.15);
    termInCallback = false;
}
void mainCleanup() {
    ++cleanups;
    if (termInCallback) cleanupInsideCallback = true;
}
// A real synchronous modal (NSAlert runModal) opened from a key handler.
int modalRound, modalOwnerTicks, modalOtherAt, modalOwnerAt, modalOwnerAfterAt;
bool modalActive, modalRequestClose, modalClosed, modalClosedInHandler, modalHandlerDone;
double modalStart, modalEnd, modalResumedAt;
sapp_window modalOwner;
void modalTick(sapp_window, void*) {
    ++modalOwnerTicks;
    if (modalHandlerDone && modalResumedAt == 0) modalResumedAt = CACurrentMediaTime();
}
void modalClose(sapp_window win, void*) {
    modalClosed = true;
    if (modalActive) modalClosedInHandler = true;
    sapp_destroy_window(win);
}
void modalEvent(const sapp_event* event, sapp_window win, void*) {
    if (event->type != SAPP_EVENTTYPE_KEY_DOWN || modalActive) return;
    modalActive = true;
    if (modalRequestClose) sapp_window_request_close(win);
    modalOwnerAt = modalOwnerTicks; modalOtherAt = callbacks; modalStart = CACurrentMediaTime();
    NSAlert* alert = [NSAlert new];
    alert.messageText = @"nativeWindow modal";
    NSTimer* dismiss = [NSTimer timerWithTimeInterval:1.0 repeats:NO block:^(NSTimer*) { [NSApp abortModal]; }];
    [[NSRunLoop mainRunLoop] addTimer:dismiss forMode:NSRunLoopCommonModes];
    [alert runModal];
    modalEnd = CACurrentMediaTime();
    const double seconds = modalEnd - modalStart;
    std::printf("nativeWindow: modal round %d: %.2fs, owner ticked %d, other window ticked %.1f/s\n", modalRound,
        seconds, modalOwnerTicks - modalOwnerAt, (callbacks - modalOtherAt) / seconds);
    check("owner window gets no tick while its key handler is in a modal", modalOwnerTicks == modalOwnerAt);
    check("other windows keep ticking during the modal", (callbacks - modalOtherAt) / seconds > 20);
    modalOwnerAfterAt = modalOwnerTicks;
    modalActive = false;
    modalHandlerDone = true;
}
uint32_t fsCreatedId;

// A secondary closed natively whose close_cb keeps it registered must stop ticking.
int closedTicks[2];
void closedTick(sapp_window, void* user) { ++closedTicks[(intptr_t)user]; }
void keepOnClose(sapp_window, void*) {}
sapp_window closedWindows[2];
int closedTicksAt[2];

enum Stage { Sync, SettleAfterSync, MinimizeTrusted, RestoreTrusted, MinimizeUntrusted, RestoreUntrusted,
             MinimizeMain, RestoreMain, Reverse, ReverseWait, NativeClose, NativeClosed,
             NestSetup, NestWarm, NestWait, FsCreate, FsSettle, ThrowTickStart, ThrowTickWait,
             ThrowEventStart, ThrowEventWait, ThrowLegacy, ThrowLegacyWait, ModalSetup, ModalWarm, ModalAfter,
             CcSetup, CcWarm, CcWait, XSetup, XWarm, XWait, TerminateChild,
             CloseSecondary, VetoQuit, Done };
Stage stage = Sync;
double stageStart, sampleStart;
int ticksAt, acquiredAt, mainFramesAt, mainDrawablesAt, reverseTarget;
bool reverseSawFullscreen;

void enter(Stage next) { stage = next; stageStart = now(); }
bool waited(double seconds) { return now() - stageStart > seconds; }

void step() {
    if (modalActive) return;   // the driver timer also fires inside the modal
    _sapp_tc_window_t* first = _sapp_tc.app.main;
    _sapp_tc_window_t* other = _sapp_tc_lookup(second);
    switch (stage) {
    case Sync:
        synchronousChecks();
        destroyInCallbackChecks();
        // AppKit drops a miniaturize: issued in the same turn as the key-window
        // change of a window destroyed during creation; let focus settle first.
        enter(SettleAfterSync);
        break;
    case SettleAfterSync:
        if (!waited(0.5)) break;
        other->desc.tick_cb = probeTick;
        callbacks = acquired = 0;
        [other->window miniaturize:nil];
        enter(MinimizeTrusted);
        break;
    case MinimizeTrusted:
        if (!other->iconified && !waited(5)) break;
        if (sampleStart == 0) { sampleStart = now(); ticksAt = callbacks; acquiredAt = acquired; break; }
        if (now() - sampleStart < 0.5) break;
        check("trusted minimize is reported for the secondary", other->iconified && sapp_window_occluded(second) &&
            iconifiedEvents == 1);
        check("minimized secondary ticks from its own fallback source", other->fallback_active &&
            callbacks - ticksAt >= 5);
        check("minimized secondary draws nothing (draw_while_hidden false)", acquired == acquiredAt);
        sampleStart = 0;
        [other->window deminiaturize:nil];
        enter(RestoreTrusted);
        break;
    case RestoreTrusted:
        if (other->window.miniaturized && !waited(5)) break;
        if (!waited(0.5)) break;
        check("restore is reported", !other->iconified && restoredEvents == 1);
        other->trust_hidden_signals = false;
        _sapp_tc_mac_observe(other);
        iconifiedEvents = restoredEvents = suspendedEvents = 0;
        [other->window miniaturize:nil];
        enter(MinimizeUntrusted);
        break;
    case MinimizeUntrusted:
        if (!other->window.miniaturized && !waited(5)) break;
        if (sampleStart == 0) { sampleStart = now(); ticksAt = callbacks; acquiredAt = acquired; break; }
        if (now() - sampleStart < 1.5) break;
        std::printf("nativeWindow: untrusted minimized secondary: %d ticks, %d drawables in %.1fs\n",
            callbacks - ticksAt, acquired - acquiredAt, now() - sampleStart);
        check("untrusted minimize reports visible and sends no hidden events", other->window.miniaturized &&
            !sapp_window_occluded(second) && !other->iconified && iconifiedEvents == 0 && suspendedEvents == 0);
        check("untrusted minimized secondary keeps ticking and drawing", other->fallback_active &&
            callbacks > ticksAt && acquired > acquiredAt);
        sampleStart = 0;
        [other->window deminiaturize:nil];
        enter(RestoreUntrusted);
        break;
    case RestoreUntrusted:
        if (other->window.miniaturized && !waited(5)) break;
        if (!waited(0.5)) break;
        check("untrusted restore sends no event", restoredEvents == 0);
        other->trust_hidden_signals = true;
        _sapp_tc_mac_observe(other);
        mainDraws = true;
        [first->window miniaturize:nil];
        enter(MinimizeMain);
        break;
    case MinimizeMain:
        if (!first->iconified && !waited(5)) break;
        if (sampleStart == 0) { sampleStart = now(); mainFramesAt = mainFrames; mainDrawablesAt = mainDrawables; break; }
        if (now() - sampleStart < 1.5) break;
        std::printf("nativeWindow: minimized first window: %d frames, %d drawables in %.1fs\n",
            mainFrames - mainFramesAt, mainDrawables - mainDrawablesAt, now() - sampleStart);
        check("minimized first window keeps its frame callback on its fallback source",
            first->iconified && sapp_window_occluded({0}) && first->fallback_active && mainFrames - mainFramesAt >= 5);
        check("minimized first window keeps drawing (launch draw_while_hidden)", mainDrawables > mainDrawablesAt);
        sampleStart = 0;
        [first->window deminiaturize:nil];
        enter(RestoreMain);
        break;
    case RestoreMain:
        if (first->window.miniaturized && !waited(5)) break;
        if (!waited(0.5)) break;
        mainDraws = false;
        reverseTarget = 0;
        enter(Reverse);
        break;
    case Reverse: {
        // Reverse an entering transition, then stop the OTHER window's ticks:
        // only the target's own tick may reconcile it.
        _sapp_tc_window_t* target = reverseTarget == 0 ? first : other;
        _sapp_tc_window_t* peer = target == first ? other : first;
        if (reverseTarget == 0) {
            for (NSWindow* win in NSApp.windows)
                if ([win.contentView isKindOfClass:[_sapp_tc_view class]])
                    std::printf("nativeWindow: AppKit window \"%s\" visible=%d key=%d\n", win.title.UTF8String, win.visible, win.keyWindow);
            check("disposed windows have left AppKit", liveNativeWindows() == 2);
        }
        reverseSawFullscreen = false;
        sapp_tc_window_set_fullscreen({target->win_id}, true);
        sapp_tc_window_toggle_fullscreen({target->win_id});
        check("toggle during entry records the reversal on the addressed window only",
            target->fullscreen_state.desired == false && peer->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Windowed);
        peer->ready = false;
        enter(ReverseWait);
        break;
    }
    case ReverseWait: {
        _sapp_tc_window_t* target = reverseTarget == 0 ? first : other;
        _sapp_tc_window_t* peer = target == first ? other : first;
        if (target->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Fullscreen ||
            (target->window.styleMask & NSWindowStyleMaskFullScreen)) reverseSawFullscreen = true;
        const bool settled = target->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Windowed &&
            !target->fullscreen_state.reconcilePending && !(target->window.styleMask & NSWindowStyleMaskFullScreen);
        if (!(settled && reverseSawFullscreen) && !waited(20)) break;
        std::printf("nativeWindow: reversal on %s window: saw fullscreen=%d settled=%d after %.1fs (source %s)\n",
            target == first ? "first" : "secondary", reverseSawFullscreen, settled, now() - stageStart,
            target->fallback_active ? "fallback" : "display link");
        check("reversal mid-entry settles windowed from the window's own tick", settled && reverseSawFullscreen &&
            !sapp_is_fullscreen() && peer->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Windowed);
        peer->ready = true;
        _sapp_tc_mac_observe(peer);
        if (++reverseTarget < 2) { enter(Reverse); break; }
        enter(NativeClose);
        break;
    }
    case NativeClose:
        // Close two secondaries natively (trusted / untrusted); close_cb keeps them.
        for (int i = 0; i < 2; ++i) {
            sapp_window_desc d = {};
            d.x = d.y = -1; d.width = 120; d.height = 90; d.title = "closed"; d.no_high_dpi = noHighDpi;
            d.mtl_device = (__bridge const void*)_sapp_tc.app.device;
            d.tick_cb = closedTick; d.close_cb = keepOnClose; d.user_data = (void*)(intptr_t)i;
            closedWindows[i] = sapp_create_window(&d);
            _sapp_tc_window_t* w = _sapp_tc_lookup(closedWindows[i]);
            if (!w) { check("closed-window probe created", false); continue; }
            w->trust_hidden_signals = i == 0;
            NSWindow* window = w->window;
            [window performClose:nil];
            // AppKit reports the closed window as no longer visible.
            forceHidden = window;
            [[NSNotificationCenter defaultCenter] postNotificationName:NSWindowDidChangeOcclusionStateNotification object:window];
            forceHidden = nil;
        }
        other->desc.tick_cb = probeTick;
        mainDraws = true;
        sampleStart = 0;
        enter(NativeClosed);
        break;
    case NativeClosed:
        if (!waited(0.5)) break;
        if (sampleStart == 0) {
            sampleStart = now(); closedTicksAt[0] = closedTicks[0]; closedTicksAt[1] = closedTicks[1];
            mainDrawablesAt = mainDrawables; acquiredAt = acquired; break;
        }
        if (now() - sampleStart < 1.0) break;
        std::printf("nativeWindow: natively closed secondaries ticked %d (trusted) and %d (untrusted) times in %.1fs after close\n",
            closedTicks[0] - closedTicksAt[0], closedTicks[1] - closedTicksAt[1], now() - sampleStart);
        {
            const double seconds = now() - sampleStart;
            const double mainRate = (mainDrawables - mainDrawablesAt) / seconds, otherRate = (acquired - acquiredAt) / seconds;
            std::printf("nativeWindow: meanwhile first window drew %.1f/s, open secondary drew %.1f/s\n", mainRate, otherRate);
            check("other windows keep drawing at the display rate", mainRate >= 20 && otherRate >= 20);
            mainDraws = false;
        }
        for (int i = 0; i < 2; ++i) {
            check(i == 0 ? "natively closed secondary stops ticking (trusted)" : "natively closed secondary stops ticking (untrusted)",
                sapp_window_valid(closedWindows[i]) && closedTicks[i] == closedTicksAt[i]);
            sapp_destroy_window(closedWindows[i]);
            check("a natively closed secondary is destroyed once", !sapp_window_valid(closedWindows[i]));
        }
        sampleStart = 0;
        nestKind = 0;
        enter(NestSetup);
        break;
    case NestSetup:
        nestTicks = 0; nestNested = -1; nestArmed = nestInCallback = nestCloseDuringCallback = nestClosed = false;
        nestWindow = makeProbe(nestTick, nestEvent, nestClose, "nested");
        enter(NestWarm);
        break;
    case NestWarm: {
        if (!waited(0.5)) break;
        _sapp_tc_window_t* w = _sapp_tc_lookup(nestWindow);
        if (!w) { check("nested-loop probe created", false); enter(FsCreate); break; }
        if (nestKind == 0) { nestOn = SAPP_EVENTTYPE_RESIZED; w->fb_width = 1; }   // next tick re-publishes size
        else if (nestKind == 1) { nestOn = SAPP_EVENTTYPE_SUSPENDED; forceHidden = w->window; }   // next tick observes hidden
        else nestOn = SAPP_EVENTTYPE_KEY_DOWN;                                    // delivered below, outside any tick
        nestArmed = true;
        enter(NestWait);
        if (nestKind == 2) {
            _sapp_tc_view* view = w->view;
            NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                timestamp:0 windowNumber:w->window.windowNumber context:nil characters:@"a"
                charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
            [view keyDown:key];
        }
        break;
    }
    case NestWait:
        if (!nestClosed && !waited(3)) break;
        forceHidden = nil;
        std::printf("nativeWindow: nested run loop inside %s: %d nested frames of that window, close during callback=%d, closed after=%d\n",
            nestKind == 0 ? "in-tick RESIZED" : nestKind == 1 ? "in-tick SUSPENDED" : "out-of-tick KEY_DOWN",
            nestNested, nestCloseDuringCallback, nestClosed);
        // A window never ticks while one of its own callbacks is on the stack (D5).
        check(nestKind == 0 ? "no nested frame inside an in-tick RESIZED callback" :
              nestKind == 1 ? "no nested frame inside an in-tick SUSPENDED callback" :
                              "no nested frame inside an out-of-tick KEY_DOWN callback", nestNested == 0);
        check("close requested inside the callback is deferred until it returns, then processed",
            !nestCloseDuringCallback && nestClosed && !sapp_window_valid(nestWindow));
        if (sapp_window_valid(nestWindow)) sapp_destroy_window(nestWindow);
        if (++nestKind < 3) { enter(NestSetup); break; }
        enter(FsCreate);
        break;
    case FsCreate: {
        // Startup fullscreen on the common constructor; the client destroys the
        // window from the resize that fullscreen entry notifies.
        sapp_window_desc d = {};
        d.x = d.y = -1; d.width = 120; d.height = 90; d.title = "fullscreen victim"; d.no_high_dpi = noHighDpi;
        d.mtl_device = (__bridge const void*)_sapp_tc.app.device; d.event_cb = victimEvent;
        _sapp_tc_native_desc native = {};
        native.window = _sapp_tc_normalize_window(&d);
        native.color_format = SAPP_PIXELFORMAT_BGRA8; native.depth_format = SAPP_PIXELFORMAT_DEPTH_STENCIL;
        native.fullscreen = true;
        destroyOn = SAPP_EVENTTYPE_RESIZED; destroyedInCallback = false;
        fsCreated = _sapp_tc_mac_create_window(&native);
        fsCreatedId = fsCreated ? fsCreated->win_id : 0;
        std::printf("nativeWindow: startup fullscreen creation: destroyed during creation=%d returned=%d\n",
            destroyedInCallback, fsCreated != nullptr);
        check("destroy during startup-fullscreen creation returns invalid", !destroyedInCallback || fsCreated == nullptr);
        enter(FsSettle);
        break;
    }
    case FsSettle: {
        // Destroy from the later asynchronous resize is fine too; wait for settle.
        _sapp_tc_window_t* w = fsCreatedId ? _sapp_tc_lookup({fsCreatedId}) : nullptr;
        if (w && !waited(6)) {
            if (w->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Fullscreen) sapp_tc_window_set_fullscreen({fsCreatedId}, false);
            break;
        }
        std::printf("nativeWindow: startup fullscreen window destroyed by its RESIZED: %d (after creation returned)\n",
            destroyedInCallback);
        if (w && w->fullscreen_state.phase == _sapp_tc_fullscreen_phase::Windowed) sapp_destroy_window({fsCreatedId});
        destroyOn = (sapp_event_type)0;
        if (_sapp_tc_lookup({fsCreatedId})) { if (!waited(15)) break; sapp_destroy_window({fsCreatedId}); }
        enter(ThrowTickStart);
        break;
    }
    case ThrowTickStart:
        throwTicks = 0; throwClosed = false;
        throwWindow = makeProbe(throwTick, nullptr, throwClose, "throwing tick");
        enter(ThrowTickWait);
        break;
    case ThrowTickWait: {
        _sapp_tc_window_t* w = _sapp_tc_lookup(throwWindow);
        if (w && !waited(1.0)) break;
        if (w && !w->close_requested && !throwClosed) {
            std::printf("nativeWindow: throwing tick_cb: %d ticks in 1s (threw on tick 3)\n", throwTicks);
            check("ticks continue after tick_cb throws", throwTicks >= 10 && !w->in_tick && !w->draw_admitted);
            sapp_window_request_close(throwWindow);
            break;
        }
        if (!throwClosed && !waited(4)) break;
        check("a close request is still processed after tick_cb threw (close_cb throw contained too)",
            throwClosed && !sapp_window_valid(throwWindow));
        enter(ThrowEventStart);
        break;
    }
    case ThrowEventStart:
        throwEventTicks = 0; throwClosed = eventThrown = false;
        throwWindow = makeProbe(countTick, throwEvent, throwClose, "throwing event");
        enter(ThrowEventWait);
        break;
    case ThrowEventWait: {
        _sapp_tc_window_t* w = _sapp_tc_lookup(throwWindow);
        if (w && !waited(0.3)) break;
        if (w && !eventThrown && sampleStart == 0) {
            w->fb_width = 1; sampleStart = now();                    // RESIZED next tick (in-tick throw)
            _sapp_tc_view* view = w->view;                           // and an out-of-tick throw from AppKit input
            NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                timestamp:0 windowNumber:w->window.windowNumber context:nil characters:@"a"
                charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
            [view keyDown:key];
            check("an out-of-tick event_cb throw is contained at the input boundary", keyThrown);
            break;
        }
        if (w && now() - sampleStart < 1.0) break;
        if (w && !w->close_requested && !throwClosed) {
            std::printf("nativeWindow: throwing event_cb: threw=%d, %d ticks\n", eventThrown, throwEventTicks);
            check("ticks continue after event_cb throws", eventThrown && throwEventTicks >= 10 && !w->in_tick);
            sapp_window_request_close(throwWindow);
            break;
        }
        if (!throwClosed && !waited(5)) break;
        sampleStart = 0;
        check("a close request is still processed after event_cb threw", throwClosed && !sapp_window_valid(throwWindow));
        enter(ThrowLegacy);
        break;
    }
    case ThrowLegacy:
        mainThrowOnce = true;
        sampleStart = 0;
        enter(ThrowLegacyWait);
        break;
    case ThrowLegacyWait:
        if (!mainThrew && !waited(2)) break;
        if (sampleStart == 0) { sampleStart = now(); mainFramesAt = mainFrames; break; }
        if (now() - sampleStart < 0.5) break;
        check("first-window frames continue after the legacy frame_cb throws", mainThrew &&
            mainFrames - mainFramesAt >= 5 && !first->in_tick && !first->draw_admitted);
        sampleStart = 0;
        modalRound = 0;
        enter(ModalSetup);
        break;
    case ModalSetup:
        modalOwnerTicks = 0; modalResumedAt = 0;
        modalClosed = modalClosedInHandler = modalHandlerDone = false;
        modalRequestClose = modalRound == 1;
        modalOwner = makeProbe(modalTick, modalEvent, modalClose, "modal owner");
        _sapp_tc_lookup(second)->desc.tick_cb = probeTick;
        enter(ModalWarm);
        break;
    case ModalWarm: {
        if (!waited(0.5)) break;
        _sapp_tc_window_t* w = _sapp_tc_lookup(modalOwner);
        if (!w) { check("modal owner created", false); enter(ModalAfter); break; }
        _sapp_tc_view* view = w->view;
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
            timestamp:0 windowNumber:w->window.windowNumber context:nil characters:@"m"
            charactersIgnoringModifiers:@"m" isARepeat:NO keyCode:0];
        [view keyDown:key];   // returns after the modal is dismissed
        enter(ModalAfter);
        break;
    }
    case ModalAfter:
        if (!waited(1.0)) break;
        if (modalRound == 0) {
            const double resume = modalResumedAt > 0 ? modalResumedAt - modalEnd : -1;
            std::printf("nativeWindow: owner resumed %.3fs after the modal, %d ticks in the following second\n",
                resume, modalOwnerTicks - modalOwnerAfterAt);
            check("owner resumes within a few frames after the modal", modalHandlerDone && resume >= 0 && resume < 0.2 &&
                modalOwnerTicks - modalOwnerAfterAt >= 20);
            sapp_destroy_window(modalOwner);
            ++modalRound;
            enter(ModalSetup);
            break;
        }
        std::printf("nativeWindow: close requested before the modal: closed=%d, inside handler=%d\n",
            modalClosed, modalClosedInHandler);
        check("a close requested before the modal is processed after the handler returns",
            modalHandlerDone && modalClosed && !modalClosedInHandler && !sapp_window_valid(modalOwner));
        if (sapp_window_valid(modalOwner)) sapp_destroy_window(modalOwner);
        ccKind = 0;
        enter(CcSetup);
        break;
    case CcSetup:
        ccCloses = ccTicksAfterNativeClose = 0;
        ccArmed = ccInCallback = ccClosedInCallback = ccSpinDone = ccNativeClosed = false;
        ccWindow = makeProbe(ccTick, ccEvent, ccClose, "native close");
        enter(CcWarm);
        break;
    case CcWarm: {
        if (!waited(0.5)) break;
        _sapp_tc_window_t* w = _sapp_tc_lookup(ccWindow);
        if (!w) { check("native-close probe created", false); enter(TerminateChild); break; }
        ccArmed = true;
        enter(CcWait);
        if (ccKind % 2 == 0) {
            _sapp_tc_view* view = w->view;
            NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                timestamp:0 windowNumber:w->window.windowNumber context:nil characters:@"c"
                charactersIgnoringModifiers:@"c" isARepeat:NO keyCode:0];
            [view keyDown:key];
        }
        break;
    }
    case CcWait:
        if (!waited(1.5)) break;
        std::printf("nativeWindow: %s inside %s callback: close_cb ran %d time(s), inside callback=%d, ticks between close and teardown=%d\n",
            ccKind >= 2 ? "[NSWindow close]" : "performClose:", ccKind % 2 == 0 ? "an event" : "a tick",
            ccCloses, ccClosedInCallback, ccTicksAfterNativeClose);
        {
            static const char* names[] = {
                "native close inside an event callback runs close_cb once, after it returns",
                "native close inside a tick callback runs close_cb once, after it returns",
                "direct NSWindow close inside an event callback runs close_cb once, after it returns, no tick between",
                "direct NSWindow close inside a tick callback runs close_cb once, after it returns, no tick between"};
            check(names[ccKind], ccSpinDone && ccCloses == 1 && !ccClosedInCallback && !sapp_window_valid(ccWindow) &&
                (ccKind < 2 || ccTicksAfterNativeClose == 0));
        }
        if (sapp_window_valid(ccWindow)) sapp_destroy_window(ccWindow);
        if (++ccKind < 4) { enter(CcSetup); break; }
        xKind = 4;
        enter(XSetup);
        break;
    case XSetup:
        xArmed = xSpinDone = xQuitInside = yNativeClosed = yClosedInside = false;
        yTicksAfterClose = yCloses = 0;
        xQuitBefore = quitRequests;
        xWindow = makeProbe(nullptr, xEvent, nullptr, "x");
        yWindow = xKind == 5 ? makeProbe(yTick, nullptr, yClose, "y") : sapp_window{0};
        enter(XWarm);
        break;
    case XWarm: {
        if (!waited(0.5)) break;
        _sapp_tc_window_t* x = _sapp_tc_lookup(xWindow);
        if (!x) { check("cross-window probe created", false); enter(TerminateChild); break; }
        xArmed = true;
        enter(XWait);
        _sapp_tc_view* view = x->view;
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
            timestamp:0 windowNumber:x->window.windowNumber context:nil characters:@"x"
            charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:0];
        [view keyDown:key];
        break;
    }
    case XWait:
        if (!waited(1.0)) break;
        if (xKind == 4) {
            std::printf("nativeWindow: performClose: of the first window inside another window's callback: quit requests %d, inside callback=%d\n",
                quitRequests - xQuitBefore, xQuitInside);
            check("first-window performClose: inside a callback asks to quit once, after it returns (vetoed)",
                xSpinDone && quitRequests - xQuitBefore == 1 && !xQuitInside && _sapp_tc.app.main && first->window.visible);
            quitRequests = xQuitBefore;   // keep the scenario's own quit count
        } else {
            std::printf("nativeWindow: [NSWindow close] of Y inside X's callback: close_cb ran %d time(s), inside=%d, Y ticks between close and teardown=%d\n",
                yCloses, yClosedInside, yTicksAfterClose);
            check("direct close of another window inside a callback: close_cb once, after it returns, no tick between",
                xSpinDone && yCloses == 1 && !yClosedInside && yTicksAfterClose == 0 && !sapp_window_valid(yWindow));
            if (sapp_window_valid(yWindow)) sapp_destroy_window(yWindow);
        }
        if (sapp_window_valid(xWindow)) sapp_destroy_window(xWindow);
        if (++xKind <= 5) { enter(XSetup); break; }
        enter(TerminateChild);
        break;
    case TerminateChild: {
        // Terminate ends the process, so that scenario runs in a child.
        NSTask* task = [NSTask new];
        task.executableURL = [NSURL fileURLWithPath:NSProcessInfo.processInfo.arguments[0]];
        task.arguments = @[@"--display", @"--terminate-in-callback"];
        NSPipe* pipe = [NSPipe pipe];
        task.standardOutput = pipe;
        NSError* error = nil;
        const bool launched = [task launchAndReturnError:&error];
        NSData* output = launched ? [pipe.fileHandleForReading readDataToEndOfFile] : nil;
        if (launched) [task waitUntilExit];
        NSString* text = output ? [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding] : @"";
        std::printf("%s", text.UTF8String);
        check("terminate inside a callback (child): veto works, cleanup and exit once after the callback",
            launched && task.terminationStatus == 0 && [text containsString:@"terminate scenario complete"] &&
            ![text containsString:@": FAIL"]);
        sapp_window_request_close(second);
        enter(CloseSecondary);
        break;
    }
    case CloseSecondary:
        if (!secondClosed && !waited(3)) break;
        check("deferred secondary close reaches its close callback and disposes it",
            secondClosed && !sapp_window_valid(second));
        sapp_window_request_close({first->win_id});
        enter(VetoQuit);
        break;
    case VetoQuit:
        if (quitRequests == 0 && !waited(3)) break;
        if (!waited(0.3)) break;
        check("first-window close request is a vetoable process quit", quitRequests == 1 &&
            _sapp_tc.app.main == first && first->window.visible);
        allowQuit = true;
        sapp_destroy_window({first->win_id});   // redirects to the same quit request
        enter(Done);
        break;
    case Done:
        if (waited(5)) { check("quit completes", false); std::_Exit(1); }
        break;
    }
}

void mainEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        ++quitRequests;
        if (termInCallback) quitInsideCallback = true;
        if (ccInCallback) xQuitInside = true;
        if (!allowQuit) sapp_cancel_quit();
    }
}

void mainFrame() {
    ++mainFrames;
    if (terminateMode) {
        if (mainFrames == 10) {             // round 1: vetoed
            spinWithTerminate();
            termRound = 1;
        } else if (termRound == 1 && mainFrames == 40) {
            check("terminate child: vetoed quit keeps the app running; QUIT_REQUESTED came after the callback",
                quitRequests == 1 && !quitInsideCallback && cleanups == 0);
            allowQuit = true;                 // round 2: allowed
            termRound = 2;
            spinWithTerminate();
            termSpinReturned = true;
        }
        return;
    }
    if (mainThrowOnce) { mainThrowOnce = false; mainThrew = true; throw std::runtime_error("legacy frame_cb test throw"); }
    if (coveredFirst) {
        const void* drawable = sapp_metal_get_current_drawable();
        std::printf("nativeWindow: covered first frame: occluded=%d source=%s drawable=%d\n",
            sapp_window_occluded({0}), _sapp_tc.app.main->fallback_active ? "fallback" : "display link", drawable != nullptr);
        check("covered first frame follows init on the fallback source", didInitialize && sapp_window_occluded({0}) &&
            _sapp_tc.app.main->fallback_active && sapp_frame_count() == 0);
        check("covered first frame draws according to launch data", (drawable != nullptr) == (launchHidden >= 0));
        present((__bridge id<CAMetalDrawable>)drawable);
        coveredReleased = true;
        sapp_quit();
        return;
    }
    if (mainDraws) {
        id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)sapp_metal_get_current_drawable();
        if (drawable) { ++mainDrawables; present(drawable); }
    }
}

void initialized() {
    didInitialize = true;
    _sapp_tc_window_t* first = _sapp_tc.app.main;
    check("first window is window #1 of the common constructor", first && first->is_main &&
        _sapp_tc.windows[0] == first && first->ready);
    check("initial hidden admission is descriptor data", first->draw_while_hidden == (launchHidden >= 0));
    check("hidden signals trusted by default", first->trust_hidden_signals);
    if (coveredFirst) {
        check("covered launch initializes before first frame", sapp_frame_count() == 0 && first->occluded);
        return;
    }
    if (terminateMode) return;
    const float scale = expectedScale(first->window);
    std::printf("nativeWindow: backingScaleFactor=%.2f high_dpi=%d main=%.2f/%dx%d (%dx%d pt)\n",
        first->window.backingScaleFactor, !noHighDpi, first->dpi_scale, first->fb_width, first->fb_height,
        first->win_width, first->win_height);
    check("initial dimensions precede first frame", sapp_frame_count() == 0 &&
        first->dpi_scale == scale && sapp_dpi_scale() == scale &&
        first->fb_width == int(101 * scale) && first->fb_height == int(77 * scale) &&
        sapp_width() == first->fb_width && sapp_height() == first->fb_height);
    check("first window keeps its 10-bit output", sapp_color_format() == SAPP_PIXELFORMAT_RGB10A2 &&
        first->color_fmt == MTLPixelFormatRGB10A2Unorm);
    sapp_window_desc desc = {};
    desc.x = desc.y = -1;
    desc.width = 101; desc.height = 77; desc.no_high_dpi = noHighDpi; desc.title = "nativeWindow secondary";
    desc.mtl_device = (__bridge const void*)_sapp_tc.app.device;
    desc.event_cb = secondaryEvent; desc.close_cb = secondaryClosed;
    second = sapp_create_window(&desc);
    _sapp_tc_window_t* other = _sapp_tc_lookup(second);
    check("secondary created", other != nullptr);
    if (!other) { sapp_quit(); return; }
    std::printf("nativeWindow: secondary=%.2f/%dx%d (%dx%d pt)\n", other->dpi_scale, other->fb_width,
        other->fb_height, other->win_width, other->win_height);
    check("secondary dimensions are published by its constructor", other->ready &&
        sapp_window_framebuffer_width(second) == other->fb_width && other->fb_width > 0 &&
        sapp_window_dpi_scale(second) == expectedScale(other->window));
    check("initial scales and dimensions agree", first->dpi_scale == other->dpi_scale &&
        first->fb_width == other->fb_width && first->fb_height == other->fb_height &&
        first->win_width == other->win_width && first->win_height == other->win_height);
    check("secondary trusts hidden signals and does not draw while hidden", other->trust_hidden_signals &&
        !other->draw_while_hidden && !other->is_main && other->color_fmt == MTLPixelFormatBGRA8Unorm);
    check("each window has its own display link", first->link && other->link && first->link != other->link);
    // macOS creation attributes are data: the launch path passes all three.
    check("first window has its launch attributes (restorable, nearest filter, key focus)",
        first->window.restorable && [first->layer.magnificationFilter isEqualToString:kCAFilterNearest] &&
        first->window.firstResponder == first->view);
    std::printf("nativeWindow: secondary restorable=%d filter=%s firstResponder=%s\n", other->window.restorable,
        other->layer.magnificationFilter.UTF8String, NSStringFromClass([other->window.firstResponder class]).UTF8String);
    // AppKit itself defaults restorable to YES and focuses the content view of a
    // key window, so only the filter is observably different for a secondary.
    check("secondary keeps the default linear filter", [other->layer.magnificationFilter isEqualToString:kCAFilterLinear]);
    check("both windows were checked at show time", showsChecked >= 2);
    driver = [NSTimer timerWithTimeInterval:0.02 repeats:YES block:^(NSTimer*) { step(); }];
    [[NSRunLoop mainRunLoop] addTimer:driver forMode:NSRunLoopCommonModes];
}

void finish() {
    // AppKit termination: cleanup ran, then the common unwinder disposed every window.
    bool empty = _sapp_tc.app.main == nullptr;
    for (auto* w : _sapp_tc.windows) empty = empty && w == nullptr;
    check("process cleanup releases every window", empty && didInitialize);
    if (coveredFirst) check("quit requested by the first frame ends without another frame", mainFrames == 1);
    if (terminateMode) {
        std::printf("nativeWindow: terminate child: quit requests=%d cleanups=%d inside callback: quit=%d cleanup=%d, spin returned=%d\n",
            quitRequests, cleanups, quitInsideCallback, cleanupInsideCallback, termSpinReturned);
        check("terminate child: cleanup runs exactly once, after the callback returned",
            termRound == 2 && quitRequests == 2 && cleanups == 1 && !cleanupInsideCallback && !quitInsideCallback && termSpinReturned);
        if (!failures) std::puts("nativeWindow: terminate scenario complete");
    }
    if (!coveredFirst && !terminateMode) check("scenario completed", stage == Done && quitRequests == 2);
    std::fflush(stdout);
    _exit(failures ? 1 : 0);
}
}

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--display")) {
        std::puts("SKIP: nativeWindow --display [--no-high-dpi|--covered-*] needs a window server session"); return 0;
    }
    noHighDpi = argc > 2 && !std::strcmp(argv[2], "--no-high-dpi");
    coveredFirst = argc > 2 && !std::strncmp(argv[2], "--covered-", 10);
    terminateMode = argc > 2 && !std::strcmp(argv[2], "--terminate-in-callback");
    if (coveredFirst) {
        if (!std::strcmp(argv[2], "--covered-draw")) launchHidden = 1;
        if (!std::strcmp(argv[2], "--covered-pause")) launchHidden = -1;
    }
    swap([NSWindow class], @selector(makeKeyAndOrderFront:), recordShow, &realShow);
    swap([NSWindow class], @selector(backingScaleFactor), injectScale, &realScale);
    swap([NSWindow class], @selector(occlusionState), injectOcclusion, &realOcclusion);
    // Override +layer for CAMetalLayer only; AppKit's own view layers keep CALayer's.
    realLayer = (CAMetalLayer* (*)(id, SEL))method_getImplementation(class_getClassMethod([CAMetalLayer class], @selector(layer)));
    class_addMethod(object_getClass([CAMetalLayer class]), @selector(layer), (IMP)injectLayer, "@@:");
    std::atexit(finish);
    sapp_desc desc{};
    desc._tc_draw_while_hidden = launchHidden; desc.width = 101; desc.height = 77;
    desc.high_dpi = !noHighDpi; desc.init_cb = initialized; desc.frame_cb = mainFrame;
    desc.event_cb = mainEvent; desc.window_title = "nativeWindow";
    desc.cleanup_cb = mainCleanup;
    sapp_run(&desc);
    check("sapp_run returns only after a failed launch", false);
    return 1;
}
