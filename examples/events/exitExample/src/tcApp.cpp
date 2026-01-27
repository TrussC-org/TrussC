// =============================================================================
// exitExample - Demonstrates exit confirmation dialog
// =============================================================================
// Press Q or Escape to request exit
// Click X button to request exit
// A confirmation dialog will appear before closing
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Exit Example");

    // Listen to exitRequested event (new API: returns EventListener)
    exitListener_ = events().exitRequested.listen(this, &tcApp::onExitRequested);
}

void tcApp::onExitRequested(ExitRequestEventArgs& args) {
    // Cancel the exit first (we'll handle it via async dialog)
    args.cancel = true;

    // Show async confirmation dialog (title, message, callback)
    confirmDialogAsync("Confirm Exit", "Are you sure you want to quit?", [](bool confirmed) {
        if (confirmed) exitApp();
    });
}

void tcApp::draw() {
    clear(0.15f);

    setColor(1.0f);
    drawBitmapString("Exit Example", 20, 30);
    drawBitmapString("Press Q or Escape to request exit", 20, 60);
    drawBitmapString("Or click the window close button", 20, 80);
    drawBitmapString("A confirmation dialog will appear", 20, 110);
}

void tcApp::keyPressed(int key) {
    if (key == 'q' || key == 'Q' || key == KEY_ESCAPE) {
        requestExitApp();  // Request exit - triggers exitRequested event
    }
}
