// Real X11 backend fixture: synthetic XDND and independent clipboard clients.
#define SOKOL_APP_TC_IMPL
#define SOKOL_NO_ENTRY
#include "sokol/sokol_app_tc.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

namespace {
std::vector<std::string> expected_paths;
int delivered = 0;

XEvent next_event(Display* display) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!XPending(display)) {
        assert(std::chrono::steady_clock::now() < deadline && "X11 peer did not respond");
        pollfd descriptor{ConnectionNumber(display), POLLIN, 0};
        poll(&descriptor, 1, 10);
    }
    XEvent event;
    XNextEvent(display, &event);
    return event;
}

void paste_from_peer(const std::string& text, bool use_incremental, bool is_drop = false);

void drops() {
    std::string uris;
    for (int i = 0; i < 20; ++i) {
        std::string path = "/tmp/drop " + std::to_string(i) + "-日本語";
        if (i == 19) path += std::string(6000, 'z');
        expected_paths.push_back(path);
        std::string encoded;
        for (unsigned char byte : path) {
            if (byte == ' ' || byte >= 128) {
                const char* hex = "0123456789ABCDEF";
                encoded += '%'; encoded += hex[byte >> 4]; encoded += hex[byte & 15];
            } else encoded += (char)byte;
        }
        uris += "file://" + encoded;
        uris += "\r\n";
    }
    const Window window = _sapp_tc.app.main->xwin;
    XChangeProperty(_sapp_tc.display, window, _sapp_tc.xdnd.XdndSelection,
        _sapp_tc.xdnd.text_uri_list, 8, PropModeReplace,
        (const unsigned char*)uris.data(), (int)uris.size());
    XEvent event = {};
    event.xselection.type = SelectionNotify;
    event.xselection.requestor = window;
    event.xselection.property = _sapp_tc.xdnd.XdndSelection;
    event.xselection.target = _sapp_tc.xdnd.text_uri_list;
    _sapp_tc_x11_process_event(&event);
    assert(delivered == 1);
    assert(sapp_get_num_dropped_files() == 20);
    assert(expected_paths.back().size() > 2048);
    assert(std::string(sapp_get_dropped_file_path(19)) == expected_paths.back());
    assert(std::string(sapp_get_dropped_file_path(20)).empty());
    paste_from_peer(uris, true, true);
    assert(delivered == 2);
}

void paste_from_peer(const std::string& text, bool use_incremental, bool is_drop) {
    Display* peer = XOpenDisplay(nullptr); assert(peer);
    Window window = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0, 0, 1, 1, 0, 0, 0);
    const Atom selection = is_drop ? _sapp_tc.xdnd.XdndSelection : _sapp_tc.CLIPBOARD;
    const Atom target = is_drop ? _sapp_tc.xdnd.text_uri_list : _sapp_tc.UTF8_STRING;
    const Atom property = is_drop ? selection : _sapp_tc.SAPP_TC_SELECTION;
    XSetSelectionOwner(peer, selection, window, CurrentTime);
    XSync(peer, False);
    std::thread owner([&] {
        XEvent event;
        do { event = next_event(peer); } while (event.type != SelectionRequest);
        const auto request = event.xselectionrequest;
        XSelectionEvent reply = {};
        reply.type = SelectionNotify; reply.display = peer;
        reply.requestor = request.requestor; reply.selection = request.selection;
        reply.target = request.target; reply.property = request.property; reply.time = request.time;
        if (use_incremental) {
            XSelectInput(peer, request.requestor, PropertyChangeMask);
            const unsigned long size = text.size();
            XChangeProperty(peer, request.requestor, request.property, XInternAtom(peer, "INCR", False),
                32, PropModeReplace, (const unsigned char*)&size, 1);
        } else {
            XChangeProperty(peer, request.requestor, request.property, request.target, 8,
                PropModeReplace, (const unsigned char*)text.data(), (int)text.size());
        }
        XSendEvent(peer, reply.requestor, False, 0, (XEvent*)&reply);
        XFlush(peer);
        if (use_incremental) {
            size_t offset = 0;
            for (;;) {
                do { event = next_event(peer); } while (event.type != PropertyNotify ||
                    event.xproperty.atom != request.property || event.xproperty.state != PropertyDelete);
                const size_t remaining = text.size() - offset;
                const size_t count = remaining < 32768 ? remaining : 32768;
                XChangeProperty(peer, request.requestor, request.property, request.target, 8,
                    PropModeReplace, (const unsigned char*)text.data() + offset, (int)count);
                XFlush(peer);
                if (!count) break;
                offset += count;
            }
        }
    });
    if (is_drop) {
        const Window destination = _sapp_tc.app.main->xwin;
        XConvertSelection(_sapp_tc.display, selection, target, property, destination, CurrentTime);
        XEvent event;
        assert(_sapp_tc_x11_wait_for_event(destination, SelectionNotify, 1.0, &event));
        _sapp_tc_x11_process_event(&event);
    } else {
        const char* result = sapp_get_clipboard_string();
        assert(result && std::string(result) == text);
    }
    owner.join();
    XDestroyWindow(peer, window); XCloseDisplay(peer);
}

