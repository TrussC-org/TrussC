"""Exercise extension rules and GTK overwrite handling without opening a dialog."""
from pathlib import Path
import os
import shutil
import socket
import time
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DialogFilters(unittest.TestCase):
    @unittest.skipUnless(shutil.which('Xvfb') and shutil.which('pkg-config'), 'requires Xvfb and GTK3')
    def test_real_gtk_filters_and_folder_selection(self):
        source = r'''
#include "core/platform/linux/tcFileDialog_linux.cpp"
#include <cassert>
namespace trussc::internal {
void requestAsyncDialog(DialogInputs, DialogCompletion, DialogPresenter) {}
}
int main(int argc, char**) {
    auto matches = [](GtkFileFilter* filter, const char* name) {
        GtkFileFilterInfo info{};
        info.contains = GtkFileFilterFlags(GTK_FILE_FILTER_FILENAME | GTK_FILE_FILTER_DISPLAY_NAME);
        info.filename = name;
        info.display_name = name;
        return gtk_file_filter_filter(filter, &info);
    };
    GtkFileFilter* patternFilter = gtk_file_filter_new();
    for (const auto& extension : std::vector<std::string>{"PnG", "jpg"})
        gtk_file_filter_add_pattern(patternFilter, trussc::internal::dialogGtkPattern(extension).c_str());
    assert(matches(patternFilter, "image.PNG"));
    assert(matches(patternFilter, "image.pNg"));
    assert(matches(patternFilter, "image.JpG"));
    assert(!matches(patternFilter, "image.txt"));
    g_object_unref(patternFilter);
    if (argc == 1) return 0;
    assert(initGtk());
    GtkWidget* chooser = gtk_file_chooser_dialog_new("test", nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    setFilter(chooser, {"PnG", "jpg"});
    GtkFileFilter* filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(chooser));
    assert(filter);
    assert(matches(filter, "image.PNG"));
    assert(matches(filter, "image.pNg"));
    assert(matches(filter, "image.JpG"));
    assert(!matches(filter, "image.txt"));
    gtk_widget_destroy(chooser);
    using namespace trussc::internal;
    DialogInputs inputs{};
    inputs.kind = DialogKind::Load;
    inputs.folderSelection = true;
    inputs.extensions = {"png"};
    auto dismiss = presentDialog(inputs, [](trussc::FileDialogResult) {});
    GList* windows = gtk_window_list_toplevels();
    bool foundFolder = false;
    for (GList* entry = windows; entry; entry = entry->next) {
        if (!GTK_IS_FILE_CHOOSER(entry->data)) continue;
        auto* folder = GTK_FILE_CHOOSER(entry->data);
        if (gtk_file_chooser_get_action(folder) != GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) continue;
        GSList* filters = gtk_file_chooser_list_filters(folder);
        assert(!filters);
        foundFolder = true;
    }
    g_list_free(windows);
    assert(foundFolder);
    dismiss();
    assert(openDialogs == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix='dialog-gtk-') as temporary:
            folder = Path(temporary)
            cpp = folder / 'test.cpp'
            cpp.write_text(source, encoding='utf-8')
            binary = folder / 'test'
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'gtk+-3.0'], text=True, encoding='utf-8').split()
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT),
                            '-I', str(ROOT / 'core/include'), str(cpp), '-o', str(binary), *flags], check=True)
            subprocess.run([str(binary)], check=True)
            display = ':91656'
            with (folder / 'xvfb.log').open('w', encoding='utf-8') as log:
                server = subprocess.Popen(['Xvfb', display, '-screen', '0', '800x600x24', '-nolisten', 'tcp'],
                                          stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 10
                    while True:
                        if server.poll() is not None:
                            self.skipTest('Xvfb unavailable: ' + (folder / 'xvfb.log').read_text(encoding='utf-8'))
                        try:
                            with socket.socket(socket.AF_UNIX) as client:
                                client.connect('/tmp/.X11-unix/X91656')
                            break
                        except PermissionError as error:
                            self.skipTest('X11 connection unavailable: ' + str(error))
                        except OSError:
                            if time.monotonic() >= deadline:
                                self.fail('Xvfb did not become ready')
                            time.sleep(0.05)
                    subprocess.run([str(binary), "chooser"], env=dict(os.environ, DISPLAY=display), check=True)
                finally:
                    server.terminate()
                    server.wait(timeout=10)

    def test_existing_callers_compile(self):
        flags = subprocess.check_output(['pkg-config', '--cflags', 'gtk+-3.0'],
                                        text=True, encoding='utf-8').split()
        for relative in ('examples/video/videoPlayerExample/src/tcApp.cpp',
                         'examples/input_output/fileDialogExample/src/tcApp.cpp',
                         'examples/input_output/asyncDialogExample/src/tcApp.cpp',
                         'core/tests/asyncDialogs/src/main.cpp'):
            with self.subTest(source=relative):
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only',
                                '-DSOKOL_GLCORE', '-I', str(ROOT / 'core/include'),
                                '-I', str(ROOT / 'core/include/sokol'), *flags,
                                str(ROOT / relative)], check=True)

    def test_overloads_and_existing_calls_compile(self):
        source = r'''
