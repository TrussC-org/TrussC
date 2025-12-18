// =============================================================================
// macOS ファイルダイアログ実装
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

namespace trussc {

// -----------------------------------------------------------------------------
// ファイル選択（開く）ダイアログ
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(
    const std::string& windowTitle,
    bool folderSelection,
    const std::string& defaultPath
) {
    FileDialogResult result;

    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];

        // タイトル設定
        if (!windowTitle.empty()) {
            [panel setTitle:[NSString stringWithUTF8String:windowTitle.c_str()]];
        }

        // フォルダ選択モード
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

        // 初期パス設定
        if (!defaultPath.empty()) {
            NSString* path = [NSString stringWithUTF8String:defaultPath.c_str()];
            NSURL* url = [NSURL fileURLWithPath:path];
            [panel setDirectoryURL:url];
        }

        // ダイアログ表示
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

// -----------------------------------------------------------------------------
// ファイル保存ダイアログ
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(
    const std::string& defaultName,
    const std::string& message
) {
    FileDialogResult result;

    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];

        // メッセージ設定
        if (!message.empty()) {
            [panel setMessage:[NSString stringWithUTF8String:message.c_str()]];
        }

        // 初期ファイル名
        if (!defaultName.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:defaultName.c_str()]];
        }

        [panel setCanCreateDirectories:YES];

        // ダイアログ表示
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

// -----------------------------------------------------------------------------
// アラートダイアログ
// -----------------------------------------------------------------------------
void alertDialog(const std::string& message) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:message.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSAlertStyleInformational];
        [alert runModal];
    }
}

} // namespace trussc

#endif // __APPLE__
