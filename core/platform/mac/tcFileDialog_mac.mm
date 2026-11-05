// =============================================================================
// macOS file dialog implementation
// =============================================================================

#include "TrussC.h"
#include "tc/utils/internal/tcAsyncDialog.h"
#include "sokol_app_tc.h"
#include "tc/utils/tcTeardown.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

// NSAlert's modal API registers alerts that AppKit dismisses before a quit
// veto. Use its laid-out window and ordinary button actions instead.
@interface TCAsyncDialogButtonTarget : NSObject
@property(nonatomic, copy) void (^reply)(NSButton*);
- (void)clicked:(NSButton*)sender;
@end
@implementation TCAsyncDialogButtonTarget
- (void)clicked:(NSButton*)sender { self.reply(sender); }
@end

namespace trussc {

namespace {
// Sheets, unattached panels and alert windows share the existing pending list.
struct DialogSheet {
    NSWindow* sheet;
    std::function<void(NSModalResponse)> completion;
    NSAlert* alert = nil;
    TCAsyncDialogButtonTarget* buttonTarget = nil;
};
bool cancellingForQuit = false;
std::vector<std::shared_ptr<DialogSheet>>& dialogSheets() {
    // Process lifetime: Runtime's atexit shutdown can still cancel dialogs.
    static auto* sheets = new std::vector<std::shared_ptr<DialogSheet>>;
    return *sheets;
}
void completeDialogSheet(const std::shared_ptr<DialogSheet>& sheet, NSModalResponse response) {
    std::erase(dialogSheets(), sheet);
    auto completion = std::exchange(sheet->completion, {});
    if (completion) completion(response);
}
std::shared_ptr<DialogSheet> trackDialogSheet(
        NSWindow* sheet, std::function<void(NSModalResponse)> completion) {
    auto pending = std::make_shared<DialogSheet>(DialogSheet{sheet, std::move(completion)});
    dialogSheets().push_back(pending);
    return pending;
}
void closeDialogSheet(const std::shared_ptr<DialogSheet>& pending, NSModalResponse response) {
    // Take the reply first: endSheet/cancel may call the native handler inline.
    auto completion = std::exchange(pending->completion, {});
    std::erase(dialogSheets(), pending);
    if (pending->sheet.sheetParent) {
        [pending->sheet.sheetParent endSheet:pending->sheet returnCode:response];
    } else if ([pending->sheet isKindOfClass:[NSSavePanel class]]) {
        [(NSSavePanel*)pending->sheet cancel:nil];
    }
    [pending->sheet orderOut:nil];
    if (completion) completion(response);
}
} // namespace

namespace internal {
bool completingMacDialogsForQuit() { return cancellingForQuit; }
void cancelMacDialogSheets() noexcept {
    const bool previous = std::exchange(cancellingForQuit, true);
    auto sheets = std::exchange(dialogSheets(), {});
    TeardownSteps steps;
    for (const auto& pending : sheets) {
        // EXIT_PROTOCOL: cancel once while the App is still alive, before exit.
        steps.run("dialog cancellation", [&] { closeDialogSheet(pending, NSModalResponseCancel); });
    }
    cancellingForQuit = previous;
}
} // namespace internal

namespace {
NSWindow* visibleDialogOwner(const internal::DialogInputs& inputs) {
    auto nativeVisible = [](Window* window) -> NSWindow* {
        if (!window || !window->isOpen()) return nil;
        auto handle = internal::WindowRequestAccess::binding(*window).handle;
        NSWindow* native = (__bridge NSWindow*)(void*)sapp_window_macos_get_window(handle);
        return native.isVisible && !native.isMiniaturized ? native : nil;
    };
    if (auto requester = inputs.requestingWindow.lock()) {
        if (auto* native = nativeVisible(requester.get())) return native;
    }
    for (auto* window : openWindows()) {
        if (auto* native = nativeVisible(window)) return native;
    }
    return nil;
}

internal::DialogDismiss presentDialog(const internal::DialogInputs& inputs,
                                     internal::DialogCompletion complete) {
    using internal::DialogKind;
    @autoreleasepool {
        NSWindow* owner = visibleDialogOwner(inputs);
        std::shared_ptr<DialogSheet> pending;
        if (inputs.kind == DialogKind::Alert || inputs.kind == DialogKind::Confirm) {
            const bool confirm = inputs.kind == DialogKind::Confirm;
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = [NSString stringWithUTF8String:inputs.title.c_str()];
            alert.informativeText = [NSString stringWithUTF8String:inputs.message.c_str()];
            [alert addButtonWithTitle:confirm ? @"Yes" : @"OK"];
            if (confirm) [alert addButtonWithTitle:@"No"];
            alert.alertStyle = confirm ? NSAlertStyleWarning : NSAlertStyleInformational;
            [alert layout];
            pending = trackDialogSheet(alert.window, [complete, confirm](NSModalResponse response) {
                FileDialogResult result;
                result.success = confirm && response == NSAlertFirstButtonReturn;
                complete(std::move(result));
            });
            pending->alert = alert;
            pending->buttonTarget = [[TCAsyncDialogButtonTarget alloc] init];
            const auto weak = std::weak_ptr(pending);
            pending->buttonTarget.reply = ^(NSButton* button) {
                if (auto live = weak.lock()) closeDialogSheet(live, button.tag);
            };
            NSInteger index = 0;
            for (NSButton* button in alert.buttons) {
                button.tag = NSAlertFirstButtonReturn + index++;
                button.target = pending->buttonTarget;
                button.action = @selector(clicked:);
            }
            if (owner) {
                [owner beginSheet:alert.window completionHandler:^(NSModalResponse response) {
                    completeDialogSheet(pending, response);
                }];
            } else {
                [alert.window center];
                [alert.window makeKeyAndOrderFront:nil];
            }
        } else {
            const bool save = inputs.kind == DialogKind::Save;
            NSSavePanel* panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
            if (!inputs.title.empty()) panel.title = [NSString stringWithUTF8String:inputs.title.c_str()];
            if (!inputs.message.empty()) panel.message = [NSString stringWithUTF8String:inputs.message.c_str()];
            if (!inputs.defaultPath.empty()) {
                panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:inputs.defaultPath.c_str()]];
            }
            panel.canCreateDirectories = YES;
            if (save) {
                if (!inputs.defaultName.empty()) panel.nameFieldStringValue = [NSString stringWithUTF8String:inputs.defaultName.c_str()];
            } else {
                NSOpenPanel* open = (NSOpenPanel*)panel;
                open.canChooseFiles = !inputs.folderSelection;
                open.canChooseDirectories = inputs.folderSelection;
                open.allowsMultipleSelection = NO;
                open.resolvesAliases = YES;
            }
            pending = trackDialogSheet(panel, [complete, panel](NSModalResponse response) {
                FileDialogResult result;
                if (response == NSModalResponseOK && panel.URL) {
                    result.success = true;
                    result.filePath = panel.URL.path.UTF8String;
                    result.fileName = panel.URL.lastPathComponent.UTF8String;
                }
                complete(std::move(result));
            });
            auto handler = ^(NSModalResponse response) { completeDialogSheet(pending, response); };
            if (owner) [panel beginSheetModalForWindow:owner completionHandler:handler];
            else [panel beginWithCompletionHandler:handler];
        }
        return [pending] {
            pending->completion = {}; // Owner cancellation never calls user code.
            closeDialogSheet(pending, NSModalResponseCancel);
        };
    }
}
} // namespace

