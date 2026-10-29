#pragma once
#include <tcxOsc.h>
#include "tc/network/tcSocketInternal.h"

namespace tcx::osc {
struct OscReceiverTestAccess {
    static auto& socket(OscReceiver& receiver) { return receiver.socket_; }
    static auto& queue(OscReceiver& receiver) { return receiver.state_->polling_; }
    static void parse(OscReceiver& receiver, const std::vector<uint8_t>& bytes) {
        receiver.state_->parsePacket(bytes.data(), bytes.size(), receiver.state_->generation_);
    }
    static void receive(OscReceiver& receiver, const std::vector<uint8_t>& bytes) {
        tc::UdpReceiveEventArgs packet;
        packet.data.assign(bytes.begin(), bytes.end());
        receiver.state_->handleReceive(packet, receiver.state_->generation_);
    }
    static uint64_t rejected(OscReceiver& receiver) { return receiver.state_->rejectedDatagrams_; }
};
}
namespace trussc::internal {
struct UdpSocketTestAccess {
    static size_t bufferBytes(UdpSocket& socket) {
        return internal::sharedLoad(socket.state_)->receiveBuffer_.capacity();
    }
    static auto descriptor(UdpSocket& socket) { return sharedLoad(socket.state_)->socket_->handle.load(); }
    static auto run(UdpSocket& socket) { return internal::sharedLoad(socket.state_); }
};
}

inline bool oscBoundToLoopback(tcx::osc::OscReceiver& receiver) {
    sockaddr_in address{};
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    const auto handle = tc::internal::UdpSocketTestAccess::descriptor(tcx::osc::OscReceiverTestAccess::socket(receiver));
    return getsockname(handle, reinterpret_cast<sockaddr*>(&address), &size) == 0 &&
           address.sin_family == AF_INET && address.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
}
