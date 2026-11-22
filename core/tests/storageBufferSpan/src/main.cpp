#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <array>
#include "../../common/tcDisplayTest.h"
namespace {
using namespace tc;
using namespace std;
template<class R>
concept CanUpdate = requires(StorageBuffer& buffer, R&& range) { buffer.update(std::forward<R>(range)); };
static_assert(CanUpdate<vector<float>&> && CanUpdate<const vector<float>&>);
static_assert(CanUpdate<array<float, 4>&> && CanUpdate<const array<float, 4>&>);
static_assert(CanUpdate<span<float>> && CanUpdate<span<const float>>);
static_assert(CanUpdate<span<float, 4>> && CanUpdate<span<const float, 4>>);
int failures = 0;
void check(const char* label, bool ok) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
template<class R>
concept CanRead = requires(StorageBuffer& buffer, R&& range) { buffer.read(std::forward<R>(range)); };
static_assert(CanRead<vector<float>&> && CanRead<array<float, 4>&>);
static_assert(CanRead<span<float>> && CanRead<span<float, 4>>);
static_assert(!CanRead<const vector<float>&> && !CanRead<const array<float, 4>&>);
static_assert(!CanRead<span<const float>> && !CanRead<span<const float, 4>>);
static_assert(CanRead<const span<float>&>); // Const view, writable elements.
class SpanApp : public App {
    StorageBuffer buffer;
    void setup() override {
        vector<float> values{1, 2, 3, 4}, output(4);
        check("allocate float storage", buffer.allocate(values.size() * sizeof(float)));
        check("vector update uses byte count", buffer.update(values));
        check("vector read uses byte count", buffer.read(output) && output == values);
        array<float, 4> arrayOutput{};
        check("array read uses byte count", buffer.read(arrayOutput) && std::equal(values.begin(), values.end(), arrayOutput.begin()));
        vector<float> oversized(5, 42);
        check("oversized span preserves output", !buffer.read<float>(oversized) && oversized == vector<float>(5, 42));
        exitApp();
    }
};
#endif
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    if (argc > 1 && string(argv[1]) == "--gpu-check") {
        WindowSettings settings;
        settings.setSize(64, 64).setHighDpi(false).setSampleCount(1);
        return tcCoreTest::runDisplayTest<SpanApp>(settings, failures);
    }
#else
    (void)argc;
    (void)argv;
#endif
    StorageBuffer buffer;
    vector<float> values{1, 2, 3, 4};
    const array<float, 4> input{1, 2, 3, 4};
    check("deduced vector update preserves failure", !buffer.update(values));
    check("deduced const array update preserves failure", !buffer.update(input));
    check("explicit span update remains available", !buffer.update(span<const float>(values)));
    check("headless span update preserves failure", !buffer.update<float>(values));
    check("pointer nullptr zero update remains available", !buffer.update(nullptr, 0));
    check("typed pointer update remains available", !buffer.update(values.data(), values.size() * sizeof(float)));
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    check("span read failure preserves output", !buffer.read<float>(values) && values == vector<float>({1, 2, 3, 4}));
    vector<float> empty;
    check("empty vector read succeeds", buffer.read(empty));
    check("explicit element type remains available", buffer.read<float>(empty));
    check("explicit span read remains available", buffer.read(span<float>(empty)));
    array<float, 4> output{1, 2, 3, 4};
    check("deduced array read preserves output", !buffer.read(output) && output == input);
    check("pointer zero read remains available", buffer.read(nullptr, 0));
    check("typed pointer read preserves output", !buffer.read(values.data(), values.size() * sizeof(float)) && values == vector<float>({1, 2, 3, 4}));
#endif
    return failures ? 1 : 0;
}
