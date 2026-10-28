// macOS helpers for the session-end test: a logout quit Apple event dispatched
// in-process (kAEQuitReason = kAELogOut), one-shot timers, run-loop spinning,
// a native key event sent straight to a window's view, and a window count.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>

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

extern "C" int sessionEndWindowCount() {
    return (int)[NSApp windows].count;
}

#endif
