#pragma once

// =============================================================================
// File dialog
// Display OS-native file selection dialog
// =============================================================================
// All dialog functions follow unified parameter order:
//   (title, message, ..., callback for async)
// =============================================================================
// NOTE: Sync dialog functions are not available on iOS.
//       Use async versions instead (alertDialogAsync, confirmDialogAsync, etc.)
// =============================================================================

#include <string>
#include <vector>
#include <functional>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Mark sync dialogs as compile error on iOS
#if defined(__APPLE__) && TARGET_OS_IOS
#define TC_SYNC_DIALOG_UNAVAILABLE \
    __attribute__((unavailable("Sync dialogs are not supported on iOS. Use async version instead.")))
#else
#define TC_SYNC_DIALOG_UNAVAILABLE
#endif

namespace trussc {

// Dialog result for load/save dialogs
struct FileDialogResult {
    std::string filePath;   // Full path
    std::string fileName;   // Filename only
    bool success = false;   // true if not cancelled
};

// -----------------------------------------------------------------------------
// Alert dialog
// title: Bold header text
// message: Body text
// -----------------------------------------------------------------------------
TC_SYNC_DIALOG_UNAVAILABLE
void alertDialog(const std::string& title, const std::string& message);

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback = nullptr);

// -----------------------------------------------------------------------------
// Confirm dialog (Yes/No)
// Returns true if user clicked Yes
// -----------------------------------------------------------------------------
TC_SYNC_DIALOG_UNAVAILABLE
bool confirmDialog(const std::string& title, const std::string& message);

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback);

// -----------------------------------------------------------------------------
// File open dialog
// folderSelection: true for folder selection mode
// -----------------------------------------------------------------------------
TC_SYNC_DIALOG_UNAVAILABLE
FileDialogResult loadDialog(const std::string& title = "",
                            const std::string& message = "",
                            const std::string& defaultPath = "",
                            bool folderSelection = false);

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback);

// -----------------------------------------------------------------------------
// File save dialog
// defaultName: Initial filename
// -----------------------------------------------------------------------------
TC_SYNC_DIALOG_UNAVAILABLE
FileDialogResult saveDialog(const std::string& title = "",
                            const std::string& message = "",
                            const std::string& defaultPath = "",
                            const std::string& defaultName = "");

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback);

} // namespace trussc

// Alias
namespace tc = trussc;
