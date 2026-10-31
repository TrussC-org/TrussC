#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace trussc::internal {

struct IesParseResult {
    std::vector<float> vertAngles;
    std::vector<float> horizAngles;
    std::vector<std::vector<float>> candela;
    float maxCandela = 0.0f;
};

// Pure LM-63 parser: the caller handles logging and GPU resources.
// The result is replaced only on success; error is empty on success.
inline bool parseIes(const std::string& data, IesParseResult& result,
                     std::string& error) {
    error.clear();
    try {
    std::istringstream iss(data);
    std::string line;
    bool foundTilt = false;
    bool includeTilt = false;
    while (std::getline(iss, line)) {
        const auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.rfind("TILT=", 0) == 0) {
            foundTilt = true;
            includeTilt = line.find("INCLUDE") != std::string::npos;
            break;
        }
    }
    if (!foundTilt) {
        error = "TILT= line not found";
        return false;
    }

    // Collect only required values, including the optional TILT block.
    // Grow storage only after extracting an actual finite value, never from a count.
    std::vector<float> tokens;
    const auto readTokens = [&](std::size_t count) {
        for (std::size_t n = 0; n < count; ++n) {
            float value;
            if (!(iss >> value)) { error = "invalid numeric IES data"; return false; }
            if (!std::isfinite(value)) { error = "non-finite IES value"; return false; }
            tokens.push_back(value);
        }
        return true;
    };

    const auto validCount = [](float count, int minimum, std::size_t remaining) {
        // Compare without rounding INT_MAX up to the next float before casting.
        // Bound (abi): file counts must fit int indices and available input bytes; actual values are checked before output allocation. Overflow rejects the IES data.
        return std::isfinite(count) && std::trunc(count) == count && count >= minimum &&
               static_cast<double>(count) <= std::numeric_limits<int>::max() &&
               static_cast<long double>(count) <= remaining;
    };

    std::size_t idx = 0;
    if (includeTilt) {
        // <orientation> <numPairs> <angles...> <factors...>
        if (!readTokens(2)) return false;
        if (!validCount(tokens[1], 0, data.size() - 2)) {
            error = "invalid TILT pair count";
            return false;
        }
        const auto numPairs = static_cast<std::size_t>(static_cast<int>(tokens[1]));
        // Bound (abi): the TILT pair product must fit available input bytes before extraction. Overflow rejects the IES data.
        if (numPairs > (data.size() - 2) / 2) {
            error = "not enough TILT data";
            return false;
        }
        if (!readTokens(numPairs * 2)) return false;
        idx = 2 + numPairs * 2;
    }

    constexpr std::size_t headerSize = 13;
    if (!readTokens(headerSize)) return false;
    const float candelaMultiplier = tokens[idx + 2];
    const float vertCount = tokens[idx + 3];
    const float horizCount = tokens[idx + 4];
    idx += headerSize;
    if (!validCount(vertCount, 1, data.size()) || !validCount(horizCount, 1, data.size())) {
        error = "invalid angle count";
        return false;
    }
    const auto numVert = static_cast<std::size_t>(static_cast<int>(vertCount));
    const auto numHoriz = static_cast<std::size_t>(static_cast<int>(horizCount));

    // Both counts fit int, so their sum fits size_t. Guard the product too,
    // including on platforms with 32-bit size_t.
    // Bound (abi): file candela-table dimensions must fit the host index width before multiplication. Overflow rejects the IES data.
    if (numHoriz > (std::numeric_limits<std::size_t>::max() - numVert - numHoriz) / numVert) {
        error = "not enough angle or candela data";
        return false;
    }
    const std::size_t needed = numVert + numHoriz + numVert * numHoriz;
    if (!readTokens(needed)) return false;
    // Trailing means every byte after the last required value: ignore the
    // remainder without parsing, including a lone DOS EOF (0x1A), 0x1A followed
    // by bytes, text, and extra numeric values (as the old numeric-prefix parser did).
    const std::size_t remaining = tokens.size() - idx;
    // Bound (security): untrusted IES table dimensions must fit actual file tokens to bound output allocation by input. Overflow rejects the IES data.
    if (needed > remaining) {
        error = "not enough angle or candela data";
        return false;
    }

    IesParseResult parsed;
    parsed.vertAngles.resize(numVert);
    for (auto& angle : parsed.vertAngles) angle = tokens[idx++];
    parsed.horizAngles.resize(numHoriz);
    for (auto& angle : parsed.horizAngles) angle = tokens[idx++];
    parsed.candela.resize(numHoriz);
    for (auto& row : parsed.candela) {
        row.resize(numVert);
        for (auto& cd : row) {
            cd = tokens[idx++] * candelaMultiplier;
            if (!std::isfinite(cd)) { error = "candela value out of range"; return false; }
            if (cd > parsed.maxCandela) parsed.maxCandela = cd;
        }
    }
    result = std::move(parsed);
    return true;
    } catch (const std::exception&) {
        error = "out of memory";
        return false;
    }
}

} // namespace trussc::internal
