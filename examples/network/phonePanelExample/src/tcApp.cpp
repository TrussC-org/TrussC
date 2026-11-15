// Control a fullscreen app from a phone on the same trusted LAN.
// Open the on-screen URL for color radios, a brightness fader, and live logs.
// A private-LAN (or loopback) bind, per-launch token, and payload/value limits bound input.
// Workers use runOnMainThread with a weak owner; onLog fills a locked ring.
// Escape stops and joins the server. HTTP has no TLS; see README.md.
#include "tcApp.h"
#include "impl/httplib.h"
#include "panelPage.h"
#include <cmath>
#include <random>
#include <sstream>

tcApp::tcApp() : server(make_unique<httplib::Server>()) {}
tcApp::~tcApp() = default;

void tcApp::setup() {
    // Bind one private LAN interface selected by getLocalIp(), never 0.0.0.0.
    string address = getLocalIp();
    if (!isPrivate(address)) address = "127.0.0.1";   // never expose the panel on a public address
    // Fresh 128-bit token per launch; show it only on screen.
    random_device source;
    uniform_int_distribution<int> byte(0, 255);
    string token;
    for (int i = 0; i < 16; ++i) {
        const int value = byte(source);
        token += "0123456789abcdef"[value >> 4];
        token += "0123456789abcdef"[value & 15];
    }
    url = "http://" + address + ":8088/?token=" + token;
    server->set_payload_max_length(64); // Cap the control body before parsing.
    server->set_read_timeout(2);
    server->set_write_timeout(2);
    server->set_pre_routing_handler([token](const auto& request, auto& response) {
        // Check every route; a real service would use a constant-time compare.
        if (request.get_param_value("token") == token) return httplib::Server::HandlerResponse::Unhandled;
        response.status = 403;
        return httplib::Server::HandlerResponse::Handled;
    });
    server->Get("/", [](const auto&, auto& response) { response.set_content(page, "text/html"); });
    server->Get("/log", [store = logs](const auto&, auto& response) {
        lock_guard<mutex> lock(store->guard);
        string text;
        for (const auto& line : store->lines) text += line + '\n';
        response.set_content(text, "text/plain; charset=utf-8");
    });
    const weak_ptr<tcApp> app = dynamic_pointer_cast<tcApp>(shared_from_this());
    server->Post("/value", [app](const auto& request, auto& response) {
        istringstream input(request.body);
        int selected;
        float level;
        string extra;
        // Parse and validate the radio/finite level, then clamp brightness to 0–1.
        if (!(input >> selected >> level) || (input >> extra) || selected < 0 || selected > 2 || !isfinite(level)) {
            response.status = 400;
            return;
        }
        level = clamp(level, 0.0f, 1.0f);
        // Workers send scene changes to the main thread with a weak owner.
        runOnMainThread([app, selected, level] {
            if (auto owner = app.lock()) {
                owner->radio = selected;
                owner->fader = level;
                logNotice("phonePanel") << "radio=" << selected << " fader=" << level;
            }
        });
        response.set_content("queued", "text/plain");
    });
    logListener = getLogger().onLog.listen([store = logs](LogEventArgs& event) {
        // onLog may run on any thread; bound the ring to 100 lines of 512 bytes.
        lock_guard<mutex> lock(store->guard);
        istringstream input(event.message.substr(0, 512));
        string line;
        while (getline(input, line)) {
            store->lines.push_back(line);
            if (store->lines.size() > 100) store->lines.pop_front();
        }
    });
    if (!server->bind_to_port(address, 8088)) {
        url = "Could not bind port 8088 on " + address;
        return;
    }
    serverThread = thread([this] { server->listen_after_bind(); });
    server->wait_until_ready(); // Make stop() effective even immediately after setup().
    logNotice("phonePanel") << "Panel ready (URL and token are shown only on screen)";
}

void tcApp::draw() {
    clear(0.08f);
    setColor(radio == 0 ? fader : 0, radio == 1 ? fader : 0, radio == 2 ? fader : 0);
    drawRect(24, 90, getWidth() - 48, getHeight() - 114);
    setColor(1.0f);
    drawBitmapString(url, 24, 32);
    drawBitmapString("Choose the same LAN on your phone. Escape exits.", 24, 56);
}

void tcApp::exit() {
    // The framework calls exit() before member destruction: stop, then join.
    server->stop();
    if (serverThread.joinable()) serverThread.join();
    logListener.disconnect();
}
