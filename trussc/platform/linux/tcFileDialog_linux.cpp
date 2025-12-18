// =============================================================================
// Linux ファイルダイアログ実装 (GTK3)
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__linux__)

#include <gtk/gtk.h>
#include <cstring>

namespace {

// GTK初期化（一度だけ）
bool initGtk() {
    static bool initialized = false;
    if (!initialized) {
        int argc = 0;
        char** argv = nullptr;
        if (!gtk_init_check(&argc, &argv)) {
            return false;
        }
        initialized = true;
    }
    return true;
}

// パスからファイル名を抽出
std::string extractFileName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// GTK イベントを処理
void processGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
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

    if (!initGtk()) {
        return result;
    }

    // ダイアログのアクションを決定
    GtkFileChooserAction action = folderSelection
        ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
        : GTK_FILE_CHOOSER_ACTION_OPEN;

    // ボタンラベル
    const char* acceptLabel = folderSelection ? "_Select" : "_Open";

    // タイトル
    std::string title = windowTitle.empty()
        ? (folderSelection ? "Select Folder" : "Open File")
        : windowTitle;

    // ダイアログ作成
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        title.c_str(),
        nullptr,
        action,
        "_Cancel", GTK_RESPONSE_CANCEL,
        acceptLabel, GTK_RESPONSE_ACCEPT,
        nullptr
    );

    // 初期パス設定
    if (!defaultPath.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), defaultPath.c_str());
    }

    // ダイアログを前面に
    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

    // ダイアログ実行
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            result.success = true;
            result.filePath = filename;
            result.fileName = extractFileName(result.filePath);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
    processGtkEvents();

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

    if (!initGtk()) {
        return result;
    }

    // タイトル
    std::string title = message.empty() ? "Save File" : message;

    // ダイアログ作成
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        title.c_str(),
        nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        nullptr
    );

    // 上書き確認を有効化
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    // 初期ファイル名設定
    if (!defaultName.empty()) {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());
    }

    // ダイアログを前面に
    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

    // ダイアログ実行
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            result.success = true;
            result.filePath = filename;
            result.fileName = extractFileName(result.filePath);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
    processGtkEvents();

    return result;
}

// -----------------------------------------------------------------------------
// アラートダイアログ
// -----------------------------------------------------------------------------
void alertDialog(const std::string& message) {
    if (!initGtk()) {
        return;
    }

    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s",
        message.c_str()
    );

    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    processGtkEvents();
}

} // namespace trussc

#endif // __linux__