#include "TrussC.h"
using namespace trussc;
class tcApp {
    Image image;
public:
    void keyPressed(int key);
    void setup();
};
void tcApp::keyPressed(int key) {
    if (key == 'o') {
        auto result = loadDialog("Open image", "", {}, false, {"png", "jpg"});
        if (result.success) image.load(result.filePath);
    }
}
void tcApp::setup() {
    saveDialogAsync("Save recording", "", {}, "session.jsonl", {"jsonl"},
                    [](const FileDialogResult& result) {
        if (result.success) logNotice() << "saved to " << result.filePath;
    });
    auto callback = [](const FileDialogResult&) {};
    loadDialog();
    loadDialog("title");
    loadDialog("title", "message");
    loadDialog("title", "message", {});
    loadDialog("title", "message", "", true);
    loadDialog("title", "message", {}, false);
    saveDialog();
    saveDialog("title");
    saveDialog("title", "message");
    saveDialog("title", "message", {});
    saveDialog("title", "message", {}, {});
    saveDialog("title", "message", "", "untitled.txt");
    loadDialogAsync("title", "message", "/tmp", true, callback);
    loadDialogAsync("title", "message", {}, false, callback);
    saveDialogAsync("title", "message", "/tmp", "name.txt", callback);
    saveDialogAsync("title", "message", {}, {}, callback);
    loadDialog("title", "", {}, true, {"png"});
    saveDialog("title", "", {}, "session", {"jsonl"});
    loadDialogAsync("title", "", {}, false, {"png"}, callback);
    saveDialogAsync("title", "", {}, {}, {}, callback);
    loadDialogAsync("title", "", {}, false, {}, nullptr);
}
'''
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only',
                        '-DSOKOL_GLCORE', '-I', str(ROOT / 'core/include'),
                        '-I', str(ROOT / 'core/include/sokol'), '-x', 'c++', '-'],
                       input=source, text=True, encoding='utf-8', check=True)

    def test_matching_patterns_and_overwrite(self):
        linux = (ROOT / 'core/platform/linux/tcFileDialog_linux.cpp').read_text(encoding='utf-8')
        finish = linux[linux.index('trussc::FileDialogResult finishSave('):linux.index('struct NativeDialog')]
        mac = (ROOT / 'core/platform/mac/tcFileDialog_mac.mm').read_text(encoding='utf-8')
        mac_finish = mac[mac.index('FileDialogResult finishSave('):mac.index('void setFilter(')]
        source = r'''
