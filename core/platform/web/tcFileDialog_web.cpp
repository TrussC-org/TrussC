// =============================================================================
// Web/WASM file dialog implementation
// =============================================================================

#include "TrussC.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace trussc {

// JS alert binding
EM_JS(void, js_alert, (const char* str), {
    alert(UTF8ToString(str));
});

// JS confirm binding
EM_JS(bool, js_confirm, (const char* str), {
    return confirm(UTF8ToString(str));
});

// -----------------------------------------------------------------------------
// Alert dialog
// -----------------------------------------------------------------------------
void alertDialog(const std::string& title, const std::string& message) {
    // Web alert() doesn't support title, concatenate them
    std::string fullMessage = title.empty() ? message : (title + "\n\n" + message);
    js_alert(fullMessage.c_str());
}

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback) {
    alertDialog(title, message);
    if (callback) callback();
}

// -----------------------------------------------------------------------------
// Confirm dialog
// -----------------------------------------------------------------------------
bool confirmDialog(const std::string& title, const std::string& message) {
    // Web confirm() doesn't support title, concatenate them
    std::string fullMessage = title.empty() ? message : (title + "\n\n" + message);
    return js_confirm(fullMessage.c_str());
}

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    bool result = confirmDialog(title, message);
    if (callback) callback(result);
}

// -----------------------------------------------------------------------------
// Load dialog (not supported on Web)
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            bool folderSelection) {
    (void)title; (void)message; (void)defaultPath; (void)folderSelection;
    logWarning("tcFileDialog") << "loadDialog is not supported on Web/WASM";
    return FileDialogResult();
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    FileDialogResult result = loadDialog(title, message, defaultPath, folderSelection);
    if (callback) callback(result);
}

// -----------------------------------------------------------------------------
// Save dialog (not supported on Web)
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            const std::string& defaultName) {
    (void)title; (void)message; (void)defaultPath; (void)defaultName;
    logWarning("tcFileDialog") << "saveDialog is not supported on Web/WASM";
    return FileDialogResult();
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    FileDialogResult result = saveDialog(title, message, defaultPath, defaultName);
    if (callback) callback(result);
}

} // namespace trussc

#endif // __EMSCRIPTEN__
