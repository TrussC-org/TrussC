#pragma once
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace tcx::lut::detail {
inline bool cubeElements(int side, size_t stride, size_t& elements) {
    if (side < 2 || stride == 0) return false;
    size_t count = size_t(side);
    // Bound (abi): cubic table and byte products must fit host storage before allocation. Overflow rejects the whole LUT and is counted.
    if (count > SIZE_MAX / count) return false;
    count *= size_t(side);
    if (count > SIZE_MAX / size_t(side)) return false;
    count *= size_t(side);
    if (count > SIZE_MAX / stride) return false;
    elements = count * stride;
    return true;
}
struct CubeData {
    int side = 0;
    int curveSize = 0;
    std::string title;
    std::vector<float> curve;
    std::vector<float> rgb;
};
inline bool parseCube(std::istream& input, CubeData& result, std::string& error) {
    CubeData parsed;
    std::string line;
    size_t expected = 0;
    size_t expectedCurve = 0;
    bool sawData = false;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string keyword;
        if (!(fields >> keyword) || keyword[0] == '#') continue;
        if (keyword == "TITLE") {
            std::string title;
            fields >> std::ws;
            if (fields.peek() == '"') {
                // Read up to the closing quote; backslashes are literal (Adobe Cube 1.0, 5.6).
                fields.get();
                std::getline(fields, title, '"');
            } else {
                std::getline(fields, title, '#');
                const auto last = title.find_last_not_of(" \t\r");
                title = last == std::string::npos ? "" : title.substr(0, last + 1);
            }
            parsed.title = std::move(title);
        } else if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE") {
            std::string extra;
            const bool curve = keyword == "LUT_1D_SIZE";
            int& size = curve ? parsed.curveSize : parsed.side;
            if (size || sawData || !(fields >> size) || size < 2 ||
                ((fields >> extra) && extra[0] != '#')) {
                error = "invalid LUT dimensions"; return false;
            }
            if (curve) {
                // Bound (storage): RGB float entries and their bytes must fit host storage.
                if (size_t(size) > SIZE_MAX / (3 * sizeof(float)) ||
                    size_t(size) > parsed.curve.max_size() / 3) {
                    error = "invalid LUT dimensions"; return false;
                }
                expectedCurve = size_t(size) * 3;
            } else if (!cubeElements(size, 3, expected) || expected > parsed.rgb.max_size()) {
                error = "invalid LUT dimensions"; return false;
            }
            // No reserve from the declaration: storage grows only with actual complete rows.
        } else if (keyword == "LUT_1D_INPUT_RANGE" || keyword == "LUT_3D_INPUT_RANGE" ||
                   keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            // Bound (format): input range requires two finite values; per-channel domain requires three.
            const bool inputRange = keyword == "LUT_1D_INPUT_RANGE" || keyword == "LUT_3D_INPUT_RANGE";
            const int components = inputRange ? 2 : 3;
            bool defaultRange = true;
            for (int component = 0; component < components; ++component) {
                double value;
                if (!(fields >> value) || !std::isfinite(value)) {
                    error = "invalid LUT range (" + keyword + ")"; return false;
                }
                const auto next = fields.peek();
                if (next != std::char_traits<char>::eof() && next != '#' &&
                    !std::isspace(static_cast<unsigned char>(next))) {
                    error = "invalid LUT range (" + keyword + ")"; return false;
                }
                const double expectedValue = keyword == "DOMAIN_MAX" ||
                    (inputRange && component == 1) ? 1.0 : 0.0;
                // Bound (implementation): sampling supports only the default 0..1 domain.
                // Compare parsed doubles exactly; no epsilon admits semantics we cannot apply.
                defaultRange = defaultRange && value == expectedValue;
            }
            const auto trailing = (fields >> std::ws).peek();
            if (trailing != std::char_traits<char>::eof() && trailing != '#') {
                error = "invalid LUT range (" + keyword + ")"; return false;
            }
            if (!defaultRange) {
                error = "unsupported LUT range (" + keyword + "): only 0..1 is supported"; return false;
            }
        } else if (keyword == "LUT_IN_VIDEO_RANGE" || keyword == "LUT_OUT_VIDEO_RANGE") {
            // Resolve uses bare flags for legal range; full range omits the flags.
            // Bound (implementation): video-range conversion is unsupported, so these flags must fail.
            const auto trailing = (fields >> std::ws).peek();
            if (trailing != std::char_traits<char>::eof() && trailing != '#') {
                error = "invalid LUT range (" + keyword + ")"; return false;
            }
            error = "unsupported LUT range (" + keyword + "): only full range is supported"; return false;
        } else {
            float r, g, b;
            std::istringstream row(line);
            std::string extra;
            const bool curve = parsed.curve.size() < expectedCurve;
            auto& table = curve ? parsed.curve : parsed.rgb;
            const size_t count = curve ? expectedCurve : expected;
            if (!count || table.size() > count - 3 || !(row >> r >> g >> b) ||
                !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
                ((row >> extra) && extra[0] != '#')) {
                error = "invalid LUT row"; return false;
            }
            sawData = true;
            table.insert(table.end(), {r, g, b});
        }
    }
    if (input.bad() || (!expected && !expectedCurve) || parsed.rgb.size() != expected ||
        parsed.curve.size() != expectedCurve) {
        error = "incomplete LUT table"; return false;
    }
    result = std::move(parsed);
    error.clear();
    return true;
}
}
