# Recipes

Recipes introduces a phone control panel, a CLI tool, and a resident app.
[日本語](RECIPES_jp.md) · [GET_STARTED.md › Get Started](GET_STARTED.md)

## Control a fullscreen app from a phone

A phone on the same trusted LAN can change a fullscreen app's color and brightness. [phonePanelExample › README.md](../examples/network/phonePanelExample/README.md) explains how to build it, open the on-screen URL, and handle the HTTP token and input bounds. The example includes the page, main-thread updates, and a scrolling log.

## CLI tool

A CLI tool can convert a hex color to TrussC's normalized RGB values, print the result, and exit with a status code. [colorToolExample › README.md](../examples/utils/colorToolExample/README.md) shows argument handling in `tcApp.cpp` and a GUI fallback when no arguments are given, following `trusscli`'s pattern in [tools/src/main.cpp](../tools/src/main.cpp).

## Resident app

A resident app can check for background work a few times per second and keep a file log. Use a headless app with `setup()` and `update()`:

```cpp
#include <TrussC.h>
using namespace tc;

class tcApp : public App {
public:
    void setup() override {
        if (!setLogFile("watcher.log")) {
            logError("watcher") << "Could not open log file";
            exitApp();
        }
    }
    void update() override {
        // Check for work here.
    }
};

int main() {
    HeadlessSettings settings;
    settings.setFps(2);
    return runHeadlessApp<tcApp>(settings);
}
```
