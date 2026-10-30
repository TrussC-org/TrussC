// =============================================================================
// exitExample - Demonstrates exit confirmation dialog
// =============================================================================
// Press Q or Escape to request exit
// Click X to query closeRequested; process quit queries exitRequested
// A confirmation dialog will appear before closing
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("Exit Example");

    getWindow()->setQuitOnClose(true);
    // App code uses events(); non-App code (addons, app-wide handling)
    // uses runtime().events(). See main.cpp for that subscription.
    exitListener_ = events().exitRequested.listen(this, &tcApp::onExitRequested);
    closeListener_ = events().closeRequested.listen([this](CloseRequestEventArgs& e) {
        e.cancel = !quitConfirmed_;
        if (e.cancel) confirmExit();
    });
}

void tcApp::onExitRequested(ExitRequestEventArgs& args) {
    args.cancel = !quitConfirmed_;
    if (args.cancel) confirmExit();
}

void tcApp::confirmExit() {
    if (confirmationPending_) return;
    confirmationPending_ = true;
    confirmDialogAsync("Confirm Exit", "Are you sure you want to quit?", [this](bool confirmed) {
        confirmationPending_ = false;
        if (confirmed) { quitConfirmed_ = true; exitApp(); }
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
    if (key == 'Q' || key == KEY_ESCAPE) {
        requestExitApp();  // Request exit - triggers exitRequested event
    }
}
