#include "tcMath.h"
#include "tcSimd.h"
#include "tcLog.h"
#include "tcOnceGate.h"

namespace trussc {
float sum(std::span<const float> values) { return simd::sum(values.data(), values.size()); }
float mean(std::span<const float> values) { return simd::mean(values.data(), values.size()); }
float variance(std::span<const float> values) { return simd::variance(values.data(), values.size()); }
float stddev(std::span<const float> values) { return simd::stddev(values.data(), values.size()); }
std::pair<float, float> minMax(std::span<const float> values) {
    return simd::minMax(values.data(), values.size());
}
float rms(std::span<const float> values) { return simd::rms(values.data(), values.size()); }
float mse(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size()) {
        static OnceGate warned;
        if (warned.isFirstTime())
            logWarning("Math") << "mse: input lengths differ (" << a.size() << ", " << b.size() << ")";
        return 0.0f;
    }
    return simd::mse(a.data(), b.data(), a.size());
}
} // namespace trussc
