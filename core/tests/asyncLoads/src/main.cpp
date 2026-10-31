#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>
using namespace std;
using namespace tc;
namespace {
#include "../../common/resourceLoadSmoke.h"
struct WorkerExitApp : App {
    bool workerRetired = false;
    void setup() override {
        internal::startResourceLoad([this] {
            exitApp(); // posted to main; the worker returns normally
            workerRetired = true;
        });
    }
    void exit() override {
        check("worker exit request reaches main", isMainThread());
    }
    void cleanup() override {
        internal::waitForResourceLoadsForTests();
        check("orderly exit drains the resource worker", workerRetired);
    }
};
}
TC_CORE_TEST_MAIN() {
    resourceLoadSmoke();
    runHeadlessApp<WorkerExitApp>();
    return failures ? 1 : 0;
}
