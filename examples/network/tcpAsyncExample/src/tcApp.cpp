// =============================================================================
// tcApp.cpp - Asynchronous TCP send sample
// =============================================================================

#include "tcApp.h"
#include <sstream>
#include <iomanip>

using namespace std;
using namespace trussc;

namespace {

constexpr int kPort = 9002;
constexpr size_t kPayloadBytes = 2 * 1024 * 1024;   // one "frame" of data
constexpr size_t kGraphFrames = 240;

string mib(size_t bytes) {
    ostringstream oss;
    oss << fixed << setprecision(1) << (double(bytes) / (1024.0 * 1024.0)) << " MB";
    return oss.str();
}

} // namespace

void tcApp::setup() {
    // The whole point of this example is the difference between two keys, so
    // make it drivable without hands: with TRUSSC_MCP=1 in the environment, A
    // and B can be injected and the frame-time graph read back. Without that
    // variable nothing is started and no port is opened.
    // MCP is desktop-only — Web builds skip it.
#ifndef __EMSCRIPTEN__
    mcp::registerControlTools();
#endif

    payload.assign(kPayloadBytes, 'x');
    frameMs.assign(kGraphFrames, 0.0f);

    if (!server.start(kPort)) {
        lastError = "could not start the server on port " + to_string(kPort);
        return;
    }

    // Every queued send reports back here, exactly once, whether it was written
    // or dropped by a disconnect. Deliver::Main runs it at the top of the next
    // frame, so these counters need no lock.
    completeListener = server.onSendComplete.listen([this](TcpSendCompleteEventArgs& e) {
        ++completed;
        bytesDelivered += e.bytesSent;
        if (e.error != SendError::None) lastError = string("send: ") + sendErrorName(e.error);
    }, Deliver::Main);

    errorListener = server.onError.listen([this](TcpServerErrorEventArgs& e) {
        lastError = e.message;
    }, Deliver::Main);

    // The receiving end. This listener runs on the client's own receive thread —
    // hence the atomic — because marshalling every chunk to the main thread
    // would queue faster than the main thread could drain it.
    connectListener = client.onReceive.listen([this](TcpReceiveEventArgs& e) {
        bytesReceived.fetch_add(e.data.size());

        // Sleeping here is what a slow consumer looks like from the sender's
        // side: this listener runs on the receive thread, so holding it stops
        // the socket being drained, the server's window closes, and the send
        // path finally has something to wait for.
        if (slowConsumer.load()) this_thread::sleep_for(chrono::milliseconds(40));
    });

    client.connect("127.0.0.1", kPort);
}

void tcApp::update() {
    // Record the frame time before the sends, so the graph shows the cost of
    // the previous frame's choice rather than a half-finished one.
    frameMs[frameCursor] = static_cast<float>(getDeltaTime() * 1000.0);
    frameCursor = (frameCursor + 1) % frameMs.size();

    if (mode != Mode::Off) stream();
}

void tcApp::stream() {
    for (int id : server.getClientIds()) {
        if (mode == Mode::Async) {
            // Returns immediately. A falsy result means the queue is already at
            // its high-water mark: the peer is behind, and dropping this frame
            // is the right answer for a stream.
            const SendResult r = server.sendAsync(id, payload.data(), payload.size());
            if (r) ++queued;
            else if (r.error == SendError::QueueFull) ++refused;
        } else {
            // Waits until the whole payload has reached the kernel. Whatever
            // that costs comes out of this frame.
            server.send(id, payload.data(), payload.size());
            ++queued;
            ++completed;
            bytesDelivered += payload.size();
        }
    }
}

