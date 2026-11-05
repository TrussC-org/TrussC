// =============================================================================
// Windows file dialog implementation
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(_WIN32)

#define _WIN32_DCOM
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <locale>
#include <sstream>
#include "TrussC.h"
#include "tc/utils/internal/tcAsyncDialog.h"
#include <memory>
#include <thread>

namespace {

// UTF-8 to Wide string conversion
std::wstring toWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], size);
    return result;
}

// Only native values cross into the detached thread. Resolve Window on main.
struct NativeDialog {
    trussc::internal::DialogKind kind;
    HWND owner = nullptr;
    std::wstring title, message, defaultPath, defaultName;
    std::wstring filterName = L"All Files", filterPattern = L"*.*";
    bool folderSelection = false;
    trussc::internal::DialogCompletion complete;
};

struct ComRelease {
    template<class T> void operator()(T* value) const { value->Release(); }
};
struct ComApartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

trussc::FileDialogResult runDialog(const NativeDialog& native) {
    using trussc::internal::DialogKind;
    ComApartment apartment;
    if (FAILED(apartment.status)) return {};
    trussc::FileDialogResult result;
    if (native.kind == DialogKind::Alert || native.kind == DialogKind::Confirm) {
        const bool confirm = native.kind == DialogKind::Confirm;
        const int response = MessageBoxW(native.owner, native.message.c_str(), native.title.c_str(),
            confirm ? MB_YESNO | MB_ICONQUESTION : MB_OK | MB_ICONINFORMATION);
        result.success = confirm && response == IDYES;
        return result;
    }

    std::unique_ptr<IFileDialog, ComRelease> dialog;
    HRESULT created;
    if (native.kind == DialogKind::Save) {
        IFileSaveDialog* save = nullptr;
        created = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileSaveDialog, reinterpret_cast<void**>(&save));
        dialog.reset(save);
    } else {
        IFileOpenDialog* openDialog = nullptr;
        created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileOpenDialog, reinterpret_cast<void**>(&openDialog));
        dialog.reset(openDialog);
    }
    if (FAILED(created)) return {};
    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options))) return {};
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (native.kind == DialogKind::Save) options |= FOS_OVERWRITEPROMPT;
    else options |= native.folderSelection ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    if (FAILED(dialog->SetOptions(options))) return {};
    if (!native.title.empty() && FAILED(dialog->SetTitle(native.title.c_str()))) return {};
    if (!native.folderSelection) {
        const COMDLG_FILTERSPEC filter{native.filterName.c_str(), native.filterPattern.c_str()};
        if (FAILED(dialog->SetFileTypes(1, &filter))) return {};
    }
    if (!native.defaultPath.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(native.defaultPath.c_str(), nullptr,
                      IID_IShellItem, reinterpret_cast<void**>(&folder)))) {
            std::unique_ptr<IShellItem, ComRelease> ownedFolder(folder);
            if (FAILED(dialog->SetFolder(folder))) return {};
        }
    }
    if (!native.defaultName.empty() && FAILED(dialog->SetFileName(native.defaultName.c_str()))) return {};
    if (FAILED(dialog->Show(native.owner))) return {};
    IShellItem* selected = nullptr;
    if (FAILED(dialog->GetResult(&selected))) return {};
    std::unique_ptr<IShellItem, ComRelease> ownedSelection(selected);
    PWSTR shellPath = nullptr;
    const HRESULT named = selected->GetDisplayName(SIGDN_FILESYSPATH, &shellPath);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> ownedPath(shellPath, &CoTaskMemFree);
    if (FAILED(named)) return {};
    // Use the existing checked UTF-16/UTF-8 conversion, never the active code page.
    const auto pathUtf8 = trussc::pathToUtf8(trussc::fs::path(shellPath));
    result.filePath = trussc::utf8ToPath(pathUtf8);
    result.fileName = result.filePath.filename();
    result.success = true;
    return result;
}

trussc::internal::DialogDismiss presentDialog(const trussc::internal::DialogInputs& inputs,
                                            trussc::internal::DialogCompletion complete) {
    try {
        auto native = std::make_shared<NativeDialog>();
        native->kind = inputs.kind;
        if (auto window = inputs.requestingWindow.lock(); window && window->isOpen()) {
            native->owner = static_cast<HWND>(const_cast<void*>(sapp_window_win32_get_hwnd(
                trussc::internal::WindowRequestAccess::binding(*window).handle)));
        }
        native->title = toWide(inputs.title);
        if (native->title.empty()) {
            if (inputs.kind == trussc::internal::DialogKind::Save) native->title = L"Save File";
            else if (inputs.folderSelection) native->title = L"Select Folder";
        }
        native->message = toWide(inputs.message);
        native->defaultPath = inputs.defaultPath.wstring();
        native->defaultName = inputs.defaultName.wstring();
        native->folderSelection = inputs.folderSelection;
        native->complete = complete;
        std::thread([native] {
            trussc::FileDialogResult result;
            try { result = runDialog(*native); }
            catch (const std::exception&) { result = {}; }
            native->complete(std::move(result)); // Posts to the process-lifetime mailbox only.
        }).detach();
    } catch (const std::exception&) {
        complete({}); // Thread creation/input preparation failure is deferred cancellation too.
    }
    return {}; // Windows dialogs are not programmatically dismissed or joined.
}

} // anonymous namespace

