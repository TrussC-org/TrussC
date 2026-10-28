// macOS helpers for the session-end test: a logout quit Apple event dispatched
// in-process (kAEQuitReason = kAELogOut), one-shot timers, run-loop spinning,
// a native key event sent straight to a window's view, a window count, and
// native resize / screen-change triggers with a layer drawable-size readout.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" void sessionEndDispatchLogout() {
    NSAppleEventDescriptor* event = [NSAppleEventDescriptor appleEventWithEventClass:kCoreEventClass
        eventID:kAEQuitApplication targetDescriptor:[NSAppleEventDescriptor currentProcessDescriptor]
        returnID:kAutoGenerateReturnID transactionID:kAnyTransactionID];
    [event setParamDescriptor:[NSAppleEventDescriptor descriptorWithEnumCode:kAELogOut] forKeyword:kAEQuitReason];
    AppleEvent reply = {typeNull, nullptr};
    static int refCon;   // unused by the quit handler; the parameter is non-null
    [[NSAppleEventManager sharedAppleEventManager] dispatchRawAppleEvent:event.aeDesc withRawReply:&reply handlerRefCon:&refCon];
    AEDisposeDesc(&reply);
}

extern "C" void sessionEndAfter(double seconds, void (*fn)()) {
    NSTimer* t = [NSTimer timerWithTimeInterval:seconds repeats:NO block:^(NSTimer*) { fn(); }];
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
}

extern "C" void sessionEndSpin(double seconds) {
    const double until = CACurrentMediaTime() + seconds;
    while (CACurrentMediaTime() < until) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
}

// Sends a key-down ('a') to the content view of the window with this title,
// as AppKit's event dispatch would. Returns false when no such window exists.
extern "C" bool sessionEndSendKey(const char* title) {
    NSString* wanted = [NSString stringWithUTF8String:title];
    for (NSWindow* window in [NSApp windows]) {
        if (![window.title isEqualToString:wanted]) continue;
        NSEvent* ev = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
            timestamp:0 windowNumber:window.windowNumber context:nil characters:@"a"
            charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
        [window.contentView keyDown:ev];
        return true;
    }
    return false;
}

static NSWindow* sessionEndFindWindow(const char* title) {
    NSString* wanted = [NSString stringWithUTF8String:title];
    for (NSWindow* window in [NSApp windows]) {
        if ([window.title isEqualToString:wanted]) return window;
    }
    return nil;
}

// The window's Metal layer drawable size in pixels (-1 when not found).
extern "C" void sessionEndDrawableSize(const char* title, int* width, int* height) {
    *width = -1;
    *height = -1;
    NSWindow* window = sessionEndFindWindow(title);
    if (!window || ![window.contentView.layer isKindOfClass:[CAMetalLayer class]]) return;
    const CGSize size = ((CAMetalLayer*)window.contentView.layer).drawableSize;
    *width = (int)size.width;
    *height = (int)size.height;
}

// A native resize of the window's content area (windowDidResize: follows).
extern "C" void sessionEndNativeResize(const char* title, double width, double height) {
    if (NSWindow* window = sessionEndFindWindow(title)) [window setContentSize:NSMakeSize(width, height)];
}

// Resizes only the content view, then posts the window's screen-change
// notification, so windowDidChangeScreen: re-samples a changed view size.
extern "C" void sessionEndViewResizeAndScreenChange(const char* title, double width, double height) {
    NSWindow* window = sessionEndFindWindow(title);
    if (!window) return;
    [window.contentView setFrameSize:NSMakeSize(width, height)];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSWindowDidChangeScreenNotification object:window];
}

extern "C" int sessionEndWindowCount() {
    return (int)[NSApp windows].count;
}

#endif
