// =============================================================================
// macOS file dialog implementation
// =============================================================================

#include "tc/utils/tcFileDialog.h"
#include "tc/utils/tcMainThread.h"
#include "tc/utils/tcTeardown.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

namespace trussc {

namespace {
// Dialogs and their completions run on the main thread.
struct DialogSheet {
    NSWindow* sheet;
    std::function<void(NSModalResponse)> completion;
};
std::vector<std::shared_ptr<DialogSheet>>& dialogSheets() {
    static std::vector<std::shared_ptr<DialogSheet>> sheets;
    return sheets;
}
void completeDialogSheet(const std::shared_ptr<DialogSheet>& sheet, NSModalResponse response) {
    std::erase(dialogSheets(), sheet);
    auto completion = std::exchange(sheet->completion, {});
    if (completion) completion(response);
}
std::function<void(NSModalResponse)> trackDialogSheet(
        NSWindow* sheet, std::function<void(NSModalResponse)> completion) {
    auto pending = std::make_shared<DialogSheet>(DialogSheet{sheet, std::move(completion)});
    dialogSheets().push_back(pending);
    return [pending](NSModalResponse response) { completeDialogSheet(pending, response); };
}
} // namespace

namespace internal {
void cancelMacDialogSheets() noexcept {
    auto sheets = std::exchange(dialogSheets(), {});
    TeardownSteps steps;
    for (const auto& pending : sheets) {
        steps.run("dialog sheet dismissal", [&] {
            [pending->sheet.sheetParent endSheet:pending->sheet returnCode:NSModalResponseCancel];
        });
        // AppKit may defer its completion until after App teardown. Deliver
        // cancellation while the owner is alive; the later native reply is a no-op.
        steps.run("dialog sheet cancellation", [&] { completeDialogSheet(pending, NSModalResponseCancel); });
    }
}
} // namespace internal

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
    callback = internal::appOwnedCallback(std::move(callback));
    const std::weak_ptr<std::atomic<bool>> token = internal::appRunToken();
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSAlertStyleInformational];

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            auto completion = trackDialogSheet([alert window], [=](NSModalResponse response) {
                (void)response;
                if (!internal::appRunIsAlive(token)) return;
                if (callback) callback();
            });
            [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) { completion(response); }];
        } else {
            [alert runModal];
            if (internal::appRunIsAlive(token) && callback) callback();
        }
    }
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
    callback = internal::appOwnedCallback(std::move(callback));
    const std::weak_ptr<std::atomic<bool>> token = internal::appRunToken();
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        [alert setAlertStyle:NSAlertStyleWarning];

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            auto completion = trackDialogSheet([alert window], [=](NSModalResponse response) {
                if (!internal::appRunIsAlive(token)) return;
                if (callback) callback(response == NSAlertFirstButtonReturn);
            });
            [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) { completion(response); }];
        } else {
            NSModalResponse response = [alert runModal];
            if (internal::appRunIsAlive(token) && callback) callback(response == NSAlertFirstButtonReturn);
        }
    }
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
    callback = internal::appOwnedCallback(std::move(callback));
    const std::weak_ptr<std::atomic<bool>> token = internal::appRunToken();
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

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            auto completion = trackDialogSheet(panel, [=](NSModalResponse response) {
                if (!internal::appRunIsAlive(token)) return;
                FileDialogResult result;
                if (response == NSModalResponseOK) {
                    NSURL* url = [[panel URLs] firstObject];
                    if (url) {
                        result.success = true;
                        result.filePath = [[url path] UTF8String];
                        result.fileName = [[url lastPathComponent] UTF8String];
                    }
                }
                if (callback) callback(result);
            });
            [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) { completion(response); }];
        } else {
            FileDialogResult result = loadDialog(title, message, defaultPath, folderSelection);
            if (internal::appRunIsAlive(token) && callback) callback(result);
        }
    }
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
    callback = internal::appOwnedCallback(std::move(callback));
    const std::weak_ptr<std::atomic<bool>> token = internal::appRunToken();
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

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            auto completion = trackDialogSheet(panel, [=](NSModalResponse response) {
                if (!internal::appRunIsAlive(token)) return;
                FileDialogResult result;
                if (response == NSModalResponseOK) {
                    NSURL* url = [panel URL];
                    if (url) {
                        result.success = true;
                        result.filePath = [[url path] UTF8String];
                        result.fileName = [[url lastPathComponent] UTF8String];
                    }
                }
                if (callback) callback(result);
            });
            [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) { completion(response); }];
        } else {
            FileDialogResult result = saveDialog(title, message, defaultPath, defaultName);
            if (internal::appRunIsAlive(token) && callback) callback(result);
        }
    }
}

} // namespace trussc

#endif // __APPLE__