#include "tc/utils/internal/tcDialogFilter.h"
#include "tc/utils/tcFileDialog.h"
#include <cassert>
#include <fstream>
namespace trussc {
int confirmations = 0;
bool answer = false;
fs::path expected;
bool confirmDialog(const std::string&, const std::string& message) {
    ++confirmations;
    assert(message.find(expected.string()) != std::string::npos);
    return answer;
}
}
''' + finish + '\nnamespace trussc {\n' + mac_finish + '\n}\n' + r'''
int main(int argc, char** argv) {
    using namespace trussc;
    using namespace trussc::internal;
    assert(argc == 2);
    assert(dialogExtensionMatches("image.PNG", {"png", "jpg"}));
    assert(dialogExtensionMatches("image.pNg", {"PNG"}));
    assert(dialogExtensionMatches("image.JPEG", {"png", "jpeg"}));
    assert(!dialogExtensionMatches("image.png.txt", {"png"}));
    assert(!dialogExtensionMatches("imagepng", {"png"}));
    assert(dialogExtensionMatches("anything", {}));
    assert(dialogSaveName("session.JSONL", {"jsonl"}) == "session.JSONL");
    assert(dialogSaveName("session", {"jsonl"}) == "session.jsonl");
    assert(dialogSaveName("session.txt", {"jsonl", "txt"}) == "session.txt");
    assert(dialogSaveName("session.txt", {"jsonl"}) == "session.txt.jsonl");
    assert(dialogSaveName("session", {}) == "session");
    assert(dialogGtkPattern("PnG") == "*.[pP][nN][gG]");
    assert(dialogGtkPattern("mp3") == "*.[mM][pP]3");
    assert(dialogWindowsPattern({"PNG", "jpg"}) == "*.png;*.jpg");
    assert(dialogWindowsPattern({}) == "*.*");
    const fs::path directory = argv[1];
    FileDialogResult selected{directory / "test5", "test5", true};
    expected = directory / "test5.jsonl";
    auto result = ::finishSave(selected, {"jsonl"});
    assert(result.success && result.filePath == expected && result.fileName == "test5.jsonl");
    assert(confirmations == 0);
    std::ofstream(expected) << "keep";
    result = ::finishSave(selected, {"jsonl"});
    assert(!result.success && result.filePath.empty() && confirmations == 1);
    answer = true;
    result = ::finishSave(selected, {"jsonl"});
    assert(result.success && result.filePath == expected && confirmations == 2);
    std::string contents;
    std::ifstream(expected) >> contents;
    assert(contents == "keep");
    result = ::finishSave(selected, {});
    assert(result.filePath == selected.filePath && confirmations == 2);
    result = ::finishSave({}, {"jsonl"});
    assert(!result.success && confirmations == 2);
    expected = directory / "test5.JSONL";
    std::ofstream(expected) << "keep";
    result = ::finishSave({expected, expected.filename(), true}, {"jsonl"});
    assert(result.success && result.filePath == expected && confirmations == 3);
    expected = directory / "image.jpeg.jpg";
    std::ofstream(expected) << "keep";
    answer = false;
    result = trussc::finishSave({directory / "image.jpeg", "image.jpeg", true}, {"jpg"});
    assert(!result.success && confirmations == 4);
    answer = true;
    result = trussc::finishSave({directory / "image.jpeg", "image.jpeg", true}, {"jpg"});
    assert(result.success && result.filePath == expected && result.fileName == "image.jpeg.jpg" && confirmations == 5);
    result = trussc::finishSave({directory / "image.JPG", "image.JPG", true}, {"jpg"});
    assert(result.success && result.fileName == "image.JPG" && confirmations == 5);
}
'''
        with tempfile.TemporaryDirectory(prefix='dialog-filter-') as temporary:
            folder = Path(temporary)
            cpp = folder / 'test.cpp'
            cpp.write_text(source, encoding='utf-8')
            binary = folder / 'test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT / 'core/include'),
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(folder)], check=True)


if __name__ == '__main__':
    unittest.main()
