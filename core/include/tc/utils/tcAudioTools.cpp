#include <TrussC.h>
#include "tcAudioTools.h"
#include "tc/sound/tcAudioAnalysis.h"
#include "tc/sound/tcAudioRecorder.h"
#include "tc/math/tcFFT.h"

namespace trussc::mcp::detail {
namespace {
json error(const std::string& message) {
    return {{"status", "error"}, {"message", message}};
}

bool number(const json& args, const char* key) {
    return !args.contains(key) || (args[key].is_number() && std::isfinite(args[key].get<double>()));
}

json decibels(double amplitude) {
    // JSON cannot represent -infinity. Null denotes exactly zero amplitude.
    if (!(amplitude > 0)) return nullptr;
    return std::round(200.0 * std::log10(amplitude)) / 10.0;
}

json spectrum(const json& args) {
    if (args.contains("n") && !args["n"].is_number_integer()) return error("n must be an integer");
    const auto nValue = args.value("n", 1024.0);
    if (nValue < 64 || nValue > double(std::numeric_limits<int>::max()))
        return error("n must be a power of two from 64 up to the ring length");
    const int n = int(nValue);
    if (!isPowerOfTwo(n)) return error("n must be a power of two from 64 up to the ring length");
    const std::string window = args.value("window", std::string("hann"));
    const std::string channels = args.value("channels", std::string("mix"));
    if (window != "hann" && window != "blackmanharris") return error("window must be hann or blackmanharris");
    if (channels != "mix" && channels != "each") return error("channels must be mix or each");
    if (args.contains("peaks") && !args["peaks"].is_number_integer()) return error("peaks must be a non-negative integer");
    const double peaks = args.value("peaks", 3.0);
    if (peaks < 0) return error("peaks must be a non-negative integer");
    if (!number(args, "fmin") || !number(args, "fmax")) return error("frequency bounds must be finite numbers");

    auto data = internal::AudioAnalysisAccess::snapshot(AudioEngine::getInstance(), n);
    if (!data.channels) return error("Audio output history is unavailable; start audio or retry after a concurrent callback");
    if (size_t(n) > data.capacity) return error("n exceeds the ring length");
    const double binHz = double(data.sampleRate) / n;
    const double fmin = args.value("fmin", 0.0);
    const double fmax = args.value("fmax", double(data.sampleRate) / 2);
    if (fmin < 0 || fmin > fmax || fmax > double(data.sampleRate) / 2)
        return error("frequency range must satisfy 0 <= fmin <= fmax <= sampleRate / 2");
    const int first = int(std::ceil(fmin / binHz));
    const int last = int(std::floor(fmax / binHz));
    const int outputChannels = channels == "mix" ? 1 : data.channels;
    const size_t count = data.samples.size() / data.channels;
    std::vector<double> weights(n);
    double gain = 0;
    for (int i = 0; i < n; ++i) {
        const double phase = double(TAU) * i / (n - 1);
        weights[i] = window == "hann" ? 0.5 - 0.5 * std::cos(phase)
            : 0.35875 - 0.48829 * std::cos(phase) + 0.14128 * std::cos(2 * phase) - 0.01168 * std::cos(3 * phase);
        gain += weights[i];
    }
    json results = json::array();
    for (int ch = 0; ch < outputChannels; ++ch) {
        std::vector<float> signal(n, 0);
        double peak = 0, sumSquares = 0;
        for (size_t f = 0; f < count; ++f) {
            double sample = 0;
            if (channels == "mix") {
                for (int c = 0; c < data.channels; ++c) sample += data.samples[f * data.channels + c];
                sample /= data.channels;
            } else sample = data.samples[f * data.channels + ch];
            if (!std::isfinite(sample)) sample = 0; // never feed NaN to the FFT
            peak = std::max(peak, std::abs(sample));
            sumSquares += sample * sample;
            signal[n - count + f] = float(sample * weights[n - count + f]);
        }
        const auto transformed = fftReal(signal);
        std::vector<double> amplitudes(n / 2 + 1);
        json bins = json::array();
        std::vector<int> maxima;
        for (int k = 0; k <= n / 2; ++k) {
            amplitudes[k] = std::abs(transformed[k]) / gain * ((k == 0 || k == n / 2) ? 1 : 2);
            if (k >= first && k <= last) bins.push_back(decibels(amplitudes[k]));
        }
        for (int k = first; k <= last; ++k) {
            if (amplitudes[k] > 0 && (k == 0 || amplitudes[k] > amplitudes[k - 1]) &&
                (k == n / 2 || amplitudes[k] >= amplitudes[k + 1])) maxima.push_back(k);
        }
        std::sort(maxima.begin(), maxima.end(), [&](int a, int b) { return amplitudes[a] > amplitudes[b]; });
        json strongest = json::array();
        const size_t peakCount = size_t(std::min(peaks, double(maxima.size())));
        for (size_t i = 0; i < peakCount; ++i) {
            const int k = maxima[i];
            double offset = 0, amplitude = amplitudes[k];
            if (k > 1 && k < n / 2 - 1 && amplitudes[k - 1] > 0 && amplitudes[k + 1] > 0) {
                const double a = std::log(amplitudes[k - 1]), b = std::log(amplitude), c = std::log(amplitudes[k + 1]);
                const double curvature = a - 2 * b + c;
                if (curvature < 0) {
                    offset = std::clamp(0.5 * (a - c) / curvature, -0.5, 0.5);
                    amplitude = std::exp(b - 0.25 * (a - c) * offset);
                }
            }
            strongest.push_back({{"hz", (k + offset) * binHz}, {"dbfs", decibels(amplitude)}});
        }
        results.push_back({{"peak", peak}, {"rms", std::sqrt(sumSquares / n)},
                           {"peaks", strongest}, {"spectrum", bins}});
    }
    return {{"sampleRate", data.sampleRate}, {"n", n}, {"binHz", binHz},
            {"framesWritten", data.framesWritten}, {"channels", results}};
}

json capture(const json& args) {
    const std::string path = args.at("path").get<std::string>();
    if (path.empty()) return error("path must not be empty");
    if (!number(args, "seconds")) return error("seconds must be a finite positive number");
    const double seconds = args.value("seconds", 1.0);
    if (seconds <= 0) return error("seconds must be a finite positive number");
    auto data = internal::AudioAnalysisAccess::snapshot(AudioEngine::getInstance(), std::numeric_limits<size_t>::max());
    if (!data.channels || data.samples.empty()) return error("Audio output history is unavailable or empty; retry after audio has been produced");
    const size_t requested = size_t(std::min(seconds, 2.0) * data.sampleRate);
    const size_t frames = std::min(requested, data.samples.size() / data.channels);
    if (!frames) return error("seconds must include at least one audio frame");
    auto destination = internal::utf8ToPath(path);
    if (destination.is_relative()) destination = getDataPath(destination);
    std::string extension = internal::pathToUtf8(destination.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".wav") {
        destination += ".wav";
        logWarning("MCP") << "tc_save_audio_capture: appending .wav to " << internal::pathToDisplayUtf8(destination);
    }
    std::error_code ec;
    if (destination.has_parent_path()) fs::create_directories(destination.parent_path(), ec);
    if (ec) return error("Could not create audio capture directory: " + ec.message());
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) return error("Could not open audio capture file");
    const auto header = internal::writeWavHeader(out, data.sampleRate, data.channels, true);
    const size_t samples = frames * size_t(data.channels);
    out.write(reinterpret_cast<const char*>(data.samples.data() + data.samples.size() - samples),
              std::streamsize(samples * sizeof(float)));
    internal::patchWavHeader(out, header, frames, data.channels, true);
    out.close();
    if (!out) return error("Could not write audio capture file");
    return {{"path", internal::pathToUtf8(destination)}, {"sampleRate", data.sampleRate},
            {"channels", data.channels}, {"frames", frames}, {"framesWritten", data.framesWritten}};
}
} // namespace

