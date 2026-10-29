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
        if (bytes > 16 * 1024) throw std::bad_alloc();
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

namespace {
struct Measurement {
    size_t largest, total;
    bool completed, namedFailure, boundedStorage;
};

Measurement probe(size_t inputBytes, bool instrument = true) {
    TcpServer tcp;
    UdpSocket udp;
    string tcpError, udpError;
    auto tcpListener = tcp.onError.listen([&](TcpServerErrorEventArgs& event) { tcpError = event.message; });
    auto udpListener = udp.onError.listen([&](UdpErrorEventArgs& event) { udpError = event.message; });
    string input(inputBytes, '\xff');
    // Length must reject before examining NUL/colon far beyond the bounded preview.
    input.back() = ':';
    input[input.size() - 2] = '\0';
    bool threw = false, tcpFailed = false, udpFailed = false;
    largestAllocation = totalAllocation = 0;
    measuring = instrument;
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
    return {largestAllocation, totalAllocation, !threw,
            tcpFailed && udpFailed && tcpError.find("Invalid bind address") == 0 &&
            udpError == tcpError && tcpError.size() <= 256 && tcpError.find("...") != string::npos &&
            tcpError.find("\\xff") != string::npos && all_of(tcpError.begin(), tcpError.end(),
                [](unsigned char byte) { return byte >= 0x20 && byte < 0x7f; }),
            internal::TcpServerBindAddressTestAccess::retained(tcp) < 512 &&
            internal::UdpSocketBindAddressTestAccess::retained(udp) < 512};
}
} // namespace

TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    // Initialize one-time logging/platform buffers before comparing fresh sockets.
    probe(64 * 1024, false);
    size_t minLargest = SIZE_MAX, maxLargest = 0, minTotal = SIZE_MAX, maxTotal = 0;
    bool completed = true, namedFailure = true, boundedStorage = true;
    // Repeat in reverse order too, so reuse cannot hide input-dependent growth.
    for (const size_t bytes : {64u * 1024, 1024u * 1024, 8u * 1024 * 1024,
                              8u * 1024 * 1024, 1024u * 1024, 64u * 1024}) {
        const auto result = probe(bytes);
        minLargest = std::min(minLargest, result.largest);
        maxLargest = std::max(maxLargest, result.largest);
        minTotal = std::min(minTotal, result.total);
        maxTotal = std::max(maxTotal, result.total);
        completed &= result.completed;
        namedFailure &= result.namedFailure;
        boundedStorage &= result.boundedStorage;
        std::printf("Address storage probe: input=%zu bytes, largest=%zu bytes, total=%zu bytes\n",
                    bytes, result.largest, result.total);
    }
    // The security property is independence from input size. macOS logging may
    // allocate a fixed 4 KiB buffer; allow platform buffers with fixed caps and
    // at most 1 KiB variation across inputs, rather than requiring <4 KiB.
    check("Invalid settings and lifecycle allocations are independent of input size",
          completed && maxLargest <= 16 * 1024 && maxTotal <= 64 * 1024 &&
          maxLargest - minLargest <= 1024 && maxTotal - minTotal <= 1024);
    check("All input sizes fail the next start/bind with the named error", namedFailure);
    check("No input-sized storage survives rejection or lifecycle replacement", boundedStorage);
    return failures ? 1 : 0;
}
