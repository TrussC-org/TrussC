// macOS helpers for the session-end test: a logout quit Apple event dispatched
// in-process (kAEQuitReason = kAELogOut), one-shot timers and run-loop spinning.
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
