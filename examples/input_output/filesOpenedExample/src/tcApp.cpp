// =============================================================================
// filesOpenedExample - Receive files opened through the operating system.
//
// Retain the filesOpened listener to receive Open With or app-icon file opens.
// Lists the paths and loads the first readable image; delivery is platform-dependent.
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    opened = runtime().events().filesOpened.listen(this, &tcApp::filesOpened);
}

void tcApp::draw() {
    clear(0.15f);
    if (image.isAllocated()) image.draw(0, 0);
    if (files.empty()) {
        drawBitmapStringHighlight("Drop files on the app icon, or use Open With", 20, 20);
    }
    float y = 20;
    for (const auto& file : files) {
        drawBitmapStringHighlight(pathToUtf8(file.filename()), 20, y);
        y += 20;
    }
}

void tcApp::filesOpened(FilesOpenedEventArgs& e) {
    files = e.files;
    for (const auto& file : files) {
        if (image.load(file)) break;
    }
}
