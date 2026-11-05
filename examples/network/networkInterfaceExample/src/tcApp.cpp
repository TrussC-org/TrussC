// =============================================================================
// tcApp.cpp - Network Interface Example
//
// Lists the host's network interfaces (tc::listNetworkInterfaces) and shows the
// "most likely" LAN address (tc::getLocalIp) — e.g. for telling the user which
// address to point a phone / remote at. Addresses are color-coded with the
// classification helpers (isLoopback / isPrivate / isLinkLocal).
// =============================================================================

#include "tcApp.h"

using namespace std;
using namespace trussc;

void tcApp::setup() {
    refresh();
    printNetworkInterfaces();   // also dump to the log
    logNotice("tcApp") << "Press R to refresh, P to print to log";
}

void tcApp::refresh() {
    interfaces = listNetworkInterfaces();
    localIp = getLocalIp();
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;
    setColor(1.0f);
    drawBitmapString("Network Interfaces", 40, y);
    y += 30;

    // The headline: the address you'd hand out on the LAN.
    setColor(0.4f, 0.9f, 1.0f);
    drawBitmapString("Local IP: " + (localIp.empty() ? string("(none)") : localIp), 40, y);
    y += 36;

    setColor(0.6f);
    drawBitmapString("name            address                                  flags", 40, y);
    y += 22;

    for (const auto& ni : interfaces) {
        // Color by address class.
        if (ni.isLoopback)              setColor(0.5f);              // gray
        else if (isPrivate(ni.address)) setColor(0.5f, 1.0f, 0.5f);  // green: LAN
        else if (isLinkLocal(ni.address)) setColor(1.0f, 0.85f, 0.3f); // yellow
        else                            setColor(1.0f);              // white: public / other

        string flags;
        flags += ni.isIPv4 ? "v4 " : "v6 ";
        if (!ni.isUp)       flags += "down ";
        if (ni.isLoopback)  flags += "loopback ";
        if (!ni.mac.empty() && isLocallyAdministered(ni.mac)) flags += "rand-mac ";

        char line[256];
        snprintf(line, sizeof(line), "%-15s %-40s %s",
                 ni.name.c_str(), ni.address.c_str(), flags.c_str());
        drawBitmapString(line, 40, y);
        y += 18;
    }

    if (interfaces.empty()) {
        setColor(1.0f, 0.6f, 0.6f);
        drawBitmapString("(no interfaces — not available on this platform)", 40, y);
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'R') refresh();
    if (key == 'P') printNetworkInterfaces();
}
