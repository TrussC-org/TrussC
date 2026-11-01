#pragma once

#include <TrussC.h>
#include <cstdio>
#include <functional>
#include <utility>

namespace tcCoreTest {
namespace detail {

template<class AppClass>
struct DisplayTestApp : AppClass {
    // Keep the callback alive when the browser's asynchronous runApp returns.
    inline static std::function<void()> finish;

    void cleanup() override {
        AppClass::cleanup();
        finish();
    }
};

} // namespace detail

// A display test must decide its verdict inside the app: macOS terminates
// the process in sapp_run and never executes code following runApp(). Final
// checks run after the App's exit()/cleanup(), including on an early close,
// while TrussC can still propagate exitApp(code) through platform cleanup.
// Return this call directly; startup failures retain runApp's non-zero result.
template<class AppClass>
int runDisplayTest(const tc::WindowSettings& settings, int& failures,
                   std::function<void()> finalChecks = {},
                   const tc::RuntimeSettings& runtime = {}) {
    using TestApp = detail::DisplayTestApp<AppClass>;
    TestApp::finish = [&failures, finalChecks = std::move(finalChecks)] {
        if (finalChecks) finalChecks();
        // Only ever raise the code: a framework-set non-zero code (e.g. the
        // device-lost default 1) must not become 0. A test that verified a
        // non-zero code in finalChecks may clear it itself. Set the code
        // only: the app is already shutting down (no second quit request).
        if (failures) tc::internal::appExitCode() = 1;
        std::fflush(stdout);
        std::fflush(stderr);
    };
    return tc::runApp<TestApp>(settings, runtime);
}

} // namespace tcCoreTest
