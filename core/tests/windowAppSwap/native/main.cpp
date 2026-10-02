// Optional Linux/X11 regression checks for #315; see ../README.md.
#include <TrussC.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <cstdio>
#include <string>
using namespace tc;
using namespace std;
namespace {
string mode;
int fails = 0;
void check(const char* label, bool ok) {
    printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++fails;
}
struct Probe {
    int setups = 0, updates = 0, draws = 0, keys = 0, chars = 0, exits = 0, cleanups = 0;
    bool requested = false;
};
Probe oldP, nextP, victimP;
shared_ptr<tc::Window> target, victim;
struct Next : App {
    void setup() override { ++nextP.setups; }
    void update() override { ++nextP.updates; getWindow()->close(); }
    void exit() override { ++nextP.exits; }
    void cleanup() override { ++nextP.cleanups; }
};
struct Sub : App {
    void setup() override { ++oldP.setups; }
    void request() {
        if (oldP.requested) return;
        oldP.requested = true;
        auto* w = getWindow();
        if (mode.find("swap") != string::npos) w->setApp(make_shared<Next>());
        else w->close();
        check("request preserves App / window / isOpen inside callback",
              w->getApp().get() == this && getWindow() == w && w->isOpen());
    }
    void update() override {
        ++oldP.updates;
        if (mode == "update-close" || mode == "update-swap") request();
    }
    void draw() override {
        ++oldP.draws;
        if (mode == "draw-close" || mode == "draw-swap") request();
    }
    void keyPressed(int) override {
        ++oldP.keys;
        if (mode == "key-close" || mode == "key-swap") request();
    }
    void exit() override {
        ++oldP.exits;
        if (mode == "shutdown-destroy") victim.reset();
        if (mode == "shutdown-chain") victim->close();
        if (target) { target->setApp(nullptr); target->close(); }
    }
    void cleanup() override { ++oldP.cleanups; }
};
struct Victim : App {
    void setup() override { ++victimP.setups; }
    void exit() override { ++victimP.exits; }
    void cleanup() override { ++victimP.cleanups; }
};
struct Main : App {
    bool sent = false;
    EventListener chars;
    void setup() override {
        WindowSettings ws;
        ws.setSize(240, 180);
        ws.title = "request-target";
        ws.decorated = mode != "borderless-close";
        if (mode == "shutdown-chain") {
            victim = createWindow(ws);
            victim->setApp(make_shared<Victim>());
        }
        target = createWindow(ws);
        target->setApp(make_shared<Sub>());
        chars = target->events().rawEvent.listen([](const sapp_event& e) {
            if (e.type == SAPP_EVENTTYPE_CHAR) ++oldP.chars;
        });
        if (mode == "shutdown-destroy") {
            victim = createWindow(ws);
            victim->setApp(make_shared<Victim>());
        }
    }
    void update() override {
        if (mode.find("shutdown-") == 0) {
            // App requests can be applied by an event before the first tick.
            if (target->getApp() && victim->getApp()) exitApp();
            return;
        }
        if (!target->isOpen()) { exitApp(); return; }
        if (!oldP.setups || sent) return;
        if (mode.find("key-") == 0) {
            sent = true;
            auto* d = static_cast<Display*>(const_cast<void*>(sapp_x11_get_display()));
            auto handle = *static_cast<sapp_window*>(target->native_);
            ::Window xw = (uintptr_t)sapp_window_x11_get_window(handle);
            XSetInputFocus(d, xw, RevertToParent, CurrentTime);
            auto key = XKeysymToKeycode(d, XK_q);
            XTestFakeKeyEvent(d, key, True, CurrentTime);
            XTestFakeKeyEvent(d, key, False, CurrentTime);
            XFlush(d);
        } else if (mode == "borderless-close") {
            sent = true;
            target->close();
            check("main App close is deferred", target->isOpen());
        } else if (mode == "destructor-close") {
            sent = true;
            target.reset();
            check("last Window owner tears down immediately", oldP.exits == 1 && oldP.cleanups == 1);
            exitApp();
        }
    }
    void exit() override {
        if (mode.find("shutdown-") == 0) {
            target->close();
            if (mode == "shutdown-destroy") victim->close();
        }
    }
    void cleanup() override {
        check("window App ended before main cleanup",
              mode.find("swap") != string::npos ? nextP.exits == 1 && nextP.cleanups == 1
                                                 : oldP.exits == 1 && oldP.cleanups == 1);
        if (mode == "key-close") check("KEY_DOWN then CHAR survives close request", oldP.keys == 1 && oldP.chars == 1);
        if (mode.find("swap") != string::npos) check("incoming App setup / update once", nextP.setups == 1 && nextP.updates == 1);
        if (mode.find("shutdown-") == 0) check("other window also ended", victimP.exits == 1 && victimP.cleanups == 1);
        target.reset();
        victim.reset();
    }
};
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    mode = argv[1];
    WindowSettings ws;
    ws.setSize(480, 360);
    ws.title = "request-main";
    runApp<Main>(ws);
    return fails ? 1 : 0;
}
