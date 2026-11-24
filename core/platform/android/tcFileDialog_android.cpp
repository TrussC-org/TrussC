// =============================================================================
// Android file dialog implementation (stub)
// =============================================================================
// Android has no native file dialog accessible from NativeActivity.
// A real implementation would need JNI to invoke Intent.ACTION_OPEN_DOCUMENT.
// For now, all dialogs return empty/failure.
// =============================================================================

#include "tc/utils/tcFileDialog.h"
#include "tc/utils/tcLog.h"

#ifdef __ANDROID__

// Not implemented on Android yet (#1113): needs JNI Intent implementation.

#include <android/log.h>

namespace trussc {

void alertDialog(const std::string& title, const std::string& message) {
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    __android_log_print(ANDROID_LOG_INFO, "TrussC", "Alert: %s - %s",
                        title.c_str(), message.c_str());
}

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback) {
    alertDialog(title, message);
    if (callback) callback();
}

bool confirmDialog(const std::string& title, const std::string& message) {
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    __android_log_print(ANDROID_LOG_INFO, "TrussC", "Confirm: %s - %s",
                        title.c_str(), message.c_str());
    return false;
}

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    (void)title; (void)message;
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    if (callback) callback(false);
}

FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            bool folderSelection,
                            const std::vector<std::string>& extensions) {
    (void)extensions;
    (void)title; (void)message; (void)defaultPath; (void)folderSelection;
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    return FileDialogResult{};
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     bool folderSelection,
                     const std::vector<std::string>& extensions,
                     std::function<void(const FileDialogResult&)> callback) {
    (void)extensions;
    (void)title; (void)message; (void)defaultPath; (void)folderSelection;
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    if (callback) callback(FileDialogResult{});
}

FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            const fs::path& defaultName,
                            const std::vector<std::string>& extensions) {
    (void)extensions;
    (void)title; (void)message; (void)defaultPath; (void)defaultName;
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    return FileDialogResult{};
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     const fs::path& defaultName,
                     const std::vector<std::string>& extensions,
                     std::function<void(const FileDialogResult&)> callback) {
    (void)extensions;
    (void)title; (void)message; (void)defaultPath; (void)defaultName;
    logWarning("FileDialog") << "file dialogs are not available on Android yet";
    if (callback) callback(FileDialogResult{});
}


// Preserve the original entry points and their unfiltered behavior.
FileDialogResult loadDialog(const std::string& title, const std::string& message,
                            const fs::path& defaultPath, bool folderSelection) {
    return loadDialog(title, message, defaultPath, folderSelection, {});
}
void loadDialogAsync(const std::string& title, const std::string& message,
                     const fs::path& defaultPath, bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    loadDialogAsync(title, message, defaultPath, folderSelection, {}, std::move(callback));
}
FileDialogResult saveDialog(const std::string& title, const std::string& message,
                            const fs::path& defaultPath, const fs::path& defaultName) {
    return saveDialog(title, message, defaultPath, defaultName, {});
}
void saveDialogAsync(const std::string& title, const std::string& message,
                     const fs::path& defaultPath, const fs::path& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    saveDialogAsync(title, message, defaultPath, defaultName, {}, std::move(callback));
}

} // namespace trussc

#endif // __ANDROID__
