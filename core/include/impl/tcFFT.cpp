#include "tc/math/tcFFT.h"
#include <array>

namespace trussc::internal {

const std::vector<float>* windowWeights(int n, WindowType type) {
    thread_local bool destroyed = false;
    if (destroyed) return nullptr;
    struct Cache {
        int size = -1;
        std::array<std::vector<float>, 4> weights;
        ~Cache() { destroyed = true; }
    };
    thread_local Cache cache;
    if (cache.size != n) {
        cache.size = n;
        for (auto& weights : cache.weights) weights.clear();
    }
    int index = static_cast<int>(type);
    if (index < 0 || index >= 4) index = 0; // windowFunction's default is rectangular.
    auto& weights = cache.weights[index];
    if (weights.empty()) {
        weights.resize(n);
        for (int i = 0; i < n; ++i) weights[i] = windowFunction(type, i, n);
    }
    return &weights;
}

const std::vector<std::complex<float>>* fftTwiddles(int n) {
    thread_local bool destroyed = false;
    if (destroyed) return nullptr;
    struct Cache {
        int size = 0;
        std::vector<std::complex<float>> twiddles;
        ~Cache() { destroyed = true; }
    };
    thread_local Cache cache;
    if (cache.size != n) {
        cache.twiddles.resize(n - 1);
        for (int len = 2; len <= n; len *= 2) {
            float angle = -TAU / len;
            std::complex<float> wn(std::cos(angle), std::sin(angle));
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                cache.twiddles[len / 2 - 1 + j] = w;
                w *= wn;
            }
        }
        cache.size = n;
    }
    return &cache.twiddles;
}

} // namespace trussc::internal
