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
                   std::function<void()> finalChecks = {}) {
    using TestApp = detail::DisplayTestApp<AppClass>;
    TestApp::finish = [&failures, finalChecks = std::move(finalChecks)] {
        if (finalChecks) finalChecks();
        // Set only the code: the app is already shutting down, so no second
        // quit request and the recorded exit reason stays as it was.
        tc::internal::appExitCode() = failures ? 1 : 0;
        std::fflush(stdout);
        std::fflush(stderr);
    };
    return tc::runApp<TestApp>(settings);
}

} // namespace tcCoreTest
