#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// File dialog
// Display OS-native file selection dialog
// =============================================================================
// All dialog functions follow unified parameter order:
//   (title, message, ..., callback for async)
// =============================================================================
// NOTE: Sync dialogs can't block on iOS. There they compile, log an error and
//       return a failed result (false / success == false); use the async
//       versions instead (alertDialogAsync, confirmDialogAsync, etc.)
// =============================================================================

#include <string>
#include "tc/utils/tcFileIO.h"   // fs alias
#include <vector>
#include <functional>

namespace trussc {

// Dialog result for load/save dialogs
struct FileDialogResult {
    fs::path filePath;      // Full path
    fs::path fileName;      // Filename only
    bool success = false;   // true if not cancelled
};

// -----------------------------------------------------------------------------
// Alert dialog
// title: Bold header text
// message: Body text
// -----------------------------------------------------------------------------
TC_PLATFORMS("macos,windows,linux,android,web") void alertDialog(const std::string& title, const std::string& message);

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback = nullptr);

// -----------------------------------------------------------------------------
// Confirm dialog (Yes/No)
// Returns true if user clicked Yes
// -----------------------------------------------------------------------------
TC_PLATFORMS("macos,windows,linux,android,web") bool confirmDialog(const std::string& title, const std::string& message);

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback);

// -----------------------------------------------------------------------------
// File open dialog
// folderSelection: true for folder selection mode
// -----------------------------------------------------------------------------
TC_PLATFORMS("macos,windows,linux,android") FileDialogResult loadDialog(const std::string& title = "",
                            const std::string& message = "",
                            const fs::path& defaultPath = {},
                            bool folderSelection = false);

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback);

// -----------------------------------------------------------------------------
// File save dialog
// defaultName: Initial filename
// -----------------------------------------------------------------------------
TC_PLATFORMS("macos,windows,linux,android") FileDialogResult saveDialog(const std::string& title = "",
                            const std::string& message = "",
                            const fs::path& defaultPath = {},
                            const fs::path& defaultName = {});

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     const fs::path& defaultName,
                     std::function<void(const FileDialogResult&)> callback);

} // namespace trussc

// Alias
namespace tc = trussc;
