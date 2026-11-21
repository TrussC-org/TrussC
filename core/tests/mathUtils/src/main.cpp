#include <tcMath.h>
#include <tc/utils/tcSimd.h>
#include <tc/utils/tcLog.h>
#include "../../common/tcCoreTest.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>

// Exercise the same unqualified calls as a typical app.
namespace tc = trussc;
using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(bool value, const char* label) {
    if (!value) { std::printf("FAIL: %s\n", label); ++failures; }
}
constexpr std::array<const char*, 8> names = {
    "sum", "mean", "variance", "stddev", "min", "max", "rms", "mse"};
std::array<double, 8> maxError{}, maxRelativeError{};

void statistics(std::size_t count, int pattern, std::size_t offset) {
    // Exact extents and non-vector-aligned input pointers also run under ASan.
    vector<float> a(count + offset), b(count + offset);
    for (std::size_t i = 0; i < count; ++i) {
        const float wave = float(int((i * 37) % 2001) - 1000) / 32.0f;
        a[i + offset] = pattern == 0 ? wave : pattern == 1 ? 1000000.0f + wave :
            pattern == 2 ? 7.0f : (i % 3 == 0 ? 1e10f : i % 3 == 1 ? 1.0f : -1e10f);
        b[i + offset] = -wave / 2.0f;
    }
    span<const float> av(count ? a.data() + offset : nullptr, count);
    span<const float> bv(count ? b.data() + offset : nullptr, count);
    double total = 0.0, squares = 0.0, difference = 0.0;
    double lo = count ? av[0] : 0.0, hi = lo;
    for (std::size_t i = 0; i < count; ++i) {
        const double value = av[i], delta = value - double(bv[i]);
        total += value;
        squares += value * value;
        difference += delta * delta;
        lo = std::min(lo, value); hi = std::max(hi, value);
    }
    const double center = count ? total / count : 0.0;
    double spread = 0.0;
    for (float value : av) { const double delta = double(value) - center; spread += delta * delta; }
    const double population = count ? spread / count : 0.0;
    const array<double, 8> expected = {total, center, population, std::sqrt(population), lo, hi,
        count ? std::sqrt(squares / count) : 0.0, count ? difference / count : 0.0};
    const auto bounds = minMax(av);
    const auto scalarBounds = simd::scalar::minMax(av.data(), count);
    const array<float, 8> actual = {sum(av), mean(av), variance(av), stddev(av),
        bounds.first, bounds.second, rms(av), mse(av, bv)};
    const array<float, 8> scalarValues = {simd::scalar::sum(av.data(), count),
        simd::scalar::mean(av.data(), count), simd::scalar::variance(av.data(), count),
        simd::scalar::stddev(av.data(), count), scalarBounds.first, scalarBounds.second,
        simd::scalar::rms(av.data(), count), simd::scalar::mse(av.data(), bv.data(), count)};
    for (std::size_t j = 0; j < actual.size(); ++j) {
        const double error = std::isfinite(actual[j])
            ? std::abs(double(actual[j]) - expected[j]) : numeric_limits<double>::infinity();
        const double scalarError = std::isfinite(scalarValues[j])
            ? std::abs(double(scalarValues[j]) - expected[j]) : numeric_limits<double>::infinity();
        const double worstError = std::max(error, scalarError);
        maxError[j] = std::max(maxError[j], worstError);
        maxRelativeError[j] = std::max(maxRelativeError[j], worstError / std::max(1.0, std::abs(expected[j])));
        // Float return rounding plus double reduction order. No timing assertions.
        const double tolerance = std::max(1e-6, std::abs(expected[j]) * 2e-7);
        if (!(error <= tolerance) || !(scalarError <= tolerance)) {
            std::printf("FAIL %s n=%zu pattern=%d offset=%zu expected=%.17g got=%.9g scalar=%.9g\n",
                names[j], count, pattern, offset, expected[j], actual[j], scalarValues[j]);
            ++failures;
        }
    }
}
void minMaxSpecialValues() {
    const float nan = numeric_limits<float>::quiet_NaN();
    const float inf = numeric_limits<float>::infinity();
    // Every position covers leading NaN, vector lanes/reduction, and scalar tails.
    for (size_t count : {1u, 3u, 17u, 65u}) {
        vector<float> values(count, 1.0f);
        values.front() = -100.0f;
        values.back() = 100.0f;
        for (size_t i = 0; i < count; ++i) {
            const float saved = values[i];
            values[i] = nan;
            const auto bounds = minMax(values);
            const auto reference = simd::scalar::minMax(values.data(), count);
            check(std::isnan(bounds.first) && std::isnan(bounds.second), "minMax propagates any NaN");
            check(std::isnan(reference.first) && std::isnan(reference.second), "scalar minMax propagates any NaN");
            values[i] = saved;
        }
    }
    for (const auto& values : {vector<float>{-inf, 1.0f, inf}, vector<float>(65, inf),
                               vector<float>(65, -inf)}) {
        const pair<float, float> expected{values.front(), values.back()};
        check(minMax(values) == expected, "minMax keeps infinities as bounds");
        check(simd::scalar::minMax(values.data(), values.size()) == expected,
              "scalar minMax keeps infinities as bounds");
    }
}
void helpers() {
    vector<float> first;
    vector<int> cards(101);
    std::iota(cards.begin(), cards.end(), 0);
    randomSeed(1475);
    for (int i = 0; i < 101; ++i) first.push_back(i % 2 ? randomGaussian() : randomGaussian(3.0f, 2.0f));
    shuffle(cards);
    const float uniform = random(1.0f);
    randomSeed(1475);
    for (int i = 0; i < 101; ++i)
        check(first[i] == (i % 2 ? randomGaussian() : randomGaussian(3.0f, 2.0f)), "seeded Gaussian");
    check(randomGaussian(3.0f, 0.0f) == 3.0f, "Gaussian stddev 0 returns mean");
    check(randomGaussian(3.0f, -2.0f) == 3.0f, "Gaussian negative stddev returns mean");
    vector<int> repeated(101);
    std::iota(repeated.begin(), repeated.end(), 0);
    shuffle(repeated);
    check(repeated == cards && random(1.0f) == uniform, "seeded shuffle and mixed engine sequence");
    std::sort(cards.begin(), cards.end());
    for (int i = 0; i < 101; ++i) check(cards[i] == i, "shuffle permutation");
    vector<int> empty; shuffle(empty);
    vector<int> single{42}; shuffle(single); check(single[0] == 42, "singleton shuffle");
    vector<bool> bits{true, false, true}; shuffle(bits);
    check(std::count(bits.begin(), bits.end(), true) == 2, "vector<bool> shuffle");

    constexpr int samples = 200000;
    for (int mode : {0, 1}) {
        randomSeed(42);
        const double expectedMean = mode ? 3.0 : 0.0;
        const double expectedStddev = mode ? 2.0 : 1.0;
        double total = 0.0, squareTotal = 0.0;
        for (int i = 0; i < samples; ++i) {
            const double value = mode ? randomGaussian(3.0f, 2.0f) : randomGaussian();
            total += value; squareTotal += value * value;
        }
        const double average = total / samples;
        const double deviation = std::sqrt(squareTotal / samples - average * average);
        std::printf("Gaussian mode=%d n=%d mean=%.9g stddev=%.9g\n", mode, samples, average, deviation);
        check(std::abs(average - expectedMean) < 0.025 && std::abs(deviation - expectedStddev) < 0.025,
              "Gaussian moments");
    }
    for (auto edges : {array<float, 2>{0.0f, 1.0f}, array<float, 2>{0.2f, 0.8f}}) {
        for (auto point : {array<float, 2>{-1.0f, 0.0f}, {0.0f, 0.0f}, {0.25f, 0.15625f},
                           {0.5f, 0.5f}, {0.75f, 0.84375f}, {1.0f, 1.0f}, {2.0f, 1.0f}}) {
            const float x = edges[0] + point[0] * (edges[1] - edges[0]);
            check(std::abs(smoothstep(edges[0], edges[1], x) - point[1]) < 2e-7f, "GLSL smoothstep");
        }
    }
    check(sigmoid(0.0f) == 0.5f, "sigmoid midpoint");
    check(std::abs(sigmoid(1.0f) - 0.73105858f) < 1e-7f, "sigmoid value");
    check(std::abs(sigmoid(-1.0f) - (1.0f - sigmoid(1.0f))) < 1e-7f, "sigmoid symmetry");
    check(sigmoid(-1000.0f) == 0.0f && sigmoid(1000.0f) == 1.0f, "sigmoid extremes");
    check(sigmoid(-numeric_limits<float>::infinity()) == 0.0f &&
          sigmoid(numeric_limits<float>::infinity()) == 1.0f, "sigmoid infinities");
    check(std::isnan(sigmoid(numeric_limits<float>::quiet_NaN())), "sigmoid NaN");

    // All statistics accept app vectors/arrays directly through span conversion.
    const vector<float> values{1, 2, 3};
    const array<float, 3> other{1, 2, 3};
    check(sum(values) == 6 && mean(other) == 2 && mse(values, other) == 0, "app ADL calls");
    check(minMax(values) == pair<float, float>{1, 3}, "app minMax");
    const float largest = numeric_limits<float>::max();
    const array<float, 2> extremes{largest, -largest};
    const array<float, 2> constant{largest, largest};
    check(mean(constant) == largest && rms(constant) == largest && variance(constant) == 0,
          "double accumulation avoids float intermediate overflow");
    check(stddev(extremes) == largest, "stddev avoids float variance overflow");
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("mse") != string::npos) ++warnings;
    });
    for (int i = 0; i < 100; ++i)
        check(mse(values, span<const float>{}) == 0.0f, "mse mismatch returns zero");
    check(warnings == 1, "mse repeated mismatches warn once per process");
}
void scalarStatistics() {
    const float nan = numeric_limits<float>::quiet_NaN();
    const float inf = numeric_limits<float>::infinity();
    const vector<float> empty, one{7}, equal{7, 7, 7}, values{9, 1, 5, 3};
    const auto original = values;
    check(!median(empty) && !percentile(empty, 0.5), "selection empty");
    check(median(one) == 7 && median(equal) == 7, "selection singleton and equal");
    check(median(values) == 4 && median(vector<float>{9, 1, 5}) == 5, "even and odd median");
    check(percentile(values, 0) == 1 && percentile(values, 1) == 9 &&
          percentile(values, 0.25) == 2.5f, "percentile endpoints and interpolation");
    check(values == original, "selection preserves input order");
    check(!percentile(values, -0.1) && !percentile(values, 1.1) &&
          !percentile(values, nan) && !percentile(values, inf), "invalid percentile");
    check(median(vector<float>{nan, 9, nan, 1}) == 5 && !median(vector<float>{nan}),
          "selection ignores NaN");
    check(median(vector<float>{inf, inf}) == inf && median(vector<float>{-inf, 1}) == -inf &&
          !median(vector<float>{-inf, inf}), "selection infinity interpolation");
    const float largest = numeric_limits<float>::max();
    check(median(vector<float>{largest, largest}) == largest &&
          median(vector<float>{-largest, largest}) == 0, "median avoids float overflow");

    check(histogram(empty, 3, 0, 1) == optional<vector<size_t>>(vector<size_t>{0, 0, 0}), "histogram empty");
    const vector<float> edges{-1, 0, 0.25f, 0.5f, nextafter(1.0f, 0.0f), 1, 2, nan, inf, -inf};
    check(histogram(edges, 4, 0, 1) == optional<vector<size_t>>(vector<size_t>{1, 1, 1, 1}),
          "histogram half-open edges and skipped values");
    check(histogram(one, 1, 0, 8) == optional<vector<size_t>>(vector<size_t>{1}) &&
          histogram(equal, 2, 0, 8) == optional<vector<size_t>>(vector<size_t>{0, 3}), "histogram singleton and equal");
    check(!histogram(values, 0, 0, 1) && !histogram(values, 2, 1, 1) &&
          !histogram(values, 2, 2, 1) && !histogram(values, 2, nan, 1) &&
          !histogram(values, 2, 0, inf), "histogram invalid parameters");
    vector<size_t> expected22(22, 0), expected49(49, 0);
    expected22[15] = 1;
    expected49[1] = 1;
    check(histogram(vector<float>{15}, 22, 0, 22) == optional<vector<size_t>>(expected22),
          "histogram exact edge 15 in 22 bins");
    check(histogram(vector<float>{1}, 49, 0, 49) == optional<vector<size_t>>(expected49),
          "histogram exact edge 1 in 49 bins");
    check(histogram(vector<float>{-1e-20f}, 2, -1, 1) == optional<vector<size_t>>(vector<size_t>{1, 0}),
          "histogram just below internal edge");
    bool integerEdges = true;
    for (size_t bins = 2; bins <= 512 && integerEdges; ++bins) {
        vector<size_t> expected(bins, 0);
        for (size_t i = 0; i < bins; ++i) {
            expected[i] = 1;
            if (histogram(array<float, 1>{float(i)}, bins, 0, float(bins)) !=
                optional<vector<size_t>>(expected)) {
                std::printf("Histogram first integer-edge mismatch: bins=%zu, value=%zu\n", bins, i);
                integerEdges = false;
                break;
            }
            expected[i] = 0;
        }
    }
    check(integerEdges, "histogram integer edges for bins 2..512");

    check(!correlation(empty, empty) && !fitLine(empty, empty), "paired empty");
    check(!correlation(one, one) && !fitLine(one, one), "paired singleton");
    check(!correlation(values, one) && !fitLine(values, one), "paired mismatch");
    check(!correlation(equal, equal) && !fitLine(equal, equal), "paired constant x");
    const vector<float> x{0, 1, 2}, y{3, 5, 7}, negative{3, 1, -1}, flat{3, 3, 3};
    const auto line = fitLine(x, y);
    check(line && line->slope == 2 && line->intercept == 3, "exact line");
    check(correlation(x, y) == 1 && correlation(x, negative) == -1, "perfect correlations");
    const auto horizontal = fitLine(x, flat);
    check(!correlation(x, flat) && horizontal && horizontal->slope == 0 && horizontal->intercept == 3,
          "constant y fits horizontal line");
    const vector<float> offsetX{1000000, 1000001, 1000002, 1000003};
    const vector<float> offsetY{1000000, 1000003, 1000006, 1000009};
    const auto offsetLine = fitLine(offsetX, offsetY);
    check(offsetLine && abs(offsetLine->slope - 3) < 1e-12 &&
          abs(offsetLine->intercept + 2000000) < 1e-9 &&
          abs(correlation(offsetX, offsetY).value_or(0) - 1) < 1e-12, "centered large-offset accuracy");
    const vector<float> noisy{1, 2, 2};
    const auto noisyLine = fitLine(x, noisy);
    check(noisyLine && abs(noisyLine->slope - 0.5) < 1e-12 &&
          abs(noisyLine->intercept - 7.0 / 6.0) < 1e-12 &&
          abs(correlation(x, noisy).value_or(0) - sqrt(0.75)) < 1e-12, "hand-computed noisy fit");
    for (float invalid : {nan, inf, -inf}) {
        for (size_t index = 0; index < x.size(); ++index) {
            auto mixed = x;
            mixed[index] = invalid;
            check(!correlation(mixed, y) && !correlation(x, mixed) &&
                  !fitLine(mixed, y) && !fitLine(x, mixed), "paired rejects nonfinite input");
        }
    }
    // std::ranges is present without importing its names; tc::simd stays qualified.
    check(std::ranges::is_sorted(x) && simd::scalar::sum(x.data(), x.size()) == 3,
          "app names coexist with ranges and simd");
}
volatile float benchSink = 0.0f;
template <typename F> double measure(F fn) {
    constexpr int iterations = 100;
    benchSink = fn();
    const auto begin = chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) benchSink = fn();
    return chrono::duration<double, std::micro>(chrono::steady_clock::now() - begin).count() / iterations;
}
void benchmark() {
    std::printf("Highway target: %s\n", simd::targetName());
    vector<float> a(1000000), b(a.size());
    for (size_t i = 0; i < a.size(); ++i) { a[i] = float(int(i % 2001) - 1000) / 32; b[i] = -a[i] / 2; }
    using Kernel = float (*)(const float*, size_t);
    const array<Kernel, 5> dispatched = {simd::sum, simd::mean, simd::variance, simd::stddev, simd::rms};
    const array<Kernel, 5> references = {simd::scalar::sum, simd::scalar::mean, simd::scalar::variance,
        simd::scalar::stddev, simd::scalar::rms};
    const array<const char*, 5> labels = {"sum", "mean", "variance", "stddev", "rms"};
    std::printf("Benchmark: 1M floats, 100 iterations, us/call (no timing assertions)\n");
    for (size_t i = 0; i < dispatched.size(); ++i) {
        const double baseline = measure([&] { return references[i](a.data(), a.size()); });
        const double optimized = measure([&] { return dispatched[i](a.data(), a.size()); });
        std::printf("%s scalar=%.3f SIMD=%.3f speedup=%.2fx\n", labels[i], baseline, optimized, baseline / optimized);
    }
    const double boundScalar = measure([&] { auto v = simd::scalar::minMax(a.data(), a.size()); return v.first + v.second; });
    const double boundSimd = measure([&] { auto v = simd::minMax(a.data(), a.size()); return v.first + v.second; });
    std::printf("minMax scalar=%.3f SIMD=%.3f speedup=%.2fx\n", boundScalar, boundSimd, boundScalar / boundSimd);
    const double mseScalar = measure([&] { return simd::scalar::mse(a.data(), b.data(), a.size()); });
    const double mseSimd = measure([&] { return simd::mse(a.data(), b.data(), a.size()); });
    std::printf("mse scalar=%.3f SIMD=%.3f speedup=%.2fx\n", mseScalar, mseSimd, mseScalar / mseSimd);
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    helpers();
    scalarStatistics();
    minMaxSpecialValues();
    // Covers float/double lanes and four-vector boundaries for scalar, SSE,
    // AVX2, AVX512 and NEON (double lanes = 1, 2, 4, 8), including 8*lanes+3.
    for (size_t count : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 11u, 15u, 16u, 17u,
                         19u, 31u, 32u, 33u, 35u, 65u, 67u, 1000000u})
        for (int pattern = 0; pattern < 4; ++pattern)
            for (size_t offset : {0u, 1u}) statistics(count, pattern, offset);
    for (size_t j = 0; j < names.size(); ++j) std::printf("max float error %s: absolute=%.9g scaled=%.9g\n", names[j], maxError[j], maxRelativeError[j]);
    if (argc == 2 && std::strcmp(argv[1], "--bench") == 0) benchmark();
    std::printf("mathUtils: %d failures\n", failures);
    return failures ? 1 : 0;
}
