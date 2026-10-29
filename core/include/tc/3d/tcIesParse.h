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

    // Tokenizing the optional TILT block too bounds its skip by actual data.
    // Extraction stops immediately on a failed stream.
    std::vector<float> tokens;
    float value;
    for (;;) {
        // Establish clean EOF before attempting a token. A token extraction
        // failure is malformed even when it also consumes the end of input.
        iss >> std::ws;
        if (iss.eof()) break;
        if (!(iss >> value)) { error = "invalid numeric IES data"; return false; }
        if (!std::isfinite(value)) { error = "non-finite IES value"; return false; }
        tokens.push_back(value);
    }

    const auto validCount = [](float count, int minimum, std::size_t remaining) {
        // Compare without rounding INT_MAX up to the next float before casting.
        // Bound (abi): file angle counts must fit int indices and the available token count. Overflow rejects the IES data.
        return std::isfinite(count) && std::trunc(count) == count && count >= minimum &&
               static_cast<double>(count) <= std::numeric_limits<int>::max() &&
               static_cast<long double>(count) <= remaining;
    };

    std::size_t idx = 0;
    if (includeTilt) {
        // <orientation> <numPairs> <angles...> <factors...>
        if (tokens.size() < 2 || !validCount(tokens[1], 0, tokens.size() - 2)) {
            error = "invalid TILT pair count";
            return false;
        }
        const auto numPairs = static_cast<std::size_t>(static_cast<int>(tokens[1]));
        if (numPairs > (tokens.size() - 2) / 2) {
            error = "not enough TILT data";
            return false;
        }
        idx = 2 + numPairs * 2;
    }

    constexpr std::size_t headerSize = 13;
    if (tokens.size() - idx < headerSize) {
        error = "insufficient numeric data";
        return false;
    }
    const float candelaMultiplier = tokens[idx + 2];
    const float vertCount = tokens[idx + 3];
    const float horizCount = tokens[idx + 4];
    idx += headerSize;
    const std::size_t remaining = tokens.size() - idx;
    if (!validCount(vertCount, 1, remaining) || !validCount(horizCount, 1, remaining)) {
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
