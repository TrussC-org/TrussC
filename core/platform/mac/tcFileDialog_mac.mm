// =============================================================================
// macOS file dialog implementation
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

namespace trussc {

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
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSAlertStyleInformational];

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
                (void)response;
                if (callback) callback();
            }];
        } else {
            [alert runModal];
            if (callback) callback();
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
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"Yes"];
        [alert addButtonWithTitle:@"No"];
        [alert setAlertStyle:NSAlertStyleWarning];

        NSWindow* window = [[NSApplication sharedApplication] keyWindow];
        if (window) {
            [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
                if (callback) callback(response == NSAlertFirstButtonReturn);
            }];
        } else {
            NSModalResponse response = [alert runModal];
            if (callback) callback(response == NSAlertFirstButtonReturn);
        }
    }
}

// -----------------------------------------------------------------------------
// Load dialog
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
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
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
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
            [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
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
            }];
        } else {
            FileDialogResult result = loadDialog(title, message, defaultPath, folderSelection);
            if (callback) callback(result);
        }
    }
}

// -----------------------------------------------------------------------------
// Save dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            const std::string& defaultName) {
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
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
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
            [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
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
            }];
        } else {
            FileDialogResult result = saveDialog(title, message, defaultPath, defaultName);
            if (callback) callback(result);
        }
    }
}

} // namespace trussc

#endif // __APPLE__
