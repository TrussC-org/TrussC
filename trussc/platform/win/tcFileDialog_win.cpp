// =============================================================================
// Windows ファイルダイアログ実装
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(_WIN32)

#define _WIN32_DCOM
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <locale>
#include <sstream>

namespace {

// UTF-8 から Wide文字列への変換
std::wstring toWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], size);
    return result;
}

// Wide文字列から UTF-8 への変換
std::string toUtf8(const wchar_t* wstr) {
    if (!wstr || *wstr == L'\0') return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
    return result;
}

// パスからファイル名を抽出
std::string extractFileName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// フォルダ選択ダイアログのコールバック
static int CALLBACK browseCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData != 0) {
        std::wstring* defaultPath = reinterpret_cast<std::wstring*>(lpData);
        if (!defaultPath->empty()) {
            SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)defaultPath->c_str());
        }
    }
    return 0;
}

} // anonymous namespace

namespace trussc {

// -----------------------------------------------------------------------------
// ファイル選択（開く）ダイアログ
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(
    const std::string& windowTitle,
    bool folderSelection,
    const std::string& defaultPath
) {
    FileDialogResult result;

    if (!folderSelection) {
        // ファイル選択ダイアログ
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof(ofn));

        wchar_t szFileName[MAX_PATH] = L"";
        wchar_t szDir[MAX_PATH] = L"";

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFile = szFileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

        // タイトル設定
        std::wstring titleW;
        if (!windowTitle.empty()) {
            titleW = toWide(windowTitle);
            ofn.lpstrTitle = titleW.c_str();
        }

        // 初期ディレクトリ設定
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
        // フォルダ選択ダイアログ
        BROWSEINFOW bi;
        ZeroMemory(&bi, sizeof(bi));

        wchar_t szDisplayName[MAX_PATH] = L"";

        bi.hwndOwner = GetActiveWindow();
        bi.pszDisplayName = szDisplayName;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

        // タイトル設定
        std::wstring titleW;
        if (!windowTitle.empty()) {
            titleW = toWide(windowTitle);
        } else {
            titleW = L"Select Folder";
        }
        bi.lpszTitle = titleW.c_str();

        // 初期パス設定
        std::wstring defaultPathW;
        if (!defaultPath.empty()) {
            defaultPathW = toWide(defaultPath);
            bi.lpfn = browseCallback;
            bi.lParam = (LPARAM)&defaultPathW;
        }

        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t szPath[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, szPath)) {
                result.success = true;
                result.filePath = toUtf8(szPath);
                result.fileName = extractFileName(result.filePath);
            }
            CoTaskMemFree(pidl);
        }
    }

    return result;
}

// -----------------------------------------------------------------------------
// ファイル保存ダイアログ
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(
    const std::string& defaultName,
    const std::string& message
) {
    FileDialogResult result;

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));

    wchar_t szFileName[MAX_PATH] = L"";

    // 初期ファイル名を設定
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

    // タイトル（メッセージ）設定
    std::wstring titleW;
    if (!message.empty()) {
        titleW = toWide(message);
    } else {
        titleW = L"Save File";
    }
    ofn.lpstrTitle = titleW.c_str();

    if (GetSaveFileNameW(&ofn)) {
        result.success = true;
        result.filePath = toUtf8(szFileName);
        result.fileName = extractFileName(result.filePath);
    }

    return result;
}

// -----------------------------------------------------------------------------
// アラートダイアログ
// -----------------------------------------------------------------------------
void alertDialog(const std::string& message) {
    std::wstring messageW = toWide(message);
    MessageBoxW(GetActiveWindow(), messageW.c_str(), L"Alert", MB_OK | MB_ICONINFORMATION);
}

} // namespace trussc

#endif // _WIN32