// -----------------------------------------------------------------------------
// Alert dialog
// -----------------------------------------------------------------------------
void alertDialog(const std::string& title, const std::string& message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert runModal];
    }
}

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Alert, title, message, {}, {}},
        [callback = std::move(callback)](FileDialogResult) { if (callback) callback(); }, presentDialog);
}

// -----------------------------------------------------------------------------
// Confirm dialog
// -----------------------------------------------------------------------------
bool confirmDialog(const std::string& title, const std::string& message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        [alert setAlertStyle:NSAlertStyleWarning];
        return [alert runModal] == NSAlertFirstButtonReturn;
    }
}

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Confirm, title, message, {}, {}},
        [callback = std::move(callback)](FileDialogResult result) { if (callback) callback(result.success); }, presentDialog);
}

// -----------------------------------------------------------------------------
// Load dialog
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            bool folderSelection) {
    FileDialogResult result;

    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        if (!title.empty()) {
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
        }
        if (!message.empty()) {
            [panel setMessage:[NSString stringWithUTF8String:message.c_str()]];
        }

        if (folderSelection) {
            [panel setCanChooseFiles:NO];
            [panel setCanChooseDirectories:YES];
            [panel setCanCreateDirectories:YES];
        } else {
            [panel setCanChooseFiles:YES];
            [panel setCanChooseDirectories:NO];
        }

        [panel setAllowsMultipleSelection:NO];
        [panel setResolvesAliases:YES];

        if (!defaultPath.empty()) {
            NSString* path = [NSString stringWithUTF8String:defaultPath.c_str()];
            NSURL* url = [NSURL fileURLWithPath:path];
            [panel setDirectoryURL:url];
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url) {
                result.success = true;
                result.filePath = [[url path] UTF8String];
                result.fileName = [[url lastPathComponent] UTF8String];
            }
        }
    }

    return result;
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Load, title, message, defaultPath, {}, folderSelection},
        std::move(callback), presentDialog);
}

// -----------------------------------------------------------------------------
// Save dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            const fs::path& defaultName) {
    FileDialogResult result;

    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];

        if (!title.empty()) {
            [panel setTitle:[NSString stringWithUTF8String:title.c_str()]];
        }
        if (!message.empty()) {
            [panel setMessage:[NSString stringWithUTF8String:message.c_str()]];
        }
        if (!defaultPath.empty()) {
            NSString* path = [NSString stringWithUTF8String:defaultPath.c_str()];
            NSURL* url = [NSURL fileURLWithPath:path];
            [panel setDirectoryURL:url];
        }
        if (!defaultName.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:defaultName.c_str()]];
        }

        [panel setCanCreateDirectories:YES];

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [panel URL];
            if (url) {
                result.success = true;
                result.filePath = [[url path] UTF8String];
                result.fileName = [[url lastPathComponent] UTF8String];
            }
        }
    }

    return result;
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     const fs::path& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Save, title, message, defaultPath, defaultName},
        std::move(callback), presentDialog);
}

} // namespace trussc

#endif // __APPLE__
