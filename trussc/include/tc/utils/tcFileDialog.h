#pragma once

// =============================================================================
// File dialog
// Display OS-native file selection dialog
// =============================================================================

#include <string>
#include <vector>

namespace trussc {

// Dialog result
struct FileDialogResult {
    std::string filePath;   // Full path
    std::string fileName;   // Filename only
    bool success = false;   // true if not cancelled
};

// -----------------------------------------------------------------------------
// File open dialog
// windowTitle: Dialog title (defaults to "Open" if empty)
// folderSelection: true for folder selection mode
// defaultPath: Initial display path (defaults to home if empty)
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(
    const std::string& windowTitle = "",
    bool folderSelection = false,
    const std::string& defaultPath = ""
);

// -----------------------------------------------------------------------------
// File save dialog
// defaultName: Initial filename
// message: Message to display in dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(
    const std::string& defaultName = "",
    const std::string& message = ""
);

// -----------------------------------------------------------------------------
// Alert dialog (message display)
// -----------------------------------------------------------------------------
void alertDialog(const std::string& message);

// -----------------------------------------------------------------------------
// Version with extended filters (for future use)
// -----------------------------------------------------------------------------
// struct FileFilter {
//     std::string name;        // "Image Files"
//     std::vector<std::string> extensions;  // {"png", "jpg", "gif"}
// };
// FileDialogResult loadDialogWithFilter(
//     const std::string& windowTitle,
//     const std::vector<FileFilter>& filters,
//     const std::string& defaultPath = ""
// );

} // namespace trussc

// Alias
namespace tc = trussc;
