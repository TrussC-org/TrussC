#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <bitset>
#include <cstdint>
#include <unordered_map>
#include <cmath>
#include <memory>
#include <filesystem>
#include "../sound/tcSound.h"

// Forward declaration for tcPlatform.h (avoid circular include)
namespace trussc {
    std::string getExecutableDir();
}

namespace trussc {

// Forward declaration for frame count access
namespace internal {
    extern uint64_t updateFrameCount;
}

// ---------------------------------------------------------------------------
// Data Path (similar to oF's ofToDataPath)
// ---------------------------------------------------------------------------

namespace internal {
    // Default is "data/" relative to executable directory
    // On macOS, executable is in xxx.app/Contents/MacOS/, so "../../../data/" points to bin/data/
    #ifdef __APPLE__
    inline std::string dataPathRoot = "../../../data/";
    #else
    inline std::string dataPathRoot = "data/";
    #endif
    inline bool dataPathRootIsAbsolute = false;
}

// Set the data path root
// If relative path, resolved relative to executable directory
// If absolute path (starts with /), used as-is
inline void setDataPathRoot(const std::string& path) {
    internal::dataPathRoot = path;
    // Add trailing slash if missing
    if (!internal::dataPathRoot.empty() && internal::dataPathRoot.back() != '/') {
        internal::dataPathRoot += '/';
    }
    // Record whether path is absolute
    internal::dataPathRootIsAbsolute = (!path.empty() && path[0] == '/');
}

// Get the data path root
inline std::string getDataPathRoot() {
    return internal::dataPathRoot;
}

// Get data path for a filename
// - If filename is absolute, return as-is
// - Otherwise, resolved relative to executable directory + dataPathRoot
inline std::string getDataPath(const std::string& filename) {
    // If filename is absolute, return as-is (like oF)
    // Uses std::filesystem for cross-platform support (Unix: /path, Windows: C:\path)
    if (!filename.empty() && std::filesystem::path(filename).is_absolute()) {
        return filename;
    }

    if (internal::dataPathRootIsAbsolute) {
        // Absolute dataPathRoot: use as-is
        return internal::dataPathRoot + filename;
    } else {
        // Relative path: resolve relative to executable directory
        return getExecutableDir() + internal::dataPathRoot + filename;
    }
}

// For macOS bundle distribution: Set data path to Resources folder
// Will reference xxx.app/Contents/Resources/data/
// No-op on non-macOS platforms
inline void setDataPathToResources() {
    #ifdef __APPLE__
    setDataPathRoot("../Resources/data/");
    #endif
}

// ---------------------------------------------------------------------------
// toString - Convert value to string
// ---------------------------------------------------------------------------

// Basic string conversion
template <class T>
std::string toString(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// Convert to string with specified decimal precision
// Example: toString(6.28318, 2) → "6.28"
template <class T>
std::string toString(const T& value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// Convert to string with specified width and fill character
// Example: toString(42, 5, '0') → "00042"
template <class T>
std::string toString(const T& value, int width, char fill) {
    std::ostringstream out;
    out << std::setfill(fill) << std::setw(width) << value;
    return out.str();
}

// Convert to string with precision, width, and fill character
// Example: toString(6.28, 2, 6, '0') → "006.28"
template <class T>
std::string toString(const T& value, int precision, int width, char fill) {
    std::ostringstream out;
    out << std::fixed << std::setfill(fill) << std::setw(width) << std::setprecision(precision) << value;
    return out.str();
}

// Convert vector to string
// Example: toString(vector{1,2,3}) → "{1, 2, 3}"
template <class T>
std::string toString(const std::vector<T>& values) {
    std::ostringstream out;
    out << "{";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out << ", ";
        out << values[i];
    }
    out << "}";
    return out.str();
}

// ---------------------------------------------------------------------------
// String to Number Conversion
// ---------------------------------------------------------------------------

// Convert string to integer
inline int toInt(const std::string& str) {
    try {
        return std::stoi(str);
    } catch (...) {
        return 0;
    }
}

// Convert string to 64-bit integer
inline int64_t toInt64(const std::string& str) {
    try {
        return std::stoll(str);
    } catch (...) {
        return 0;
    }
}

// Convert string to float
inline float toFloat(const std::string& str) {
    try {
        return std::stof(str);
    } catch (...) {
        return 0.0f;
    }
}

// Convert string to double
inline double toDouble(const std::string& str) {
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

// Convert string to bool ("true", "TRUE", "1" → true)
inline bool toBool(const std::string& str) {
    if (str.empty()) return false;
    // Convert to lowercase and compare
    std::string lower = str;
    for (auto& c : lower) c = std::tolower(c);
    return (lower == "true" || lower == "1" || lower == "yes");
}

// ---------------------------------------------------------------------------
// toHex - Convert to hexadecimal string
// ---------------------------------------------------------------------------

// Convert number to hexadecimal string
template <class T>
std::string toHex(const T& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

// Convert integer to hexadecimal string (zero-padded)
inline std::string toHex(int value, int width = 0) {
    std::ostringstream out;
    if (width > 0) {
        out << std::setfill('0') << std::setw(width);
    }
    out << std::hex << std::uppercase << value;
    return out.str();
}

// Convert string to hexadecimal string
inline std::string toHex(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        out << std::setfill('0') << std::setw(2) << std::hex << std::uppercase << (int)c;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// toBinary - Convert to binary string
// ---------------------------------------------------------------------------

// Convert integer to binary string
inline std::string toBinary(int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(unsigned int value) {
    return std::bitset<32>(value).to_string();
}

inline std::string toBinary(char value) {
    return std::bitset<8>(value).to_string();
}

inline std::string toBinary(unsigned char value) {
    return std::bitset<8>(value).to_string();
}

// Convert string to binary string (each character as 8 bits)
inline std::string toBinary(const std::string& value) {
    std::string result;
    for (unsigned char c : value) {
        if (!result.empty()) result += " ";
        result += std::bitset<8>(c).to_string();
    }
    return result;
}

// ---------------------------------------------------------------------------
// fromHex - Convert hexadecimal string to number
// ---------------------------------------------------------------------------

inline int hexToInt(const std::string& hexStr) {
    try {
        return std::stoi(hexStr, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

inline unsigned int hexToUInt(const std::string& hexStr) {
    try {
        return static_cast<unsigned int>(std::stoul(hexStr, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Base64 Encoding
// ---------------------------------------------------------------------------

namespace internal {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
}

inline std::string toBase64(const unsigned char* bytes, size_t len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += internal::base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += internal::base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

inline std::string toBase64(const std::vector<unsigned char>& bytes) {
    return toBase64(bytes.data(), bytes.size());
}

inline std::string toBase64(const std::string& bytes) {
    return toBase64(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

// ---------------------------------------------------------------------------
// String Operations
// ---------------------------------------------------------------------------

/// Check if string contains another string
/// Same as oF's ofIsStringInString
inline bool isStringInString(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Count occurrences of needle in haystack
/// Same as oF's ofStringTimesInString
inline std::size_t stringTimesInString(const std::string& haystack, const std::string& needle) {
    const size_t step = needle.size();
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        pos += step;
        ++count;
    }
    return count;
}

/// Split string by delimiter
/// Same argument order as oF's ofSplitString
/// @param source Source string
/// @param delimiter Delimiter string
/// @param ignoreEmpty Whether to ignore empty elements
/// @param trim Whether to trim whitespace from each element
inline std::vector<std::string> splitString(const std::string& source, const std::string& delimiter, bool ignoreEmpty = false, bool trim = false) {
    std::vector<std::string> result;
    if (delimiter.empty()) {
        result.push_back(source);
        return result;
    }
    std::string::const_iterator substart = source.begin(), subend;
    while (true) {
        subend = std::search(substart, source.end(), delimiter.begin(), delimiter.end());
        std::string sub(substart, subend);
        if (trim) {
            // Remove leading/trailing whitespace
            size_t start = sub.find_first_not_of(" \t\r\n");
            size_t end = sub.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                sub = sub.substr(start, end - start + 1);
            } else if (start == std::string::npos) {
                sub.clear();
            }
        }
        if (!ignoreEmpty || !sub.empty()) {
            result.push_back(sub);
        }
        if (subend == source.end()) {
            break;
        }
        substart = subend + delimiter.size();
    }
    return result;
}

/// Join string array with delimiter
/// Same as oF's ofJoinString
inline std::string joinString(const std::vector<std::string>& stringElements, const std::string& delimiter) {
    std::string str;
    if (stringElements.empty()) {
        return str;
    }
    auto numStrings = stringElements.size();
    std::string::size_type strSize = delimiter.size() * (numStrings - 1);
    for (const std::string& s : stringElements) {
        strSize += s.size();
    }
    str.reserve(strSize);
    str += stringElements[0];
    for (decltype(numStrings) i = 1; i < numStrings; ++i) {
        str += delimiter;
        str += stringElements[i];
    }
    return str;
}

/// Replace occurrences in string (in-place)
/// Same as oF's ofStringReplace
inline void stringReplace(std::string& input, const std::string& searchStr, const std::string& replaceStr) {
    auto pos = input.find(searchStr);
    while (pos != std::string::npos) {
        input.replace(pos, searchStr.size(), replaceStr);
        pos += replaceStr.size();
        std::string nextfind(input.begin() + pos, input.end());
        auto nextpos = nextfind.find(searchStr);
        if (nextpos == std::string::npos) {
            break;
        }
        pos += nextpos;
    }
}

/// Remove leading/trailing whitespace from string
/// Same as oF's ofTrim
inline std::string trim(const std::string& src) {
    size_t start = src.find_first_not_of(" \t\r\n");
    size_t end = src.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    return src.substr(start, end - start + 1);
}

/// Remove leading whitespace from string
/// Same as oF's ofTrimFront
inline std::string trimFront(const std::string& src) {
    size_t start = src.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    return src.substr(start);
}

/// Remove trailing whitespace from string
/// Same as oF's ofTrimBack
inline std::string trimBack(const std::string& src) {
    size_t end = src.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return "";
    }
    return src.substr(0, end + 1);
}

/// Convert string to lowercase
/// Same as oF's ofToLower
inline std::string toLower(const std::string& src) {
    std::string dst = src;
    for (auto& c : dst) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return dst;
}

/// Convert string to uppercase
/// Same as oF's ofToUpper
inline std::string toUpper(const std::string& src) {
    std::string dst = src;
    for (auto& c : dst) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return dst;
}

// ---------------------------------------------------------------------------
// Beep / Debug Sound
// ---------------------------------------------------------------------------

// Preset sound types
enum class Beep {
    // Basic
    ping,       // Single beep (default)

    // Positive feedback
    success,    // Two-tone rising (pico)
    complete,   // Task completion fanfare
    coin,       // Game item pickup (sparkly)

    // Negative feedback
    error,      // Low buzz (boo)
    warning,    // Attention (two short beeps)
    cancel,     // Cancel/back

    // UI feedback
    click,      // UI selection click
    typing,     // Key input feedback
    notify,     // Two-tone notification

    // Transition
    sweep       // Screen transition whoosh
};

namespace beep_internal {

// Cache key for preset sounds (negative to avoid collision with frequencies)
inline int presetToKey(Beep type) {
    return -static_cast<int>(type) - 1;
}

// Internal state manager
struct BeepManager {
    std::unordered_map<int, std::shared_ptr<Sound>> cache;
    uint64_t lastBeepFrame = 0;
    float volume = 0.5f;
    static constexpr size_t MAX_CACHE_SIZE = 128;

    // Generate sound for a preset type
    std::shared_ptr<Sound> generatePreset(Beep type) {
        auto sound = std::make_shared<Sound>();
        SoundBuffer buffer;

        switch (type) {
            case Beep::ping: {
                buffer.generateSineWave(880.0f, 0.08f, volume);
                buffer.applyADSR(0.005f, 0.02f, 0.3f, 0.05f);
                break;
            }
            case Beep::success: {
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.08f, volume);
                b1.applyADSR(0.005f, 0.02f, 0.5f, 0.03f);
                b2.generateSineWave(1100.0f, 0.1f, volume);
                b2.applyADSR(0.005f, 0.02f, 0.5f, 0.05f);
                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.07f * 44100), 1.0f);
                buffer.clip();
                break;
            }
            case Beep::complete: {
                SoundBuffer b1, b2, b3, b4;
                b1.generateSineWave(523.0f, 0.1f, volume * 0.7f);
                b1.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b2.generateSineWave(659.0f, 0.1f, volume * 0.8f);
                b2.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b3.generateSineWave(784.0f, 0.1f, volume * 0.9f);
                b3.applyADSR(0.005f, 0.03f, 0.5f, 0.04f);
                b4.generateSineWave(1047.0f, 0.2f, volume);
                b4.applyADSR(0.005f, 0.05f, 0.6f, 0.1f);
                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.08f * 44100), 1.0f);
                buffer.mixFrom(b3, static_cast<size_t>(0.16f * 44100), 1.0f);
                buffer.mixFrom(b4, static_cast<size_t>(0.24f * 44100), 1.0f);
                buffer.clip();
                break;
            }
            case Beep::coin: {
                SoundBuffer n1_main, n1_oct, n1_det;
                n1_main.generateSineWave(1318.5f, 0.1f, volume * 0.5f);
                n1_main.applyADSR(0.001f, 0.02f, 0.3f, 0.04f);
                n1_oct.generateSineWave(2637.0f, 0.08f, volume * 0.3f);
                n1_oct.applyADSR(0.001f, 0.015f, 0.2f, 0.03f);
                n1_det.generateSineWave(1324.0f, 0.1f, volume * 0.4f);
                n1_det.applyADSR(0.001f, 0.02f, 0.3f, 0.04f);
                SoundBuffer n2_main, n2_oct, n2_det;
                n2_main.generateSineWave(1975.5f, 0.12f, volume * 0.5f);
                n2_main.applyADSR(0.001f, 0.025f, 0.35f, 0.05f);
                n2_oct.generateSineWave(3951.0f, 0.1f, volume * 0.25f);
                n2_oct.applyADSR(0.001f, 0.02f, 0.2f, 0.04f);
                n2_det.generateSineWave(1982.0f, 0.12f, volume * 0.4f);
                n2_det.applyADSR(0.001f, 0.025f, 0.35f, 0.05f);
                buffer = n1_main;
                buffer.mixFrom(n1_oct, 0, 1.0f);
                buffer.mixFrom(n1_det, 0, 1.0f);
                size_t offset = static_cast<size_t>(0.06f * 44100);
                buffer.mixFrom(n2_main, offset, 1.0f);
                buffer.mixFrom(n2_oct, offset, 1.0f);
                buffer.mixFrom(n2_det, offset, 1.0f);
                buffer.clip();
                break;
            }
            case Beep::error: {
                buffer.generateSquareWave(220.0f, 0.25f, volume * 0.4f);
                buffer.applyADSR(0.01f, 0.05f, 0.6f, 0.1f);
                break;
            }
            case Beep::warning: {
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.06f, volume * 0.8f);
                b1.applyADSR(0.002f, 0.02f, 0.5f, 0.02f);
                b2.generateSineWave(880.0f, 0.06f, volume * 0.8f);
                b2.applyADSR(0.002f, 0.02f, 0.5f, 0.02f);
                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.1f * 44100), 1.0f);
                buffer.clip();
                break;
            }
            case Beep::cancel: {
                SoundBuffer b1, b2;
                b1.generateSineWave(440.0f, 0.05f, volume * 0.6f);
                b1.applyADSR(0.002f, 0.02f, 0.4f, 0.02f);
                b2.generateSineWave(330.0f, 0.08f, volume * 0.5f);
                b2.applyADSR(0.002f, 0.02f, 0.3f, 0.04f);
                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.04f * 44100), 1.0f);
                buffer.clip();
                break;
            }
            case Beep::click: {
                buffer.generateSineWave(1200.0f, 0.02f, volume * 0.6f);
                buffer.applyADSR(0.001f, 0.01f, 0.2f, 0.01f);
                break;
            }
            case Beep::typing: {
                buffer.generateSineWave(600.0f, 0.015f, volume * 0.3f);
                buffer.applyADSR(0.001f, 0.005f, 0.2f, 0.005f);
                SoundBuffer noise;
                noise.generateNoise(0.01f, volume * 0.1f);
                noise.applyADSR(0.001f, 0.003f, 0.1f, 0.003f);
                buffer.mixFrom(noise, 0, 1.0f);
                buffer.clip();
                break;
            }
            case Beep::notify: {
                SoundBuffer b1, b2;
                b1.generateSineWave(880.0f, 0.1f, volume);
                b1.applyADSR(0.005f, 0.03f, 0.5f, 0.05f);
                b2.generateSineWave(660.0f, 0.12f, volume);
                b2.applyADSR(0.005f, 0.03f, 0.5f, 0.07f);
                buffer = b1;
                buffer.mixFrom(b2, static_cast<size_t>(0.12f * 44100), 1.0f);
                buffer.clip();
                break;
            }
            case Beep::sweep: {
                float duration = 0.12f;
                int sr = 44100;
                int numSamples = static_cast<int>(duration * sr);
                buffer.samples.resize(numSamples);
                buffer.channels = 1;
                buffer.sampleRate = sr;
                buffer.numSamples = numSamples;
                for (int i = 0; i < numSamples; i++) {
                    float t = static_cast<float>(i) / sr;
                    float progress = static_cast<float>(i) / numSamples;
                    float freq = 300.0f * std::pow(6.0f, progress);
                    float env = std::sin(progress * HALF_TAU);
                    env = env * env;
                    float noise = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.15f;
                    float sample = (std::sin(TAU * freq * t) + noise * env) * env * volume * 0.4f;
                    buffer.samples[i] = sample;
                }
                break;
            }
        }
        sound->loadFromBuffer(buffer);
        return sound;
    }

    std::shared_ptr<Sound> generateFrequency(float freq) {
        auto sound = std::make_shared<Sound>();
        SoundBuffer buffer;
        buffer.generateSineWave(freq, 0.1f, volume);
        buffer.applyADSR(0.005f, 0.02f, 0.4f, 0.05f);
        sound->loadFromBuffer(buffer);
        return sound;
    }

    void playPreset(Beep type) {
        uint64_t currentFrame = internal::updateFrameCount;
        if (currentFrame == lastBeepFrame && currentFrame > 0) return;
        lastBeepFrame = currentFrame;

        int key = presetToKey(type);
        auto it = cache.find(key);
        if (it == cache.end()) {
            if (cache.size() >= MAX_CACHE_SIZE) cache.clear();
            auto sound = generatePreset(type);
            cache[key] = sound;
            sound->play();
        } else {
            it->second->play();
        }
    }

    void playFrequency(float freq) {
        uint64_t currentFrame = internal::updateFrameCount;
        if (currentFrame == lastBeepFrame && currentFrame > 0) return;
        lastBeepFrame = currentFrame;

        int key = static_cast<int>(freq);
        auto it = cache.find(key);
        if (it == cache.end()) {
            if (cache.size() >= MAX_CACHE_SIZE) cache.clear();
            auto sound = generateFrequency(freq);
            cache[key] = sound;
            sound->play();
        } else {
            it->second->play();
        }
    }

    void setVolume(float vol) {
        volume = (vol < 0.0f) ? 0.0f : (vol > 1.0f) ? 1.0f : vol;
        cache.clear();
    }
};

inline BeepManager& getManager() {
    static BeepManager manager;
    return manager;
}

} // namespace beep_internal

// Play default beep (ping)
inline void beep() {
    beep_internal::getManager().playPreset(Beep::ping);
}

// Play preset sound
inline void beep(Beep type) {
    beep_internal::getManager().playPreset(type);
}

// Play custom frequency
inline void beep(float frequency) {
    beep_internal::getManager().playFrequency(frequency);
}

// Play custom frequency (int overload)
inline void beep(int frequency) {
    beep_internal::getManager().playFrequency(static_cast<float>(frequency));
}

// Set beep volume (0.0-1.0)
inline void setBeepVolume(float vol) {
    beep_internal::getManager().setVolume(vol);
}

// Get current beep volume
inline float getBeepVolume() {
    return beep_internal::getManager().volume;
}

} // namespace trussc
