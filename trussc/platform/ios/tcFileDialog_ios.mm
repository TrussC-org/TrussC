// =============================================================================
// iOS file dialog implementation
// Async versions: UIAlertController + UIDocumentPickerViewController
// Sync versions: unavailable on iOS (compile error via header attribute)
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "tc/utils/tcLog.h"
#include "sokol_app.h"

// ---------------------------------------------------------------------------
// Helper: get root view controller from sokol window
// ---------------------------------------------------------------------------
static UIViewController* _tc_getRootViewController() {
    UIWindow* window = (__bridge UIWindow*)sapp_ios_get_window();
    return window.rootViewController;
}

// ---------------------------------------------------------------------------
// UIDocumentPickerDelegate bridge
// ---------------------------------------------------------------------------
@interface TcDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^completionHandler)(NSURL* _Nullable);
@end

@implementation TcDocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
 didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (self.completionHandler && urls.count > 0) {
        self.completionHandler(urls.firstObject);
    } else if (self.completionHandler) {
        self.completionHandler(nil);
    }
    self.completionHandler = nil;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (self.completionHandler) {
        self.completionHandler(nil);
    }
    self.completionHandler = nil;
}

@end

// Keep delegate alive while picker is presented
static TcDocumentPickerDelegate* _tc_activePickerDelegate = nil;

namespace trussc {

// ---------------------------------------------------------------------------
// Sync versions (linker stubs - never called due to header unavailable attr)
// ---------------------------------------------------------------------------
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wavailability"

void alertDialog(const std::string& title, const std::string& message) {
    logError() << "[FileDialog] Sync alertDialog is not supported on iOS. Use alertDialogAsync.";
}

bool confirmDialog(const std::string& title, const std::string& message) {
    logError() << "[FileDialog] Sync confirmDialog is not supported on iOS. Use confirmDialogAsync.";
    return false;
}

FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            bool folderSelection) {
    logError() << "[FileDialog] Sync loadDialog is not supported on iOS. Use loadDialogAsync.";
    return FileDialogResult{};
}

FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            const std::string& defaultName) {
    logError() << "[FileDialog] Sync saveDialog is not supported on iOS. Use saveDialogAsync.";
    return FileDialogResult{};
}

#pragma clang diagnostic pop

// ---------------------------------------------------------------------------
// alertDialogAsync - UIAlertController
// ---------------------------------------------------------------------------
void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback) {
    auto cb = callback;
    NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
    NSString* nsMessage = [NSString stringWithUTF8String:message.c_str()];

    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* vc = _tc_getRootViewController();
        if (!vc) {
            logError() << "[FileDialog] No root view controller available";
            if (cb) cb();
            return;
        }

        UIAlertController* alert = [UIAlertController
            alertControllerWithTitle:nsTitle
                             message:nsMessage
                      preferredStyle:UIAlertControllerStyleAlert];

        [alert addAction:[UIAlertAction
            actionWithTitle:@"OK"
                      style:UIAlertActionStyleDefault
                    handler:^(UIAlertAction* action) {
            if (cb) cb();
        }]];

        [vc presentViewController:alert animated:YES completion:nil];
    });
}

// ---------------------------------------------------------------------------
// confirmDialogAsync - UIAlertController with Yes/No
// ---------------------------------------------------------------------------
void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    auto cb = callback;
    NSString* nsTitle = [NSString stringWithUTF8String:title.c_str()];
    NSString* nsMessage = [NSString stringWithUTF8String:message.c_str()];

    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* vc = _tc_getRootViewController();
        if (!vc) {
            logError() << "[FileDialog] No root view controller available";
            if (cb) cb(false);
            return;
        }

        UIAlertController* alert = [UIAlertController
            alertControllerWithTitle:nsTitle
                             message:nsMessage
                      preferredStyle:UIAlertControllerStyleAlert];

        [alert addAction:[UIAlertAction
            actionWithTitle:@"Yes"
                      style:UIAlertActionStyleDefault
                    handler:^(UIAlertAction* action) {
            if (cb) cb(true);
        }]];

        [alert addAction:[UIAlertAction
            actionWithTitle:@"No"
                      style:UIAlertActionStyleCancel
                    handler:^(UIAlertAction* action) {
            if (cb) cb(false);
        }]];

        [vc presentViewController:alert animated:YES completion:nil];
    });
}

// ---------------------------------------------------------------------------
// loadDialogAsync - UIDocumentPickerViewController (open)
// ---------------------------------------------------------------------------
void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    auto cb = callback;
    NSString* nsDefaultPath = defaultPath.empty() ? nil :
        [NSString stringWithUTF8String:defaultPath.c_str()];

    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* vc = _tc_getRootViewController();
        if (!vc) {
            logError() << "[FileDialog] No root view controller available";
            if (cb) cb(FileDialogResult{});
            return;
        }

        NSArray<UTType*>* types;
        if (folderSelection) {
            types = @[UTTypeFolder];
        } else {
            types = @[UTTypeItem];
        }

        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types];
        picker.allowsMultipleSelection = NO;

        if (nsDefaultPath) {
            picker.directoryURL = [NSURL fileURLWithPath:nsDefaultPath];
        }

        TcDocumentPickerDelegate* delegate = [[TcDocumentPickerDelegate alloc] init];
        delegate.completionHandler = ^(NSURL* url) {
            FileDialogResult result;
            if (url) {
                [url startAccessingSecurityScopedResource];
                result.success = true;
                result.filePath = [[url path] UTF8String];
                result.fileName = [[url lastPathComponent] UTF8String];
                [url stopAccessingSecurityScopedResource];
            }
            if (cb) cb(result);
            _tc_activePickerDelegate = nil;
        };

        _tc_activePickerDelegate = delegate;
        picker.delegate = delegate;

        [vc presentViewController:picker animated:YES completion:nil];
    });
}

// ---------------------------------------------------------------------------
// saveDialogAsync - UIDocumentPickerViewController (export)
// On iOS, creates a temp file and lets the user choose where to export it.
// The returned path is the export destination.
// ---------------------------------------------------------------------------
void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    auto cb = callback;

    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* vc = _tc_getRootViewController();
        if (!vc) {
            logError() << "[FileDialog] No root view controller available";
            if (cb) cb(FileDialogResult{});
            return;
        }

        // Create temp file for export
        NSString* tempDir = NSTemporaryDirectory();
        NSString* fileName = defaultName.empty() ?
            @"untitled" :
            [NSString stringWithUTF8String:defaultName.c_str()];
        NSString* tempPath = [tempDir stringByAppendingPathComponent:fileName];

        // Create empty file
        [[NSFileManager defaultManager] createFileAtPath:tempPath
                                                contents:[NSData data]
                                              attributes:nil];

        NSURL* tempURL = [NSURL fileURLWithPath:tempPath];

        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForExportingURLs:@[tempURL]];

        TcDocumentPickerDelegate* delegate = [[TcDocumentPickerDelegate alloc] init];
        delegate.completionHandler = ^(NSURL* url) {
            FileDialogResult result;
            if (url) {
                result.success = true;
                result.filePath = [[url path] UTF8String];
                result.fileName = [[url lastPathComponent] UTF8String];
            }
            if (cb) cb(result);
            _tc_activePickerDelegate = nil;

            // Clean up temp file
            [[NSFileManager defaultManager] removeItemAtPath:tempPath error:nil];
        };

        _tc_activePickerDelegate = delegate;
        picker.delegate = delegate;

        [vc presentViewController:picker animated:YES completion:nil];
    });
}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
