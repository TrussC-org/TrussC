// Pins #344 S6 only: an unattached App's setSize updates its own RectNode.
// S5 (does not resize another window) and cross-window R1/R2, S1-S5, S7/S8
// remain manual: repro/appSetSizeTest on test/app-setsize-own-window; see its
// repro/README.md. Headless S6 cannot prove that another window is not resized.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
struct SizeProbe : App {
    void setup() override {
        auto other = std::make_shared<App>();
        other->setSize(123, 45);
        if (other->getWidth() != 123 || other->getHeight() != 45) ++failures;
        std::printf("appSetSize S6: %s\n", failures ? "FAIL" : "PASS");
        other.reset();
        requestExit();
    }
};
} // namespace

TC_CORE_TEST_MAIN() {
    const int result = runHeadlessApp<SizeProbe>();
    return result || failures ? 1 : 0;
}