void copy_to_peer(const std::string& text) {
    sapp_set_clipboard_string(text.c_str());
    assert(std::string(sapp_get_clipboard_string()) == text);
    std::atomic<bool> finished{false};
    std::thread reader([&] {
        Display* peer = XOpenDisplay(nullptr); assert(peer);
        Window window = XCreateSimpleWindow(peer, DefaultRootWindow(peer), 0, 0, 1, 1, 0, 0, 0);
        const Atom property = XInternAtom(peer, "D3B_CLIPBOARD", False);
        XSelectInput(peer, window, PropertyChangeMask);
        XConvertSelection(peer, _sapp_tc.CLIPBOARD, _sapp_tc.UTF8_STRING, property, window, CurrentTime);
        XFlush(peer);
        XEvent event;
        do { event = next_event(peer); } while (event.type != SelectionNotify);
        assert(event.xselection.property == property);
        std::string received;
        bool incremental = false;
        for (;;) {
            Atom type; int format; unsigned long count, after;
            unsigned char* data = nullptr;
            assert(XGetWindowProperty(peer, window, property, 0, LONG_MAX / 4, True,
                AnyPropertyType, &type, &format, &count, &after, &data) == Success);
            assert(after == 0);
            const bool header = type == XInternAtom(peer, "INCR", False);
            if (!header) {
                assert(type == _sapp_tc.UTF8_STRING && format == 8);
                received.append((const char*)data, count);
            } else incremental = true;
            XFree(data); XFlush(peer);
            if (!incremental || (!header && !count)) break;
            do { event = next_event(peer); } while (event.type != PropertyNotify ||
                event.xproperty.atom != property || event.xproperty.state != PropertyNewValue);
        }
        assert(received == text);
        XDestroyWindow(peer, window); XCloseDisplay(peer);
        finished = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished) {
        assert(std::chrono::steady_clock::now() < deadline && "clipboard copy did not complete");
        while (XPending(_sapp_tc.display)) {
            XEvent event; XNextEvent(_sapp_tc.display, &event);
            _sapp_tc_x11_process_event(&event);
        }
        std::this_thread::yield();
    }
    reader.join();
}

void verify() {
    drops();
    std::string text;
    while (text.size() < 200 * 1024) text += "D3b-0123456789-日本語\n";
    copy_to_peer(text);
    paste_from_peer(text, false);
    paste_from_peer(text, true);
    // Larger than an XChangeProperty request: exercise outgoing INCR too.
    std::string large;
    while (large.size() < 2 * _sapp_tc_x11_clipboard_chunk_size()) large += text;
    copy_to_peer(large);
    paste_from_peer(large, true);
    copy_to_peer("");
    paste_from_peer("", false);
    std::puts("PASS: 20 exact drop paths, >2048-byte path, 200 KiB clipboard, INCR both ways, empty text");
    sapp_quit();
}
} // namespace

int main() {
    sapp_desc desc = {};
    desc.width = 64; desc.height = 64;
    desc.enable_clipboard = true; desc.enable_dragndrop = true;
    desc.init_cb = verify;
    desc.event_cb = [](const sapp_event* event) {
        if (event->type != SAPP_EVENTTYPE_FILES_DROPPED) return;
        assert(sapp_get_num_dropped_files() == (int)expected_paths.size());
        for (int i = 0; i < sapp_get_num_dropped_files(); ++i)
            assert(std::string(sapp_get_dropped_file_path(i)) == expected_paths[i]);
        ++delivered;
    };
    sapp_run(&desc);
    assert(delivered == 2);
}
