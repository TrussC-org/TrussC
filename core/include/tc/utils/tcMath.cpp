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
std::optional<float> percentile(std::span<const float> values, double p) {
    if (!(p >= 0.0 && p <= 1.0)) return std::nullopt;
    std::vector<float> sorted;
    sorted.reserve(values.size());
    for (float value : values) {
        if (!std::isnan(value)) sorted.push_back(value);
    }
    if (sorted.empty()) return std::nullopt;
    const double rank = p * double(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(rank);
    std::nth_element(sorted.begin(), sorted.begin() + lower, sorted.end());
    const double a = sorted[lower];
    const double fraction = rank - double(lower);
    if (fraction == 0.0) return static_cast<float>(a);
    const double b = *std::min_element(sorted.begin() + lower + 1, sorted.end());
    // Equal infinities and exact ranks have well-defined values.
    if (std::isinf(a) || std::isinf(b)) {
        if (a == -b) return std::nullopt;
        return static_cast<float>(std::isinf(a) ? a : b);
    }
    const double result = std::lerp(a, b, fraction);
    if (std::isnan(result)) return std::nullopt;
    return static_cast<float>(result);
}

std::optional<float> median(std::span<const float> values) {
    return percentile(values, 0.5);
}

std::optional<std::vector<std::size_t>> histogram(
    std::span<const float> values, std::size_t bins, float low, float high) {
    if (bins == 0 || !std::isfinite(low) || !std::isfinite(high) || !(low < high))
        return std::nullopt;
    std::vector<std::size_t> counts(bins, 0);
    const double width = double(high) - double(low);
    for (float value : values) {
        if (!std::isfinite(value) || value < low || value >= high) continue;
        const double position = (double(value) - double(low)) / width * double(bins);
        // Rounding can place a value immediately below high at bins.
        auto index = position >= double(bins) ? bins - 1 : static_cast<std::size_t>(position);
        // Compare with the defined edges to correct rounding in either direction.
        if (index > 0 && double(value) < double(low) + double(index) * width / double(bins))
            --index;
        else if (index + 1 < bins &&
                 double(value) >= double(low) + double(index + 1) * width / double(bins))
            ++index;
        ++counts[index];
    }
    return counts;
}

namespace {
struct CenteredMoments {
    double meanX = 0.0, meanY = 0.0;
    double xx = 0.0, yy = 0.0, xy = 0.0;
};
std::optional<CenteredMoments> centeredMoments(std::span<const float> x, std::span<const float> y) {
    if (x.size() != y.size() || x.size() < 2) return std::nullopt;
    CenteredMoments moments;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) return std::nullopt;
        moments.meanX += double(x[i]);
        moments.meanY += double(y[i]);
    }
    moments.meanX /= double(x.size());
    moments.meanY /= double(y.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = double(x[i]) - moments.meanX;
        const double dy = double(y[i]) - moments.meanY;
        moments.xx += dx * dx;
        moments.yy += dy * dy;
        moments.xy += dx * dy;
    }
    return moments;
}
} // namespace

std::optional<double> correlation(std::span<const float> x, std::span<const float> y) {
    const auto moments = centeredMoments(x, y);
    if (!moments || moments->xx == 0.0 || moments->yy == 0.0) return std::nullopt;
    return std::clamp(moments->xy / std::sqrt(moments->xx * moments->yy), -1.0, 1.0);
}

std::optional<LineFit> fitLine(std::span<const float> x, std::span<const float> y) {
    const auto moments = centeredMoments(x, y);
    if (!moments || moments->xx == 0.0) return std::nullopt;
    const double slope = moments->xy / moments->xx;
    return LineFit{slope, moments->meanY - slope * moments->meanX};
}
} // namespace trussc
