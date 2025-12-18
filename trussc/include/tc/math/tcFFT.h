#pragma once

// =============================================================================
// TrussC FFT
// 高速フーリエ変換（Cooley-Tukey 基数2アルゴリズム）
//
// 使用例:
//   vector<complex<float>> data(1024);
//   // ... data にサンプルを設定 ...
//   tc::fft(data);                    // FFT
//   tc::ifft(data);                   // 逆FFT
//
//   // 実数信号用
//   vector<float> signal(1024);
//   auto spectrum = tc::fftReal(signal);
//   auto magnitudes = tc::fftMagnitude(spectrum);
//
//   // 窓関数
//   tc::applyWindow(signal, tc::WindowType::Hanning);
// =============================================================================

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

#include "../utils/tcLog.h"

namespace trussc {

// ---------------------------------------------------------------------------
// 定数
// ---------------------------------------------------------------------------
constexpr float FFT_PI = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// 窓関数の種類
// ---------------------------------------------------------------------------
enum class WindowType {
    Rectangle,  // 矩形窓（窓なし）
    Hanning,    // ハニング窓
    Hamming,    // ハミング窓
    Blackman    // ブラックマン窓
};

// ---------------------------------------------------------------------------
// 窓関数
// ---------------------------------------------------------------------------

// 窓関数の値を取得
inline float windowFunction(WindowType type, int i, int n) {
    float t = (float)i / (n - 1);
    switch (type) {
        case WindowType::Rectangle:
            return 1.0f;
        case WindowType::Hanning:
            return 0.5f * (1.0f - std::cos(2.0f * FFT_PI * t));
        case WindowType::Hamming:
            return 0.54f - 0.46f * std::cos(2.0f * FFT_PI * t);
        case WindowType::Blackman:
            return 0.42f - 0.5f * std::cos(2.0f * FFT_PI * t)
                        + 0.08f * std::cos(4.0f * FFT_PI * t);
        default:
            return 1.0f;
    }
}

// 信号に窓関数を適用
inline void applyWindow(std::vector<float>& signal, WindowType type) {
    int n = static_cast<int>(signal.size());
    for (int i = 0; i < n; i++) {
        signal[i] *= windowFunction(type, i, n);
    }
}

inline void applyWindow(std::vector<std::complex<float>>& signal, WindowType type) {
    int n = static_cast<int>(signal.size());
    for (int i = 0; i < n; i++) {
        signal[i] *= windowFunction(type, i, n);
    }
}

// ---------------------------------------------------------------------------
// ユーティリティ
// ---------------------------------------------------------------------------

// 2のべき乗かどうか
inline bool isPowerOfTwo(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// n 以上の最小の2のべき乗
inline int nextPowerOfTwo(int n) {
    int p = 1;
    while (p < n) p *= 2;
    return p;
}

// ビットリバース
inline int bitReverse(int x, int bits) {
    int result = 0;
    for (int i = 0; i < bits; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

// ビット数を取得
inline int getBits(int n) {
    int bits = 0;
    while ((1 << bits) < n) bits++;
    return bits;
}

// ---------------------------------------------------------------------------
// FFT (Cooley-Tukey 基数2 時間間引き)
// ---------------------------------------------------------------------------

// インプレース FFT
inline void fft(std::vector<std::complex<float>>& data) {
    int n = static_cast<int>(data.size());
    if (n <= 1) return;

    // 2のべき乗でなければエラー
    if (!isPowerOfTwo(n)) {
        tcLogError() << "FFT: size must be power of 2 (got " << n << ")";
        return;
    }

    int bits = getBits(n);

    // ビットリバース並び替え
    for (int i = 0; i < n; i++) {
        int j = bitReverse(i, bits);
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // バタフライ演算
    for (int len = 2; len <= n; len *= 2) {
        float angle = -2.0f * FFT_PI / len;
        std::complex<float> wn(std::cos(angle), std::sin(angle));

        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = data[i + j];
                std::complex<float> t = w * data[i + j + len / 2];
                data[i + j] = u + t;
                data[i + j + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

// インプレース 逆FFT
inline void ifft(std::vector<std::complex<float>>& data) {
    int n = static_cast<int>(data.size());
    if (n <= 1) return;

    // 共役を取る
    for (auto& x : data) {
        x = std::conj(x);
    }

    // FFT
    fft(data);

    // 共役を取って正規化
    for (auto& x : data) {
        x = std::conj(x) / (float)n;
    }
}

// ---------------------------------------------------------------------------
// 実数信号用の便利関数
// ---------------------------------------------------------------------------

// 実数信号 → 複素数配列
inline std::vector<std::complex<float>> toComplex(const std::vector<float>& real) {
    std::vector<std::complex<float>> result(real.size());
    for (size_t i = 0; i < real.size(); i++) {
        result[i] = std::complex<float>(real[i], 0.0f);
    }
    return result;
}

// 実数信号の FFT
inline std::vector<std::complex<float>> fftReal(const std::vector<float>& signal) {
    auto data = toComplex(signal);
    fft(data);
    return data;
}

// 実数信号の FFT（窓関数付き）
inline std::vector<std::complex<float>> fftReal(const std::vector<float>& signal, WindowType window) {
    std::vector<float> windowed = signal;
    applyWindow(windowed, window);
    return fftReal(windowed);
}

// ---------------------------------------------------------------------------
// スペクトル解析
// ---------------------------------------------------------------------------

// マグニチュード（振幅）を取得
inline std::vector<float> fftMagnitude(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> mag(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); i++) {
        mag[i] = std::abs(spectrum[i]);
    }
    return mag;
}

// マグニチュード（dB）を取得
inline std::vector<float> fftMagnitudeDb(const std::vector<std::complex<float>>& spectrum, float minDb = -100.0f) {
    std::vector<float> db(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); i++) {
        float mag = std::abs(spectrum[i]);
        if (mag > 0) {
            db[i] = 20.0f * std::log10(mag);
            if (db[i] < minDb) db[i] = minDb;
        } else {
            db[i] = minDb;
        }
    }
    return db;
}

// 位相を取得
inline std::vector<float> fftPhase(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> phase(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); i++) {
        phase[i] = std::arg(spectrum[i]);
    }
    return phase;
}

// パワースペクトル（マグニチュードの2乗）
inline std::vector<float> fftPower(const std::vector<std::complex<float>>& spectrum) {
    std::vector<float> power(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); i++) {
        power[i] = std::norm(spectrum[i]);  // |z|^2
    }
    return power;
}

// 周波数ビンのインデックスから周波数を取得
inline float binToFrequency(int bin, int fftSize, int sampleRate) {
    return (float)bin * sampleRate / fftSize;
}

// 周波数からビンのインデックスを取得
inline int frequencyToBin(float freq, int fftSize, int sampleRate) {
    return (int)(freq * fftSize / sampleRate + 0.5f);
}

} // namespace trussc

namespace tc = trussc;
