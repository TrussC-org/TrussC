// Regression from the C2 review: an optional reason copy must not defeat a veto.
#include <TrussC.h>
#include <new>
#include <cstdlib>
#include <cstdio>
#include <exception>
static bool failAllocations=false;
void* operator new(std::size_t n) {
    if (failAllocations) { std::fprintf(stderr,"OOM_PROBE rejected allocation=%zu\n",n); throw std::bad_alloc(); }
    if (auto* p=std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
int main() {
    std::set_terminate([] { std::fputs("OOM_PROBE std::terminate during quit veto\n",stderr); std::_Exit(71); });
    tc::getMainThreadId();
    tc::internal::beginRuntimeRun(); tc::internal::beginFrameworkLifetime();
    auto listener=tc::runtime().events().exitRequested.listen([](tc::ExitRequestEventArgs& e) {
        e.cancel=true; e.reason=std::string(512,'x');
        failAllocations=true;
    });
    bool accepted=tc::internal::dispatchExitRequested();
    failAllocations=false;
    const bool emptyReason = tc::internal::exitBlockReason().empty();
    std::printf("OOM_PROBE returned accepted=%d empty_reason=%d\n",accepted,emptyReason);
    tc::internal::shutdownRuntime(); tc::internal::endFrameworkLifetime();
    return accepted || !emptyReason ? 1 : 0;
}
