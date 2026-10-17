#pragma once

#ifdef TC_HAP_WORKER_PROBE
#include <atomic>

namespace hapTest {
// Only thread creation on this scope's calling thread is observed. All observed
// workers must finish before the scope is destroyed.
struct WorkerProbe {
    explicit WorkerProbe(unsigned int failAt = 0);
    ~WorkerProbe();
    WorkerProbe(const WorkerProbe&) = delete;
    WorkerProbe& operator=(const WorkerProbe&) = delete;

    const unsigned int failAt;
    unsigned int attempts = 0;
    unsigned int created = 0;
    std::atomic<unsigned int> completed{0};
};
} // namespace hapTest
#endif
