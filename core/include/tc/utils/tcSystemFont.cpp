// =============================================================================
// System font lookup — platform-independent part: find a face in font data
// by its PostScript name. Used where the OS gives a font's PostScript name
// but not its face index (CoreText on macOS / iOS).
// =============================================================================

#include "tc/utils/tcSystemFont.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

namespace trussc {
namespace internal {

namespace {

uint64_t readU16(const uint8_t* d, uint64_t o) {
    return ((uint64_t)d[o] << 8) | d[o + 1];
}

uint64_t readU32(const uint8_t* d, uint64_t o) {
    return (readU16(d, o) << 16) | readU16(d, o + 2);
}

// Does the name table at [tableStart, tableStart + tableLength) hold a
// PostScript name (name ID 6) equal to `wanted`? Every offset is checked
// against the table (in 64 bits). Mac Roman records are compared as bytes,
// Unicode and Windows records as UTF-16BE code units.
bool nameTableHasPostScriptName(const uint8_t* d, uint64_t tableStart, uint64_t tableLength,
                                const std::string& wanted) {
    if (tableLength < 6) return false;
    const uint64_t count = readU16(d, tableStart + 2);
    const uint64_t stringOffset = readU16(d, tableStart + 4);
    if (6 + 12 * count > tableLength) return false;

    for (uint64_t i = 0; i < count; i++) {
        const uint64_t rec = tableStart + 6 + 12 * i;
        const uint64_t platformID = readU16(d, rec);
        const uint64_t nameID = readU16(d, rec + 6);
        const uint64_t length = readU16(d, rec + 8);
        const uint64_t offset = readU16(d, rec + 10);
        if (nameID != 6) continue;
        if (stringOffset + offset + length > tableLength) continue;
        const uint8_t* str = d + tableStart + stringOffset + offset;

        if (platformID == 1) {
            if (length == wanted.size() &&
                std::equal(wanted.begin(), wanted.end(), str,
                           [](char a, uint8_t b) { return (uint8_t)a == b; })) {
                return true;
            }
        } else if (platformID == 0 || platformID == 3) {
            if (length != 2 * (uint64_t)wanted.size()) continue;
            bool same = true;
            for (size_t k = 0; k < wanted.size() && same; k++) {
                same = readU16(str, 2 * k) == (uint8_t)wanted[k];
            }
            if (same) return true;
        }
    }
    return false;
}

// Does the face whose table directory starts at `fontStart` have that
// PostScript name?
bool faceHasPostScriptName(const uint8_t* d, uint64_t size, uint64_t fontStart,
                           const std::string& wanted) {
    if (fontStart + 12 > size) return false;
    const uint64_t numTables = readU16(d, fontStart + 4);
    if (fontStart + 12 + 16 * numTables > size) return false;
    for (uint64_t i = 0; i < numTables; i++) {
        const uint64_t rec = fontStart + 12 + 16 * i;
        if (readU32(d, rec) != 0x6E616D65u) continue;  // 'name'
        const uint64_t offset = readU32(d, rec + 8);
        const uint64_t length = readU32(d, rec + 12);
        if (offset + length > size) return false;
        return nameTableHasPostScriptName(d, offset, length, wanted);
    }
    return false;
}

} // namespace

int findFaceByPostScriptName(const uint8_t* data, size_t size,
                             const std::string& postScriptName) {
    if (!data || size < 12 || postScriptName.empty()) return -1;
    const uint64_t n = size;

    if (readU32(data, 0) != 0x74746366u) {  // 'ttcf'
        return faceHasPostScriptName(data, n, 0, postScriptName) ? 0 : -1;
    }
    const uint64_t numFonts = readU32(data, 8);
    if (12 + 4 * numFonts > n) return -1;
    for (uint64_t i = 0; i < numFonts && i <= (uint64_t)INT32_MAX; i++) {
        const uint64_t fontStart = readU32(data, 12 + 4 * i);
        if (faceHasPostScriptName(data, n, fontStart, postScriptName)) return (int)i;
    }
    return -1;
}

int findFaceInFileByPostScriptName(const fs::path& path, const std::string& postScriptName) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return -1;
    const std::streamoff end = file.tellg();
    if (end < 12) return -1;
    const uint64_t fileSize = static_cast<uint64_t>(end);
    const auto inFile = [fileSize](uint64_t offset, uint64_t length) {
        return offset <= fileSize && length <= fileSize - offset;
    };
    const auto readAt = [&](uint64_t offset, uint8_t* data, uint64_t length) {
        // Check in 64 bits before seeking or narrowing to stream types.
        if (!inFile(offset, length) ||
            offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            length > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            return false;
        }
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(data),
                                           static_cast<std::streamsize>(length)));
    };

    // A single font has only face 0. For a collection, read only its header,
    // face directories and name tables, never the glyph/outline data.
    uint8_t header[12] = {};
    if (!readAt(0, header, sizeof(header))) return -1;
    if (readU32(header, 0) != 0x74746366u) return 0;  // 'ttcf'
    if (postScriptName.empty()) return -1;

    const uint64_t numFonts = readU32(header, 8);
    if (!inFile(12, 4 * numFonts)) return -1;
    for (uint64_t i = 0; i < numFonts && i <= static_cast<uint64_t>(INT32_MAX); i++) {
        uint8_t faceOffset[4] = {};
        if (!readAt(12 + 4 * i, faceOffset, sizeof(faceOffset))) return -1;
        const uint64_t fontStart = readU32(faceOffset, 0);
        if (!readAt(fontStart, header, sizeof(header))) return -1;
        const uint64_t directoryStart = fontStart + 12;
        const uint64_t directoryLength = 16 * readU16(header, 4);
        if (!inFile(directoryStart, directoryLength)) return -1;
        if (directoryLength == 0) continue;
        std::vector<uint8_t> directory(static_cast<size_t>(directoryLength));
        if (!readAt(directoryStart, directory.data(), directoryLength)) return -1;
        for (uint64_t rec = 0; rec < directoryLength; rec += 16) {
            if (readU32(directory.data(), rec) != 0x6E616D65u) continue;  // 'name'
            const uint64_t offset = readU32(directory.data(), rec + 8);
            const uint64_t length = readU32(directory.data(), rec + 12);
            if (!inFile(offset, length) || length > std::numeric_limits<size_t>::max() ||
                length > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
                return -1;
            }
            if (length < 6) break;
            std::vector<uint8_t> names(static_cast<size_t>(length));
            if (!readAt(offset, names.data(), length)) return -1;
            if (nameTableHasPostScriptName(names.data(), 0, length, postScriptName)) {
                return static_cast<int>(i);
            }
            break;
        }
    }
    return -1;
}

} // namespace internal
} // namespace trussc
