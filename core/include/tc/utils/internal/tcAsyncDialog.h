#pragma once

#include "../tcFileDialog.h"
#include <memory>

namespace trussc { class Window; }

namespace trussc::internal {

// Internal native boundary. Each presentation owns its inputs and completion.
enum class DialogKind { Alert, Confirm, Load, Save };
struct DialogInputs {
    DialogKind kind;
    std::string title, message;
    fs::path defaultPath, defaultName;
    bool folderSelection = false;
    // Lock and check isOpen() on main before resolving the native owner.
    // Linux retains null native parents.
    std::weak_ptr<Window> requestingWindow;
};
// Confirm uses success; alert ignores the result. Dismissal never calls user code.
using DialogCompletion = std::function<void(FileDialogResult)>;
using DialogDismiss = std::function<void()>;
using DialogPresenter = DialogDismiss (*)(const DialogInputs&, DialogCompletion);
void requestAsyncDialog(DialogInputs, DialogCompletion, DialogPresenter);
void cancelAsyncDialogs();
// Host-owned seam; nullptr restores the platform presenter.
void setDialogPresenterForTests(DialogPresenter);

#if defined(__APPLE__) && TARGET_OS_OSX
// EXIT_PROTOCOL is the only synchronous completion boundary.
bool completingMacDialogsForQuit();
#endif

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
void pumpLinuxDialogs();
#endif

} // namespace trussc::internal
