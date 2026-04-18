// =============================================================================
// Linux file dialog implementation (GTK3)
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__linux__)

#include <gtk/gtk.h>
#include <cstring>

namespace {

// GTK initialization (once)
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

// Extract filename from path
std::string extractFileName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// Process GTK events
void processGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

} // anonymous namespace

namespace trussc {

// -----------------------------------------------------------------------------
// Alert dialog
// -----------------------------------------------------------------------------
void alertDialog(const std::string& title, const std::string& message) {
    if (!initGtk()) return;

    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s",
        message.c_str()
    );

    if (!title.empty()) {
        gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    }

    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    processGtkEvents();
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
    if (!initGtk()) return false;

    GtkWidget* dialog = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s",
        message.c_str()
    );

    if (!title.empty()) {
        gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    }

    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    processGtkEvents();

    return result == GTK_RESPONSE_YES;
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
    (void)message;  // GTK file chooser doesn't have a message field

    if (!initGtk()) return result;

    GtkFileChooserAction action = folderSelection
        ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
        : GTK_FILE_CHOOSER_ACTION_OPEN;

    const char* acceptLabel = folderSelection ? "_Select" : "_Open";

    std::string dialogTitle = title.empty()
        ? (folderSelection ? "Select Folder" : "Open File")
        : title;

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        dialogTitle.c_str(),
        nullptr,
        action,
        "_Cancel", GTK_RESPONSE_CANCEL,
        acceptLabel, GTK_RESPONSE_ACCEPT,
        nullptr
    );

    if (!defaultPath.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), defaultPath.c_str());
    }

    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

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
    (void)message;  // GTK file chooser doesn't have a message field

    if (!initGtk()) return result;

    std::string dialogTitle = title.empty() ? "Save File" : title;

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        dialogTitle.c_str(),
        nullptr,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        nullptr
    );

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (!defaultPath.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), defaultPath.c_str());
    }
    if (!defaultName.empty()) {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());
    }

    gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);

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

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const std::string& defaultPath,
                     const std::string& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    FileDialogResult result = saveDialog(title, message, defaultPath, defaultName);
    if (callback) callback(result);
}

} // namespace trussc

#endif // __linux__
