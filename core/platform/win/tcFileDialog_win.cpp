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

namespace {

// UTF-8 to Wide string conversion
std::wstring toWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], size);
    return result;
}

// Wide string to UTF-8 conversion
std::string toUtf8(const wchar_t* wstr) {
    if (!wstr || *wstr == L'\0') return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Extract filename from path
std::string extractFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
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
    alertDialog(title, message);
    if (callback) callback();
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
    bool result = confirmDialog(title, message);
    if (callback) callback(result);
}

// -----------------------------------------------------------------------------
// Load dialog
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            bool folderSelection) {
    FileDialogResult result;
    (void)message;  // Windows file dialog doesn't support message

    if (!folderSelection) {
        // File selection dialog
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof(ofn));

        wchar_t szFileName[MAX_PATH] = L"";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = szFileName;
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
            dirW = toWide(defaultPath);
            ofn.lpstrInitialDir = dirW.c_str();
        }

        if (GetOpenFileNameW(&ofn)) {
            result.success = true;
            result.filePath = toUtf8(szFileName);
            result.fileName = extractFileName(result.filePath);
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
                std::wstring defaultPathW = toWide(defaultPath);
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
                        result.filePath = toUtf8(pszPath);
                        result.fileName = extractFileName(result.filePath);
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
                     const std::string& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    FileDialogResult result = loadDialog(title, message, defaultPath, folderSelection);
    if (callback) callback(result);
}

// -----------------------------------------------------------------------------
// Save dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const std::string& defaultPath,
                            const std::string& defaultName) {
    FileDialogResult result;
    (void)message;  // Windows file dialog doesn't support message

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    wchar_t szFileName[MAX_PATH] = L"";

    if (!defaultName.empty()) {
        std::wstring nameW = toWide(defaultName);
        wcsncpy_s(szFileName, MAX_PATH, nameW.c_str(), _TRUNCATE);
    }

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFileName;
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
        dirW = toWide(defaultPath);
        ofn.lpstrInitialDir = dirW.c_str();
    }

    if (GetSaveFileNameW(&ofn)) {
        result.success = true;
        result.filePath = toUtf8(szFileName);
        result.fileName = extractFileName(result.filePath);
    }

    return result;
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

#endif // _WIN32
