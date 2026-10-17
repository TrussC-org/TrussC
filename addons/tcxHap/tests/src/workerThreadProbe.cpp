#include "workerThreadProbe.h"

#ifdef TC_HAP_WORKER_PROBE
#include <cassert>
#include <cerrno>
#include <dlfcn.h>
#include <memory>
#include <new>
#include <pthread.h>

// Clang can link sanitizer interceptors into the executable itself. RTLD_NEXT
// would skip them and bypass the runtime's worker initialization.
extern "C" int __interceptor_pthread_create(pthread_t*, const pthread_attr_t*,
    void* (*)(void*), void*) __attribute__((weak));

namespace {
thread_local hapTest::WorkerProbe* currentProbe = nullptr;

struct Start {
    void* (*function)(void*);
    void* argument;
    hapTest::WorkerProbe* probe;
};

void* observedStart(void* argument) {
    std::unique_ptr<Start> start(static_cast<Start*>(argument));
    void* result = start->function(start->argument);
    ++start->probe->completed;
    return result;
}
} // namespace

hapTest::WorkerProbe::WorkerProbe(unsigned int failAt) : failAt(failAt) {
    assert(!currentProbe);
    currentProbe = this;
}

hapTest::WorkerProbe::~WorkerProbe() {
    assert(completed == created);
    currentProbe = nullptr;
}

// Executable-local interposition also observes std::thread's shared-library
// calls. EAGAIN makes its constructor throw std::system_error, just as an OS
// resource failure does. Outside a probe, calls pass through unchanged.
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
                              void* (*function)(void*), void* argument) noexcept {
    using Create = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    static const Create create = __interceptor_pthread_create ? __interceptor_pthread_create :
        reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    if (!create) return EAGAIN;
    auto* probe = currentProbe;
    if (!probe) return create(thread, attributes, function, argument);
    if (++probe->attempts == probe->failAt) return EAGAIN;
    auto* start = new (std::nothrow) Start{function, argument, probe};
    if (!start) return ENOMEM;
    const int result = create(thread, attributes, observedStart, start);
    if (result == 0) ++probe->created;
    else delete start;
    return result;
}
#endif
