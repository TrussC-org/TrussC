
#include "tc/comm/tcSerial.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
using namespace trussc;
Logger& loggerInitializedBeforeSerial = getLogger();
Serial serial;
int master = -1;
void sendBytes(size_t count) {
    uint8_t data[4096]{};
    while(count) {
        auto n = ::write(master,data,std::min(count,sizeof(data)));
        if(n > 0) count -= n;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct FeedAtExit {
    ~FeedAtExit() {
        fprintf(stderr,"exit: queue has been destroyed, Serial is still alive; send one byte\n");
        sendBytes(1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (serial.getDroppedByteCount() != 1) {
            if (std::chrono::steady_clock::now() > deadline) std::_Exit(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::fprintf(stderr, "PASS: worker counted late input after queue/event teardown\n");
    }
} feed;
int main() {
    master = posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(master < 0 || grantpt(master) || unlockpt(master)) return 2;
    if(!serial.setup(ptsname(master),115200)) return 3;
    internal::mainThreadQueue();
    sendBytes(internal::SerialRxBuffer::capacity);
    while(serial.available() != int(internal::SerialRxBuffer::capacity))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    fprintf(stderr,"buffer is full; main returns with a global Serial still open\n");
}
