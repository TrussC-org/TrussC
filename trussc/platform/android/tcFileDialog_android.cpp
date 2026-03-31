// =============================================================================
// Android file dialog implementation (stub)
// =============================================================================
// Android has no native file dialog accessible from NativeActivity.
// A real implementation would need JNI to invoke Intent.ACTION_OPEN_DOCUMENT.
// For now, all dialogs return empty/failure.
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#ifdef __ANDROID__

#pragma message("STUB: tcFileDialog_android.cpp — needs JNI Intent implementation")

#include <android/log.h>

namespace trussc {

void alertDialog(const std::string& title, const std::string& message) {
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
    __android_log_print(ANDROID_LOG_INFO, "TrussC", "Confirm: %s - %s",
                        title.c_str(), message.c_str());
    return false;
}

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    if (callback) callback(false);
}

FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            bool folderSelection) {
    (void)title; (void)message; (void)defaultPath; (void)folderSelection;
    return FileDialogResult{};
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    if (callback) callback(FileDialogResult{});
}

FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            const std::string& defaultName) {
    (void)title; (void)message; (void)defaultPath; (void)defaultName;
    return FileDialogResult{};
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    if (callback) callback(FileDialogResult{});
}

} // namespace trussc

#endif // __ANDROID__
