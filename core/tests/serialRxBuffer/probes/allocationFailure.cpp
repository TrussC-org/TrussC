// Linux link-time injection: a failing RX malloc also forbids operator new
// persistently on that same worker. The main thread can still format logs.
#include "tc/comm/tcSerial.h"
#include <new>
#include <cstdio>
using namespace trussc;
std::atomic<bool> armed{false};
thread_local bool failedOnWorker = false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n) {
    if (armed.load() && n == internal::SerialRxBuffer::chunkSize + 3 * sizeof(size_t)) {
        failedOnWorker = true;
        return nullptr;
    }
    return __real_malloc(n);
}
void* operator new(size_t n) {
    if (failedOnWorker && armed.load()) {
        std::fprintf(stderr, "FAIL: worker allocated notification after RX allocation failure\n");
        std::_Exit(86);
    }
    if (void* p = __real_malloc(n)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
template<class F> bool until(F f) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
int main() {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) || unlockpt(master)) return 2;
    Serial serial;
    if (!serial.setup(ptsname(master), 115200)) return 3;
    internal::mainThreadQueue();
    const auto mainThread = std::this_thread::get_id();
    int warnings = 0;
    bool mainDelivery = true;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.find("Serial: RX buffer overflow") == std::string::npos) return;
        ++warnings;
        mainDelivery &= std::this_thread::get_id() == mainThread;
        mainDelivery &= e.message.find("total dropped bytes: 1") != std::string::npos;
        mainDelivery &= e.message.find("receive allocation failed") != std::string::npos;
        serial.available(); // re-entry without locks held
    });
    armed = true;
    uint8_t byte = 37;
    if (::write(master, &byte, 1) != 1 || !until([&] { return serial.getDroppedByteCount() == 1; })) return 4;
    // Delivery is automatic in the main-thread drain, without available/read polling.
    if (!until([&] { internal::drainMainThreadQueue(); return warnings == 1; })) return 5;
    if (::write(master, &byte, 1) != 1 || !until([&] { return serial.getDroppedByteCount() == 2; })) return 6;
    internal::drainMainThreadQueue();
    if (warnings != 1 || !mainDelivery || !serial.isConnected()) return 7;
    armed = false;
    if (::write(master, &byte, 1) != 1 || !until([&] { return serial.available() == 1; })) return 8;
    if (serial.readByte() != byte || serial.getDroppedByteCount() != 2) return 9;
    serial.close();
    ::close(master);
    std::puts("PASS: persistent worker allocation failure; main delivery once; reader recovered");
}
