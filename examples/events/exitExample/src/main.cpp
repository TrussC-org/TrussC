// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    // App code uses events(); non-App code (addons, app-wide handling)
    // uses runtime().events(). This listener runs before every App listener.
    auto processQuit = tc::runtime().events().exitRequested.listen([](tc::ExitRequestEventArgs&) {
        tc::logNotice() << "Process quit requested";
    });
    tc::WindowSettings settings;
    settings.setSize(480, 200);

    return TC_RUN_APP(tcApp, settings);
}
