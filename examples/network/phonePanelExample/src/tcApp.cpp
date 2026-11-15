// =============================================================================
// phonePanelExample - Control a fullscreen app from a phone on the same LAN.
//
// Build and run, then open the on-screen URL on your phone for radio buttons,
// one fader, and a scrolling log. Escape exits. PHONE_PANEL_BIND selects an up
// local IPv4 address when needed (for example, PHONE_PANEL_BIND=192.168.1.20).
// HTTP workers send value changes through runOnMainThread; onLog feeds a bounded,
// mutex-protected log. Bind one private LAN interface and check a fresh startup
// token on every route. Requests, connections, values, and logs are bounded.
// Use a trusted LAN: plain HTTP exposes the token to anyone on the network path;
// this example has no TLS. See README.md for build and run commands.
// =============================================================================

#include "tcApp.h"
#include "impl/httplib.h"
#include <cmath>
#include <cstdlib>
#include <random>
#include <sstream>

namespace {
const char* page = R"HTML(<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Phone control panel</title>
<style>body{font:20px sans-serif;max-width:32em;margin:2em auto;padding:1em}
input[type=range]{width:100%}pre{height:16em;overflow:auto;background:#eee;white-space:pre-wrap}</style>
<h1>Phone control panel</h1>
<div id="radios"><label><input type="radio" name="mode" value="0" checked>Red</label>
<label><input type="radio" name="mode" value="1">Green</label>
<label><input type="radio" name="mode" value="2">Blue</label></div>
<p><label>Level <input id="fader" type="range" min="0" max="1" step="0.01" value="0.5"></label></p>
<p id="status">Ready</p><pre id="log"></pre>
<script>
const token = new URLSearchParams(location.search).get('token');
const endpoint = path => path + '?token=' + encodeURIComponent(token || '');
async function change() {
  const radio = document.querySelector('input[name=mode]:checked').value;
  try { const r = await fetch(endpoint('/value'), {method:'POST',body:radio+' '+document.querySelector('#fader').value});
    document.querySelector('#status').textContent = r.ok ? 'Sent' : 'HTTP ' + r.status;
  } catch { document.querySelector('#status').textContent = 'Disconnected'; }
}
document.querySelectorAll('input').forEach(input => input.onchange = change);
setInterval(async () => {
  try { const r = await fetch(endpoint('/log')); if (!r.ok) return;
    const box = document.querySelector('#log'); box.textContent = await r.text(); box.scrollTop = box.scrollHeight;
  } catch { document.querySelector('#status').textContent = 'Disconnected'; }
}, 1000);
</script>)HTML";

string makeToken() {
    // A fresh 128-bit token per launch; never put it in the log stream.
    random_device source;
    uniform_int_distribution<unsigned int> byte(0, 255);
    string token;
    for (int i = 0; i < 16; ++i) {
        const auto value = byte(source);
        token += "0123456789abcdef"[value >> 4];
        token += "0123456789abcdef"[value & 15];
    }
    return token;
}

bool tokenMatches(const string& supplied, const string& expected) {
    // Visit all expected bytes without exiting on the first mismatch.
    size_t difference = supplied.size() ^ expected.size();
    for (size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(expected[i]) ^
                      (i < supplied.size() ? static_cast<unsigned char>(supplied[i]) : 0u);
    }
    return difference == 0;
}
}

tcApp::tcApp() : server(make_unique<httplib::Server>()) {}
tcApp::~tcApp() { exit(); }

void tcApp::setup() {
    const auto interfaces = listNetworkInterfaces();
    string address = "127.0.0.1";
    // Pick one LAN interface, not 0.0.0.0 (which also exposes public/VPN interfaces).
    for (const auto& entry : interfaces) {
        if (entry.isUp && entry.isIPv4 && !entry.isLoopback && isPrivate(entry.address)) {
            address = entry.address;
            break;
        }
    }
    // Explicit override must name an up local IPv4 interface; loopback is useful for testing.
    if (const char* requested = std::getenv("PHONE_PANEL_BIND")) {
        const auto found = find_if(interfaces.begin(), interfaces.end(), [requested](const auto& entry) {
            return entry.isUp && entry.isIPv4 && entry.address == requested;
        });
        if (found == interfaces.end()) {
            logError("phonePanel") << "PHONE_PANEL_BIND must be an up local IPv4 address";
            return;
        }
        address = found->address;
    }
    const string token = makeToken();
    url = "http://" + address + ":8088/?token=" + token;
    // Plain HTTP: anyone on the network path can see this token. Use a trusted LAN.
    // Stable Server APIs keep this example ready for a later cpp-httplib update.
    server->set_payload_max_length(64); // Bound the tiny control message before parsing it.
    server->set_read_timeout(2);
    server->set_write_timeout(2);
    server->set_keep_alive_max_count(4);
    // Bound active workers and pending connections so slow clients cannot grow a queue forever.
    server->new_task_queue = [] { return new httplib::ThreadPool(2, 8); };
    server->set_pre_routing_handler([token, count = 0, start = chrono::steady_clock::now(),
                                    guard = make_shared<mutex>()]
                                   (const httplib::Request& request, httplib::Response& response) mutable {
        response.set_header("Cache-Control", "no-store");
        response.set_header("Referrer-Policy", "no-referrer");
        // Authenticate every route, including the page and log, without logging the URL.
        const bool authorized = tokenMatches(request.get_param_value("token"), token);
        response.status = authorized ? 200 : 403;
        if (!authorized) return httplib::Server::HandlerResponse::Handled;
        // A shared 30 requests/second budget also bounds main-thread work from authenticated clients.
        lock_guard<mutex> lock(*guard);
        const auto now = chrono::steady_clock::now();
        if (now - start >= chrono::seconds(1)) {
            start = now;
            count = 0;
        }
        if (++count > 30) {
            response.status = 429;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
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
        int selected = 0;
        float level = 0;
        string extra;
        // Reject invalid radio indices/non-finite numbers; clamp the fader to its displayed range.
        if (!(input >> selected >> level) || (input >> extra) || selected < 0 || selected > 2 || !std::isfinite(level)) {
            response.status = 400;
            return;
        }
        level = std::clamp(level, 0.0f, 1.0f);
        // HTTP workers never touch scene state; the weak owner also makes queued teardown work safe.
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
        lock_guard<mutex> lock(store->guard);
        // Cap both retained lines and line length; log polling never returns an unbounded history.
        istringstream input(event.message.substr(0, 512));
        string line;
        while (getline(input, line)) {
            store->lines.push_back(line.substr(0, 512));
            if (store->lines.size() > 100) store->lines.pop_front();
        }
    });
    if (!server->bind_to_port(address, 8088)) {
        url = "Could not bind port 8088 on " + address;
        logError("phonePanel") << "Could not bind HTTP server";
        return;
    }
    serverThread = thread([this] { server->listen_after_bind(); });
    server->wait_until_ready(); // stop() must also work if the app exits immediately after setup().
    logNotice("phonePanel") << "Panel ready (URL and token are shown only on screen)";
}

void tcApp::draw() {
    clear(0.08f);
    setColor(1.0f);
    drawBitmapString(url, 24, 32);
    drawBitmapString("Choose the same LAN on your phone. Escape exits.", 24, 56);
    drawBitmapString("Radio: " + to_string(radio) + "\nFader: " + to_string(fader), 24, 120, 4.0f);
}

void tcApp::exit() {
    server->stop();
    if (serverThread.joinable()) serverThread.join();
    logListener.disconnect();
}
