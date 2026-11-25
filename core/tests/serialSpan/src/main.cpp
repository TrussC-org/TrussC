#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <thread>
#endif
namespace {
using namespace tc;
using namespace std;
int failures = 0;
void check(const char* label, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
} // namespace
TC_CORE_TEST_MAIN() {
    Serial serial;
    vector<unsigned char> bytes{1, 2, 3, 4};
    check("disconnected span read retains count", serial.readBytes(bytes) == serial.readBytes(bytes.data(), 4));
    check("disconnected span read preserves output", bytes == vector<unsigned char>({1, 2, 3, 4}));
    check("disconnected span write retains count", serial.writeBytes(bytes) == serial.writeBytes(bytes.data(), 4));
    check("pointer nullptr zero remains available", serial.readBytes(nullptr, 0) == -1 && serial.writeBytes(nullptr, 0) == -1);
    // Compile coverage for the existing string-literal overload.
    check("string literal remains available", serial.writeBytes("span") == -1);
#if defined(__linux__) && !defined(__ANDROID__)
    const int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0 && serial.setup(ptsname(master), 115200)) {
        check("PTY sends bytes", ::write(master, bytes.data(), bytes.size()) == 4);
        const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
        while (serial.available() < 4 && chrono::steady_clock::now() < deadline)
            this_thread::sleep_for(chrono::milliseconds(1));
        vector<unsigned char> prefix(2);
        check("span partial read count", serial.readBytes(prefix) == 2 && prefix == vector<unsigned char>({1, 2}));
        check("span remaining read count", serial.readBytes(bytes) == 2 && bytes[0] == 3 && bytes[1] == 4);
        check("span write count", serial.writeBytes(prefix) == 2);
        unsigned char output[2]{};
        check("span write payload", ::read(master, output, sizeof(output)) == 2 && output[0] == 1 && output[1] == 2);
        serial.close();
    } else {
        puts("SKIP: PTY unavailable; disconnected checks completed");
    }
    if (master >= 0) ::close(master);
#endif
    return failures ? 1 : 0;
}
