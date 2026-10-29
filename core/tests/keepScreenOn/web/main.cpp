#include <TrussC.h>
#include <cassert>
#include <cstdio>
#ifdef __EMSCRIPTEN_PTHREADS__
#include <thread>
#endif

int main() {
#ifdef __EMSCRIPTEN_PTHREADS__
    tc::internal::updateKeepScreenOn();
#endif
    tc::internal::shutdownKeepScreenOn();
    assert(!tc::getKeepScreenOn());
#ifdef __EMSCRIPTEN_PTHREADS__
    std::thread worker([] { tc::setKeepScreenOn(true); assert(tc::getKeepScreenOn()); });
    worker.join();
    EM_ASM({ if (Module['trusscWakeLock']) throw new Error('Worker applied before main-loop hook'); });
    tc::internal::updateKeepScreenOn();
#else
    tc::setKeepScreenOn(true);
#endif
    assert(tc::getKeepScreenOn());
    tc::setKeepScreenOn(false);
    assert(!tc::getKeepScreenOn());
#ifdef __EMSCRIPTEN_PTHREADS__
    tc::internal::updateKeepScreenOn();
#endif
    tc::setKeepScreenOn(true);
#ifdef __EMSCRIPTEN_PTHREADS__
    tc::internal::updateKeepScreenOn();
#endif
    tc::internal::shutdownKeepScreenOn();
    assert(!tc::getKeepScreenOn());
    puts("PASS Web requested state and main-thread dispatch");
    return 0;
}