void registerAudioTools() {
    tool("tc_get_audio_spectrum", "Full post-clamp output spectrum, read-only; never starts the engine. Returns sampleRate, n, binHz, framesWritten (since last init), and channels [{peak, rms, peaks [{hz, dbfs}], spectrum}]. Spectrum values are dBFS rounded to one decimal; null means silence (-infinity). Missing startup frames are zero padded. Frequency bounds select inclusive FFT bins; peak frequencies are interpolated.")
        .arg<int>("n", "Power of two from 64 up to the two-second ring length (default 1024)", false)
        .arg<std::string>("window", "hann (default) or blackmanharris", false)
        .arg<std::string>("channels", "mix (default, all-channel average) or each", false)
        .arg<double>("fmin", "Lowest returned frequency in Hz (default 0)", false)
        .arg<double>("fmax", "Highest returned frequency in Hz (default Nyquist)", false)
        .arg<int>("peaks", "Number of strongest peaks (default 3)", false)
        .bind(spectrum);
    tool("tc_save_audio_capture", "Save the output history ending at the call as float32 WAV; never starts the engine or waits for new audio. Returns path, sampleRate, channels, frames, framesWritten. Relative paths use the data folder; missing folders are created; a missing .wav suffix is appended with a warning. History is capped at two seconds; use AudioRecorder for longer recordings.")
        .arg<std::string>("path", "Output WAV file path")
        .arg<double>("seconds", "Seconds of history (default 1.0, capped by available history)", false)
        .bind(capture);
}
} // namespace trussc::mcp::detail
