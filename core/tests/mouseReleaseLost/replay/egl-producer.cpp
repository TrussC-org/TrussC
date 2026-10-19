// Headless production XI2 producer, based on the round-2 review reproducer.
#define SOKOL_NO_ENTRY
#define SOKOL_GLES3
#define SOKOL_APP_TC_IMPL
#include "sokol_app_tc.h"
#include <cstdio>

// Only cookie acquisition needs a display; keep event construction/dispatch real.
extern "C" Bool XGetEventData(Display*, XGenericEventCookie*) { return True; }
extern "C" void XFreeEventData(Display*, XGenericEventCookie*) {}
static FILE* wire;
static void receive(const sapp_event* e) {
    const unsigned char locked = sapp_mouse_locked();
    std::fwrite(&locked, sizeof(locked), 1, wire);
    std::fwrite(e, sizeof(*e), 1, wire);
    std::printf("EGL producer: type=%d modifiers=%u locked=%d\n", int(e->type), e->modifiers, locked);
}
int main(int argc, char** argv) {
    if (argc != 2 || !(wire = std::fopen(argv[1], "wb"))) return 1;
    _sapp_tc.init_called = true;
    _sapp_tc.desc.event_cb = receive;
    _sapp_tc.mouse.locked = true;
    _sapp_tc.x11.xi.available = true;
    _sapp_tc.x11.xi.major_opcode = 131;
    XEvent press{};
    press.xbutton.button = Button1;
    press.xbutton.x = 10; press.xbutton.y = 20;
    _sapp_tc_x11_on_buttonpress(&press);
    unsigned char mask[1] = {1};
    double delta[1] = {3.0};
    XIRawEvent raw{};
    raw.valuators.mask_len = 1; raw.valuators.mask = mask; raw.raw_values = delta;
    XEvent move{};
    move.xcookie.type = GenericEvent;
    move.xcookie.extension = 131;
    move.xcookie.evtype = XI_RawMotion;
    move.xcookie.data = &raw;
    _sapp_tc_x11_on_genericevent(&move);
    press.xbutton.state = Button1Mask;
    _sapp_tc_x11_on_buttonrelease(&press);
    return std::fclose(wire) != 0;
}