namespace trussc {

// -----------------------------------------------------------------------------
// Alert dialog
// -----------------------------------------------------------------------------
void alertDialog(const std::string& title, const std::string& message) {
    std::wstring titleW = toWide(title);
    std::wstring messageW = toWide(message);
    MessageBoxW(GetActiveWindow(), messageW.c_str(), titleW.c_str(), MB_OK | MB_ICONINFORMATION);
}

void alertDialogAsync(const std::string& title,
                      const std::string& message,
                      std::function<void()> callback) {
    internal::DialogInputs inputs{};
    inputs.kind = internal::DialogKind::Alert; inputs.title = title; inputs.message = message;
    internal::requestAsyncDialog(std::move(inputs), [callback = std::move(callback)](FileDialogResult) {
        if (callback) callback();
    }, presentDialog);
}

// -----------------------------------------------------------------------------
// Confirm dialog
// -----------------------------------------------------------------------------
bool confirmDialog(const std::string& title, const std::string& message) {
    std::wstring titleW = toWide(title);
    std::wstring messageW = toWide(message);
    int result = MessageBoxW(GetActiveWindow(), messageW.c_str(), titleW.c_str(),
                             MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
}

void confirmDialogAsync(const std::string& title,
                        const std::string& message,
                        std::function<void(bool)> callback) {
    internal::DialogInputs inputs{};
    inputs.kind = internal::DialogKind::Confirm; inputs.title = title; inputs.message = message;
    internal::requestAsyncDialog(std::move(inputs), [callback = std::move(callback)](FileDialogResult result) {
        if (callback) callback(result.success);
    }, presentDialog);
}

// -----------------------------------------------------------------------------
// Load dialog
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            bool folderSelection) {
    FileDialogResult result;
    (void)message;  // Windows file dialog doesn't support message

    if (!folderSelection) {
        // File selection dialog
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof(ofn));

        // Bound (platform): the Windows dialog uses MAX_PATH storage. Overflow fails the dialog call, so no path is returned.
        wchar_t szFileName[MAX_PATH] = L"";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = szFileName;
        // Bound (platform): the dialog must fit the MAX_PATH buffer. Overflow fails the dialog call, so no path is returned.
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        std::wstring titleW;
        if (!title.empty()) {
            titleW = toWide(title);
            ofn.lpstrTitle = titleW.c_str();
        }

        std::wstring dirW;
        if (!defaultPath.empty()) {
            dirW = defaultPath.wstring();
            ofn.lpstrInitialDir = dirW.c_str();
        }

        if (GetOpenFileNameW(&ofn)) {
            result.success = true;
            result.filePath = fs::path(szFileName);
            result.fileName = result.filePath.filename();
        }
    } else {
        // フォルダ選択ダイアログ（IFileDialog / モダンUI）
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileOpenDialog* pDialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                     IID_IFileOpenDialog, (void**)&pDialog);
        if (SUCCEEDED(hr)) {
            // フォルダ選択モードに設定
            DWORD options;
            pDialog->GetOptions(&options);
            pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

            // タイトル設定
            std::wstring titleW = toWide(title.empty() ? "Select Folder" : title);
            pDialog->SetTitle(titleW.c_str());

            // デフォルトパス設定
            if (!defaultPath.empty()) {
                std::wstring defaultPathW = defaultPath.wstring();
                IShellItem* pDefaultFolder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(defaultPathW.c_str(), nullptr,
                              IID_IShellItem, (void**)&pDefaultFolder))) {
                    pDialog->SetFolder(pDefaultFolder);
                    pDefaultFolder->Release();
                }
            }

            hr = pDialog->Show(GetActiveWindow());
            if (SUCCEEDED(hr)) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pDialog->GetResult(&pItem))) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                        result.success = true;
                        result.filePath = fs::path(pszPath);
                        result.fileName = result.filePath.filename();
                        CoTaskMemFree(pszPath);
                    }
                    pItem->Release();
                }
            }
            pDialog->Release();
        }
        CoUninitialize();
    }

    return result;
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::DialogInputs inputs{};
    inputs.kind = internal::DialogKind::Load; inputs.title = title; inputs.message = message;
    inputs.defaultPath = defaultPath; inputs.folderSelection = folderSelection;
    internal::requestAsyncDialog(std::move(inputs), [callback = std::move(callback)](FileDialogResult result) {
        if (callback) callback(result);
    }, presentDialog);
}

// -----------------------------------------------------------------------------
// Save dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            const fs::path& defaultName) {
    FileDialogResult result;
    (void)message;  // Windows file dialog doesn't support message

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    // Bound (platform): the Windows save dialog uses MAX_PATH storage. Overflow fails the dialog call, so no path is returned.
    wchar_t szFileName[MAX_PATH] = L"";

    if (!defaultName.empty()) {
        std::wstring nameW = defaultName.wstring();
        // Bound (platform): the supplied default name must fit MAX_PATH storage. Overflow truncates the default name before opening the dialog.
        wcsncpy_s(szFileName, MAX_PATH, nameW.c_str(), _TRUNCATE);
    }

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFileName;
    // Bound (platform): the save dialog must fit the MAX_PATH buffer. Overflow fails the dialog call, so no path is returned.
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

    std::wstring titleW;
    if (!title.empty()) {
        titleW = toWide(title);
    } else {
        titleW = L"Save File";
    }
    ofn.lpstrTitle = titleW.c_str();

    std::wstring dirW;
    if (!defaultPath.empty()) {
        dirW = defaultPath.wstring();
        ofn.lpstrInitialDir = dirW.c_str();
    }

    if (GetSaveFileNameW(&ofn)) {
        result.success = true;
        result.filePath = fs::path(szFileName);
        result.fileName = result.filePath.filename();
    }

    return result;
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     const fs::path& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::DialogInputs inputs{};
    inputs.kind = internal::DialogKind::Save; inputs.title = title; inputs.message = message;
    inputs.defaultPath = defaultPath; inputs.defaultName = defaultName;
    internal::requestAsyncDialog(std::move(inputs), [callback = std::move(callback)](FileDialogResult result) {
        if (callback) callback(result);
    }, presentDialog);
}

} // namespace trussc

#endif // _WIN32
