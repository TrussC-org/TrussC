#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include "tc/network/tcUdpSocket.h"
#include <cstdlib>
#include <new>

namespace {
thread_local bool measuring = false;
thread_local size_t largestAllocation = 0, totalAllocation = 0;
}
// Instrument only this executable; construct the caller's input before measuring.
void* operator new(size_t bytes) {
    if (measuring) {
        largestAllocation = std::max(largestAllocation, bytes);
        totalAllocation += bytes;
        if (bytes >= 1024 * 1024) throw std::bad_alloc();
    }
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { std::free(pointer); }

namespace trussc::internal {
struct TcpServerBindAddressTestAccess {
    static size_t retained(TcpServer& socket) {
        return socket.state_->bindAddress_.capacity() + socket.state_->bindAddressError_.capacity();
    }
};
struct UdpSocketBindAddressTestAccess {
    static size_t retained(UdpSocket& socket) {
        auto config = sharedLoad(socket.state_)->bindConfiguration_;
        return config->address.capacity() + config->error.capacity();
    }
};
}

TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    TcpServer tcp;
    UdpSocket udp;
    string tcpError, udpError;
    auto tcpListener = tcp.onError.listen([&](TcpServerErrorEventArgs& event) { tcpError = event.message; });
    auto udpListener = udp.onError.listen([&](UdpErrorEventArgs& event) { udpError = event.message; });
    string input(8 * 1024 * 1024, '\xff');
    // Length must reject before examining NUL/colon far beyond the bounded preview.
    input.back() = ':';
    input[input.size() - 2] = '\0';
    bool threw = false, tcpFailed = false, udpFailed = false;
    measuring = true;
    try {
        tcp.setBindAddress(input);
        udp.setBindAddress(input);
        udp.stopReceiving();
        udp.close();
        tcpFailed = !tcp.start(0) && !tcp.isRunning();
        udpFailed = !udp.bind(0, false) && !udp.isValid() && udp.getLocalPort() == 0;
        udp.close();
    } catch (const std::bad_alloc&) { threw = true; }
    measuring = false;
    check("Multi-megabyte invalid settings and lifecycle use bounded allocations",
          !threw && largestAllocation < 4096 && totalAllocation < 64 * 1024);
    check("Multi-megabyte settings fail the next start/bind with the named error",
          tcpFailed && udpFailed && tcpError.find("Invalid bind address") == 0 &&
          udpError == tcpError && tcpError.size() <= 256 && tcpError.find("...") != string::npos &&
          tcpError.find("\\xff") != string::npos && all_of(tcpError.begin(), tcpError.end(),
              [](unsigned char byte) { return byte >= 0x20 && byte < 0x7f; }));
    check("No input-sized storage survives rejection or lifecycle replacement",
          internal::TcpServerBindAddressTestAccess::retained(tcp) < 512 &&
          internal::UdpSocketBindAddressTestAccess::retained(udp) < 512);
    std::printf("Address storage probe: largest=%zu bytes, total=%zu bytes\n", largestAllocation, totalAllocation);
    return failures ? 1 : 0;
}
