// =============================================================================
// Linux file dialog implementation (GTK3)
// =============================================================================

#include "tc/utils/tcFileDialog.h"

#if defined(__linux__)

#include <gtk/gtk.h>
#include <cstring>
#include "tc/utils/internal/tcAsyncDialog.h"
#include <memory>

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

// Process GTK events
void processGtkEvents() {
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

struct NativeDialog {
    std::shared_ptr<GtkWidget*> handle;
    trussc::internal::DialogKind kind;
    trussc::internal::DialogCompletion complete;
};
size_t openDialogs = 0;
bool cleanupPending = false;

void dialogDestroyed(GtkWidget*, gpointer data) {
    std::unique_ptr<NativeDialog> dialog(static_cast<NativeDialog*>(data));
    *dialog->handle = nullptr;
    --openDialogs;
    cleanupPending = true;
    // A window-manager close/destroy without a response is cancellation too.
    if (dialog->complete) dialog->complete({});
}
void dialogResponse(GtkDialog* widget, gint response, gpointer data) {
    auto* dialog = static_cast<NativeDialog*>(data);
    trussc::FileDialogResult result;
    if (dialog->kind == trussc::internal::DialogKind::Confirm) {
        result.success = response == GTK_RESPONSE_YES;
    } else if ((dialog->kind == trussc::internal::DialogKind::Load ||
                dialog->kind == trussc::internal::DialogKind::Save) && response == GTK_RESPONSE_ACCEPT) {
        if (char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget))) {
            result.filePath = filename;
            result.fileName = result.filePath.filename();
            result.success = true;
            g_free(filename);
        }
    }
    auto complete = std::move(dialog->complete);
    gtk_widget_destroy(GTK_WIDGET(widget));
    if (complete) complete(std::move(result));
}

trussc::internal::DialogDismiss presentDialog(const trussc::internal::DialogInputs& inputs,
                                            trussc::internal::DialogCompletion complete) {
    using trussc::internal::DialogKind;
    if (!initGtk()) { complete({}); return {}; }
    GtkWidget* widget;
    if (inputs.kind == DialogKind::Alert || inputs.kind == DialogKind::Confirm) {
        const bool confirm = inputs.kind == DialogKind::Confirm;
        widget = gtk_message_dialog_new(nullptr, GtkDialogFlags(0),
            confirm ? GTK_MESSAGE_QUESTION : GTK_MESSAGE_INFO,
            confirm ? GTK_BUTTONS_YES_NO : GTK_BUTTONS_OK, "%s", inputs.message.c_str());
        if (!inputs.title.empty()) gtk_window_set_title(GTK_WINDOW(widget), inputs.title.c_str());
    } else {
        const bool save = inputs.kind == DialogKind::Save;
        const auto action = save ? GTK_FILE_CHOOSER_ACTION_SAVE :
            inputs.folderSelection ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
        const std::string title = inputs.title.empty() ?
            (save ? "Save File" : inputs.folderSelection ? "Select Folder" : "Open File") : inputs.title;
        widget = gtk_file_chooser_dialog_new(title.c_str(), nullptr, action,
            "_Cancel", GTK_RESPONSE_CANCEL,
            save ? "_Save" : inputs.folderSelection ? "_Select" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        if (!inputs.defaultPath.empty())
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widget), inputs.defaultPath.c_str());
        if (save) {
            // GTK's internal overwrite confirmation still blocks (owner-approved).
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(widget), TRUE);
            if (!inputs.defaultName.empty())
                gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(widget), inputs.defaultName.c_str());
        }
    }
    auto handle = std::make_shared<GtkWidget*>(widget);
    auto* dialog = new NativeDialog{handle, inputs.kind, std::move(complete)};
    g_signal_connect(widget, "response", G_CALLBACK(dialogResponse), dialog);
    g_signal_connect(widget, "destroy", G_CALLBACK(dialogDestroyed), dialog);
    ++openDialogs;
    gtk_window_set_keep_above(GTK_WINDOW(widget), TRUE);
    gtk_widget_show_all(widget);
    return [handle] { if (*handle) gtk_widget_destroy(*handle); };
}

} // anonymous namespace

namespace trussc {
namespace internal {
void pumpLinuxDialogs() {
    if (!openDialogs && !cleanupPending) return;
    // Bound (responsiveness): service at most 64 ready GTK iterations so the host turn can progress.
    for (int iteration = 0; iteration < 64 && gtk_events_pending(); ++iteration)
        gtk_main_iteration_do(FALSE);
    cleanupPending = gtk_events_pending();
}
} // namespace internal


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
    internal::requestAsyncDialog({internal::DialogKind::Alert, title, message, {}, {}},
        [callback = std::move(callback)](FileDialogResult) { if (callback) callback(); }, presentDialog);
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
    internal::requestAsyncDialog({internal::DialogKind::Confirm, title, message, {}, {}},
        [callback = std::move(callback)](FileDialogResult result) { if (callback) callback(result.success); }, presentDialog);
}

// -----------------------------------------------------------------------------
// Load dialog
// -----------------------------------------------------------------------------
FileDialogResult loadDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
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
            result.fileName = result.filePath.filename();
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
    processGtkEvents();

    return result;
}

void loadDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     bool folderSelection,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Load, title, message, defaultPath, {}, folderSelection},
        std::move(callback), presentDialog);
}

// -----------------------------------------------------------------------------
// Save dialog
// -----------------------------------------------------------------------------
FileDialogResult saveDialog(const std::string& title,
                            const std::string& message,
                            const fs::path& defaultPath,
                            const fs::path& defaultName) {
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
            result.fileName = result.filePath.filename();
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);
    processGtkEvents();

    return result;
}

void saveDialogAsync(const std::string& title,
                     const std::string& message,
                     const fs::path& defaultPath,
                     const fs::path& defaultName,
                     std::function<void(const FileDialogResult&)> callback) {
    internal::requestAsyncDialog({internal::DialogKind::Save, title, message, defaultPath, defaultName},
        std::move(callback), presentDialog);
}

} // namespace trussc

#endif // __linux__
