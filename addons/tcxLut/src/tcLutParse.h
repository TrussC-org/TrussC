#pragma once
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
    std::string title;
    std::vector<float> rgb;
};
inline bool parseCube(std::istream& input, CubeData& result, std::string& error) {
    try {
        CubeData parsed;
        std::string line;
        size_t expected = 0;
        bool sawData = false;
        while (std::getline(input, line)) {
            std::istringstream fields(line);
            std::string keyword;
            if (!(fields >> keyword) || keyword[0] == '#') continue;
            if (keyword == "TITLE") {
                std::string title;
                std::getline(fields, title);
                auto first = title.find_first_not_of(" \t\r\"");
                auto last = title.find_last_not_of(" \t\r\"");
                if (first != std::string::npos) parsed.title = title.substr(first, last - first + 1);
            } else if (keyword == "LUT_3D_SIZE") {
                std::string extra;
                if (parsed.side || sawData || !(fields >> parsed.side) ||
                    ((fields >> extra) && extra[0] != '#') ||
                    !cubeElements(parsed.side, 3, expected) || expected > parsed.rgb.max_size()) {
                    error = "invalid LUT dimensions"; return false;
                }
                // No reserve from the declaration: storage grows only with actual complete rows.
            } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
                float a, b, c;
                if (!(fields >> a >> b >> c) || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) {
                    error = "invalid LUT domain"; return false;
                }
            } else {
                if (keyword == "LUT_1D_SIZE") { error = "1D LUT not supported"; return false; }
                float r, g, b;
                std::istringstream row(line);
                std::string extra;
                if (!expected || parsed.rgb.size() > expected - 3 || !(row >> r >> g >> b) ||
                    !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
                    ((row >> extra) && extra[0] != '#')) {
                    error = "invalid LUT row"; return false;
                }
                sawData = true;
                parsed.rgb.insert(parsed.rgb.end(), {r, g, b});
            }
        }
        if (input.bad() || !expected || parsed.rgb.size() != expected) {
            error = "incomplete LUT table"; return false;
        }
        result = std::move(parsed);
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "out of memory"; return false;
    }
}
}
