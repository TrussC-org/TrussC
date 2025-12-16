#pragma once

// =============================================================================
// ファイルダイアログ
// OS標準のファイル選択ダイアログを表示
// =============================================================================

#include <string>
#include <vector>

namespace trussc {

// ダイアログの結果
struct FileDialogResult {
    std::string filePath;   // フルパス
    std::string fileName;   // ファイル名のみ
    bool success = false;   // キャンセルでない場合 true
};

// -----------------------------------------------------------------------------
// ファイル選択（開く）ダイアログ
// windowTitle: ダイアログのタイトル（空なら"Open"）
// folderSelection: true ならフォルダ選択モード
// defaultPath: 初期表示パス（空ならホーム）
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(
    const std::string& windowTitle = "",
    bool folderSelection = false,
    const std::string& defaultPath = ""
);

// -----------------------------------------------------------------------------
// ファイル保存ダイアログ
// defaultName: 初期ファイル名
// message: ダイアログに表示するメッセージ
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(
    const std::string& defaultName = "",
    const std::string& message = ""
);

// -----------------------------------------------------------------------------
// アラートダイアログ（メッセージ表示）
// -----------------------------------------------------------------------------
void alertDialog(const std::string& message);

// -----------------------------------------------------------------------------
// 拡張フィルタ付きバージョン（将来用）
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

// エイリアス
namespace tc = trussc;
