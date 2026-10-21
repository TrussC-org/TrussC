// Test-only single-port TX-RX loopback harness. No serial consumer runs until
// the flood has reached the RX cap plus its known excess. No device => SKIP.
#include <TrussC.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <thread>
using namespace trussc;
namespace {
constexpr size_t cap = 64 * 1024 * 1024, excess = 131071;
constexpr size_t total = cap + excess, resumed = 4096;
const char* port;
const char* peerPort = nullptr;
int baud;
bool passed = false;
uint8_t pattern(size_t i) { return static_cast<uint8_t>((i * 17 + i / 251) % 256); }
struct DeviceApp : App {
    Serial serial, peer;
    EventListener logs;
    std::thread sender;
    std::atomic<bool> stop{false}, resume{false}, sendFailed{false};
    std::atomic<size_t> sent{0};
    size_t lastSent = 0;
    int backlog = 0, loss = 0, stage = 0;
    bool onMain = true, warningFields = true;
    std::thread::id appThread;
    std::chrono::steady_clock::time_point started, progress;
    void fail(const char* reason) {
        std::fprintf(stderr, "serialRxDevice detail: %s; sent=%zu backlog=%d loss=%d dropped=%zu stage=%d\n",
                     reason, sent.load(), backlog, loss, serial.getDroppedByteCount(), stage);
        passed = false;
        requestExit();
    }
    void setup() override {
        appThread = std::this_thread::get_id();
        started = progress = std::chrono::steady_clock::now();
        logs = getLogger().onLog.listen([this](LogEventArgs& e) {
            if (e.message.find("Serial: RX buffer exceeds 1 MiB") != std::string::npos) {
                ++backlog;
                onMain &= std::this_thread::get_id() == appThread;
                warningFields &= e.level == LogLevel::Warning;
            }
            if (e.message.find("Serial: RX buffer overflow") != std::string::npos) {
                ++loss;
                onMain &= std::this_thread::get_id() == appThread;
                warningFields &= e.level == LogLevel::Warning &&
                    e.message.find("capacity bytes: 67108864") != std::string::npos &&
                    e.message.find("total dropped bytes:") != std::string::npos;
            }
        });
        if (!serial.setup(port, baud) || (peerPort && !peer.setup(peerPort, baud))) { fail("open failed"); return; }
        sender = std::thread([this] {
            std::array<uint8_t, 4096> bytes;
            size_t offset = 0;
            while (!stop) {
                const size_t limit = resume ? total + resumed : total;
                if (offset == limit) {
                    if (offset == total + resumed) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const size_t n = std::min(bytes.size(), limit - offset);
                for (size_t i = 0; i < n; ++i) bytes[i] = pattern(offset + i);
                Serial& output = peerPort ? peer : serial;
                const int written = output.writeBytes(bytes.data(), static_cast<int>(n));
                if (written < 0) {
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
#endif
                    sendFailed = true;
                    return;
                }
                offset += static_cast<size_t>(written);
                sent = offset;
                if (!written) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    bool verify(size_t offset, size_t length) {
        std::array<uint8_t, 16384> bytes;
        while (length) {
            const int n = serial.readBytes(bytes.data(), static_cast<int>(std::min(length, bytes.size())));
            if (n <= 0) return false;
            for (int i = 0; i < n; ++i) if (bytes[i] != pattern(offset + i)) return false;
            offset += static_cast<size_t>(n);
            length -= static_cast<size_t>(n);
        }
        return true;
    }
    void update() override {
        const auto now = std::chrono::steady_clock::now();
        if (sent != lastSent) { lastSent = sent; progress = now; }
        if (sendFailed || now - progress > std::chrono::seconds(30) ||
            serial.getDroppedByteCount() > excess || backlog > 1 || loss > 1) { fail("timeout, send error or unexpected count"); return; }
        // Only counter queries while stalled; automatic main-thread delivery is
        // independently observed before the first available/read call.
        if (stage == 0 && sent == total && serial.getDroppedByteCount() == excess &&
            backlog == 1 && loss == 1 && now - started >= std::chrono::seconds(2)) {
            if (!onMain || !warningFields || serial.available() != static_cast<int>(cap) ||
                !verify(excess, cap) || serial.available() != 0) { fail("retained bytes or warning fields differ"); return; }
            stage = 1;
            progress = now;
            resume = true;
        } else if (stage == 1 && sent == total + resumed && serial.available() == static_cast<int>(resumed)) {
            passed = verify(total, resumed) && serial.available() == 0 &&
                serial.getDroppedByteCount() == excess && backlog == 1 && loss == 1 && onMain;
            requestExit();
        }
    }
    void exit() override {
        stop = true;
        if (sender.joinable()) sender.join();
        peer.close();
        serial.close();
        logs.disconnect();
    }
};
}
int main(int argc, char** argv) {
    if (argc == 1) {
        std::puts("SKIP: serialRxDevice needs PORT BAUD [PEER_PORT]; use a TX-RX jumper for one port");
        return 0;
    }
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: serialRxDevice PORT BAUD [PEER_PORT]\n");
        return 2;
    }
    port = argv[1];
    if (argc == 4) peerPort = argv[3];
    char* end = nullptr;
    errno = 0;
    const long rate = std::strtol(argv[2], &end, 10);
    if (errno == ERANGE || !end || *end || rate <= 0 || rate > 2147483647L) return 2;
    baud = static_cast<int>(rate);
    runHeadlessApp<DeviceApp>(HeadlessSettings().setFps(200));
    std::printf("serialRxDevice: %s (backlog=1 loss=1 dropped=131071 newest=67108864 resumed=4096)\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
