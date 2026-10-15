#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>

using namespace tc;
namespace {
bool cleanupInvalidated = false;
struct TokenApp : App {
    std::weak_ptr<std::atomic<bool>> token;
    void setup() override { token = internal::appRunToken(); }
    void update() override { requestExitApp(); }
    void cleanup() override { cleanupInvalidated = !internal::appRunIsAlive(token); }
};
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--window") {
        const int code = runApp<TokenApp>(WindowSettings().setSize(64, 64));
        const bool ok = code == 0 && cleanupInvalidated;
        std::printf("runApp cleanup invalidates dialog token before app cleanup: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    buildAppDescriptor<TokenApp>();
    auto first = internal::appRunToken();
    const std::weak_ptr<std::atomic<bool>> oldToken = first;
    bool ok = internal::appRunIsAlive(oldToken);
    first->store(false); // native cleanup invalidates this same atomic
    ok = !internal::appRunIsAlive(oldToken) && ok;
    buildAppDescriptor<TokenApp>();
    const std::weak_ptr<std::atomic<bool>> nextToken = internal::appRunToken();
    ok = internal::appRunIsAlive(nextToken) && !internal::appRunIsAlive(oldToken) && ok;
    first.reset();
    ok = oldToken.expired() && !internal::appRunIsAlive(oldToken) && ok;
    internal::appRunToken()->store(false);
    std::printf("dialog run tokens are weak, invalidated, and isolated between runs: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
