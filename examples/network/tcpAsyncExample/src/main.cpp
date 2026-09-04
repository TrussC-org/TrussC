// =============================================================================
// main.cpp - Asynchronous TCP send sample
// =============================================================================

#include "tcApp.h"

int main() {
    WindowSettings settings;
    settings.setSize(960, 600);
    settings.setTitle("tcpAsyncExample - sendAsync vs send");

    return TC_RUN_APP(tcApp, settings);
}
