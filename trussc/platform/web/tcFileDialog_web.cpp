#include "TrussC.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace trussc {

// JS alert binding
EM_JS(void, js_alert, (const char* str), {
    alert(UTF8ToString(str));
});

FileDialogResult loadDialog(const std::string& windowTitle, bool folderSelection, const std::string& defaultPath) {
    logWarning("tcFileDialog") << "loadDialog is not supported on Web/WASM (sync/blocking UI is impossible).";
    return FileDialogResult(); // success = false
}

FileDialogResult saveDialog(const std::string& defaultName, const std::string& message) {
    logWarning("tcFileDialog") << "saveDialog is not supported on Web/WASM.";
    return FileDialogResult();
}

void alertDialog(const std::string& message) {
    js_alert(message.c_str());
}

} // namespace trussc

#endif
