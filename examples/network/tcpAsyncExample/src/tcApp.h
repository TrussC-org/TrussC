#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// sendAsync() vs send(), measured on the frame time.
//
// A server streams a 2 MB payload every frame to a client in the same process.
// In BLOCKING mode the draw loop waits for each payload to reach the kernel; in
// ASYNC mode it hands the payload to a queue and carries on. The graph is the
// difference.
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void cleanup() override;

private:
    enum class Mode { Off, Async, Blocking };

    TcpServer server;
    TcpClient client;

    EventListener connectListener;
    EventListener completeListener;
    EventListener errorListener;

    Mode mode = Mode::Off;
    vector<char> payload;                 // one buffer, reused every frame

    // Sent from the draw loop, so plain members are fine
    uint64_t queued = 0;
    uint64_t refused = 0;                 // QueueFull: back-pressure, not an error
    uint64_t completed = 0;
    uint64_t bytesDelivered = 0;

    // Written by the client's receive thread, read by draw()
    atomic<uint64_t> bytesReceived{0};

    vector<float> frameMs;                // ring of recent frame times
    size_t frameCursor = 0;

    string lastError;

    void stream();
    void reset();
};