void tcApp::draw() {
    clear(0.12f);

    float y = 40;
    setColor(1.0f);
    drawBitmapString("TCP sendAsync vs send", 40, y);
    y += 26;

    setColor(0.7f);
    drawBitmapString("A: stream with sendAsync   B: stream with send   SPACE: stop", 40, y);
    y += 18;
    drawBitmapString("S: slow the receiver down   [ / ]: queue limit   X: reset", 40, y);
    y += 26;

    const char* modeName = mode == Mode::Async    ? "ASYNC  (sendAsync)"
                           : mode == Mode::Blocking ? "BLOCKING  (send)"
                                                    : "idle";
    setColor(mode == Mode::Blocking ? Color(1.0f, 0.5f, 0.35f) : Color(0.4f, 0.78f, 1.0f));
    drawBitmapString(string("Mode: ") + modeName, 40, y);
    y += 20;

    // Without this the peer keeps up and neither mode has anything to wait for,
    // so both look identical however the platform buffers.
    const bool slow = slowConsumer.load();
    setColor(slow ? Color(1.0f, 0.85f, 0.4f) : Color(0.5f));
    drawBitmapString(slow ? "Receiver: slow (40 ms per chunk) - this is what makes the two modes differ"
                          : "Receiver: keeping up - press S to slow it down",
                     40, y);
    y += 26;

    // --- frame time graph ----------------------------------------------------
    // 16.7 ms is the 60 fps budget; a bar above the line is a frame the send
    // stalled.
    const float gx = 40, gw = 880, gh = 150;
    const float gy = y;
    setColor(0.18f);
    drawRect(gx, gy, gw, gh);

    const float msPerPixel = 50.0f / gh;   // full height = 50 ms
    setColor(0.35f);
    const float budgetY = gy + gh - (16.7f / msPerPixel);
    drawLine(gx, budgetY, gx + gw, budgetY);

    const float barW = gw / static_cast<float>(frameMs.size());
    for (size_t i = 0; i < frameMs.size(); ++i) {
        // Oldest on the left: the cursor is where the next sample lands.
        const float ms = frameMs[(frameCursor + i) % frameMs.size()];
        float h = ms / msPerPixel;
        if (h > gh) h = gh;
        setColor(ms > 16.7f ? Color(1.0f, 0.45f, 0.3f) : Color(0.4f, 1.0f, 0.5f));
        drawRect(gx + i * barW, gy + gh - h, barW, h);
    }
    setColor(0.6f);
    drawBitmapString("frame time (full height = 50 ms, line = 16.7 ms)", gx, gy + gh + 16);
    y = gy + gh + 42;

    // --- numbers -------------------------------------------------------------
    ostringstream oss;
    oss << fixed << setprecision(1) << getFrameRate() << " fps";
    setColor(1.0f);
    drawBitmapString(oss.str(), 40, y);
    y += 22;

    setColor(0.86f);
    drawBitmapString("clients: " + to_string(server.getClientCount()), 40, y);
    y += 20;

    size_t pending = 0;
    for (int id : server.getClientIds()) pending += server.getSendAsyncPendingBytes(id);
    drawBitmapString("queued now: " + mib(pending) + " of " +
                         mib(server.getSendAsyncBufferSize()) + " high-water mark",
                     40, y);
    y += 20;

    // The mark, drawn as a bar, is the clearest picture of back-pressure.
    const float qw = 400.0f;
    const size_t limit = server.getSendAsyncBufferSize();
    setColor(0.2f);
    drawRect(40, y, qw, 12);
    if (limit > 0) {
        float frac = static_cast<float>(double(pending) / double(limit));
        if (frac > 1.0f) frac = 1.0f;
        setColor(frac > 0.9f ? Color(1.0f, 0.45f, 0.3f) : Color(0.4f, 0.78f, 1.0f));
        drawRect(40, y, qw * frac, 12);
    }
    y += 30;

    setColor(0.86f);
    drawBitmapString("sends queued: " + to_string(queued) +
                         "   completed: " + to_string(completed) +
                         "   refused (queue full): " + to_string(refused),
                     40, y);
    y += 20;
    drawBitmapString("delivered: " + mib(static_cast<size_t>(bytesDelivered)) +
                         "   received by the client: " +
                         mib(static_cast<size_t>(bytesReceived.load())),
                     40, y);
    y += 20;

    if (!lastError.empty()) {
        setColor(1.0f, 0.6f, 0.4f);
        drawBitmapString("last error: " + lastError, 40, y);
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'A') {
        mode = Mode::Async;
    } else if (key == 'B') {
        mode = Mode::Blocking;
    } else if (key == KEY_SPACE || key == ' ') {
        mode = Mode::Off;
    } else if (key == 'S') {
        slowConsumer.store(!slowConsumer.load());
    } else if (key == 'X') {
        reset();
    } else if (key == '[') {
        const size_t limit = server.getSendAsyncBufferSize();
        server.setSendAsyncBufferSize(limit > 1024 * 1024 ? limit / 2 : 1024 * 1024);
    } else if (key == ']') {
        server.setSendAsyncBufferSize(server.getSendAsyncBufferSize() * 2);
    }
}

void tcApp::reset() {
    queued = refused = completed = bytesDelivered = 0;
    bytesReceived.store(0);
    frameMs.assign(kGraphFrames, 0.0f);
    frameCursor = 0;
    lastError.clear();
}

void tcApp::cleanup() {
    // stop() drains the queue: whatever is still on it completes as
    // Disconnected rather than vanishing.
    client.disconnect();
    server.stop();
}
