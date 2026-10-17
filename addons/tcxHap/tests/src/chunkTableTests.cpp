#include "tcxHapDecoder.h"
#include "impl/bcdec.h"
#include <snappy-c.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <type_traits>

static_assert(std::is_copy_constructible_v<tcx::hap::HapDecoder>);
static_assert(std::is_move_constructible_v<tcx::hap::HapDecoder>);
static_assert(std::is_copy_assignable_v<tcx::hap::HapDecoder>);
static_assert(std::is_move_assignable_v<tcx::hap::HapDecoder>);

#ifdef TC_HAP_TRACE_DECODE
static unsigned int decodeCalls = 0;
extern "C" unsigned int __real_HapDecode(const void*, unsigned long, unsigned int,
    HapDecodeCallback, void*, void*, unsigned long, unsigned long*, unsigned int*);
extern "C" unsigned int __wrap_HapDecode(const void* input, unsigned long inputSize,
    unsigned int index, HapDecodeCallback callback, void* info, void* output,
    unsigned long outputSize, unsigned long* used, unsigned int* format) {
    ++decodeCalls;
    return __real_HapDecode(input, inputSize, index, callback, info, output, outputSize, used, format);
}
#endif

namespace {
using Bytes = std::vector<uint8_t>;
int failures = 0;
int checks = 0;
std::string selected;
std::string entry;
bool listCases = false;

bool selectCase(const char* kind, const std::string& name) {
    if (listCases) {
        std::printf("%s\t%s\n", kind, name.c_str());
        return false;
    }
    return selected.empty() || selected == name;
}

void check(const std::string& name, bool ok) {
    ++checks;
    std::printf("HAP tables: %-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}

void put32(Bytes& bytes, size_t pos, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[pos + i] = uint8_t(value >> (8 * i));
}

Bytes section(uint8_t type, const Bytes& payload, bool extended = false) {
    Bytes bytes(extended ? 8 : 4);
    bytes[3] = type;
    if (extended) put32(bytes, 4, uint32_t(payload.size()));
    else for (int i = 0; i < 3; ++i) bytes[i] = uint8_t(payload.size() >> (8 * i));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }

Bytes complexFrame(const Bytes& texture, uint8_t format = 0xb, bool offsets = true) {
    Bytes tables = section(2, {0xa});
    Bytes size(4);
    put32(size, 0, uint32_t(texture.size()));
    append(tables, section(3, size));
    if (offsets) append(tables, section(4, Bytes(4)));
    Bytes payload = section(1, tables);
    append(payload, texture);
    return section(0xc0 | format, payload);
}

Bytes multiple(const Bytes& first, const Bytes& second) {
    Bytes payload = first;
    append(payload, second);
    return section(0xd, payload);
}

void rejected(const std::string& name, const Bytes& bytes) {
    if (!selectCase("malformed", name)) return;
#ifdef TC_HAP_TRACE_DECODE
    const auto callsBefore = decodeCalls;
#endif
    tcx::hap::HapDecoder decoder;
    tcx::hap::HapDecodedFrame frame;
    frame.data.assign(16, 0xcd);
    frame.format = tcx::hap::HapFormat::DXT1;
    if (entry.empty() || entry == "allocating") check(name + " allocating", !decoder.decode(bytes.data(), bytes.size(), 4, 4, frame) && !frame.isValid());
    Bytes output(64, 0xcd);
    tcx::hap::HapFormat format = tcx::hap::HapFormat::Unknown;
    if (entry.empty() || entry == "preallocated") check(name + " preallocated", !decoder.decodeToBuffer(bytes.data(), bytes.size(), 4, 4,
          output.data(), output.size(), format) &&
          format == tcx::hap::HapFormat::Unknown &&
          std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0xcd; }));
#ifdef TC_HAP_TRACE_DECODE
    check(name + " before HapDecode", decodeCalls == callsBefore);
#endif
}

void accepted(const std::string& name, const Bytes& bytes, const Bytes& expected, uint32_t height = 4) {
    if (!selectCase("valid", name)) return;
    tcx::hap::HapDecoder decoder;
    tcx::hap::HapDecodedFrame frame;
    check(name + " allocating bytes", decoder.decode(bytes.data(), bytes.size(), 4, height, frame) && frame.data == expected);
    Bytes output(expected.size());
    tcx::hap::HapFormat format = tcx::hap::HapFormat::Unknown;
    check(name + " preallocated bytes", decoder.decodeToBuffer(bytes.data(), bytes.size(), 4, height,
          output.data(), output.size(), format) && output == expected);
    // Exact BC bytes preserve pixels; also compare BC1 RGBA output explicitly.
    if (format == tcx::hap::HapFormat::DXT1 && output == expected) {
        std::array<uint32_t, 16> actualPixels{}, expectedPixels{};
        bool equal = true;
        for (size_t offset = 0; offset < output.size(); offset += 8) {
            bcdec_bc1(output.data() + offset, actualPixels.data(), 16);
            bcdec_bc1(expected.data() + offset, expectedPixels.data(), 16);
            equal = equal && actualPixels == expectedPixels;
        }
        check(name + " pixels", equal);
    }
}
Bytes chunkFrame(const Bytes& compressors, const std::vector<uint32_t>& sizes,
                 const Bytes& data, const std::vector<uint32_t>& offsets = {}) {
    Bytes tables = section(2, compressors), sizeBytes(sizes.size() * 4);
    for (size_t i = 0; i < sizes.size(); ++i) put32(sizeBytes, i * 4, sizes[i]);
    append(tables, section(3, sizeBytes));
    if (!offsets.empty()) {
        Bytes offsetBytes(offsets.size() * 4);
        for (size_t i = 0; i < offsets.size(); ++i) put32(offsetBytes, i * 4, offsets[i]);
        append(tables, section(4, offsetBytes));
    }
    Bytes payload = section(1, tables);
    append(payload, data);
    return section(0xcb, payload);
}

void diagnosticsTests(const Bytes& valid, const Bytes& invalid) {
#ifndef TC_HAP_BASELINE
    if (listCases || !selected.empty()) return;
    int warnings = 0;
    auto logger = trussc::getLogger().onLog.listen([&](trussc::LogEventArgs& log) {
        if (log.level == trussc::LogLevel::Warning && log.message.find("[HapDecoder]") == 0) ++warnings;
    });
    tcx::hap::HapDecoder decoder;
    tcx::hap::HapDecodedFrame result;
    Bytes output(8, 0xcd);
    tcx::hap::HapFormat format;
    for (int i = 0; i < 50; ++i) {
        decoder.decode(invalid.data(), invalid.size(), 4, 4, result);
        decoder.decodeToBuffer(invalid.data(), invalid.size(), 4, 4, output.data(), output.size(), format);
    }
    check("invalid frame diagnostics", warnings == 1 && decoder.getInvalidFrameCount() == 100 &&
          decoder.getLastChunkCount() == -1);
    check("valid frame after invalid frames", decoder.decode(valid.data(), valid.size(), 4, 4, result));
    decoder.decode(invalid.data(), invalid.size(), 4, 4, result);
    check("warning remains suppressed after recovery", warnings == 1 && decoder.getInvalidFrameCount() == 101);
    tcx::hap::HapDecoder other;
    other.decode(invalid.data(), invalid.size(), 4, 4, result);
    check("independent decoder diagnostics", warnings == 2 && other.getInvalidFrameCount() == 1);
    auto copied = other;
    auto moved = std::move(copied);
    moved.decode(invalid.data(), invalid.size(), 4, 4, result);
    check("copied/moved diagnostics preserve warning state", warnings == 2 &&
          moved.getInvalidFrameCount() == 2 && other.getInvalidFrameCount() == 1);
    check("zero dimensions", !decoder.decode(valid.data(), valid.size(), 0, 4, result) && !result.isValid());
    check("large dimensions", !decoder.decode(valid.data(), valid.size(), UINT32_MAX, UINT32_MAX, result) && !result.isValid());
    check("texture size arithmetic", tcx::hap::calculateTextureSize(UINT32_MAX, UINT32_MAX,
          tcx::hap::HapFormat::BC7) == 0);
    tcx::hap::detail::HapSection sectionView;
    const Bytes highLength = {0, 0, 0, 0xab, 0, 0, 0, 0x80};
    check("bounded high-bit section length", !tcx::hap::detail::readHapSection(
          highLength.data(), size_t(0x80000008ULL), sectionView));
    const Bytes maxLength = {0, 0, 0, 0xab, 0xff, 0xff, 0xff, 0x7f};
    check("largest ABI section length header", tcx::hap::detail::readHapSection(
          maxLength.data(), size_t(0x80000007ULL), sectionView) && sectionView.size == 0x7fffffff);
    // Only the eight-byte header is accessed; no large allocation is needed.
    check("high-bit frame rejected before output allocation", !decoder.decode(
          highLength.data(), size_t(0x80000008ULL), 131072, 32768, result) && !result.isValid());
    output.assign(8, 0xcd);
    check("undersized destination", !decoder.decodeToBuffer(valid.data(), valid.size(), 4, 4,
          output.data(), 7, format) && std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0xcd; }));
    check("destination failure is not an invalid frame", decoder.getInvalidFrameCount() == 104 && warnings == 2);
#endif
}
} // namespace

int runChunkTableTests() {
    const Bytes block = {0x00, 0xf8, 0, 0, 0, 0, 0, 0};
    const auto valid = complexFrame(block);
    auto bytes = valid;
    put32(bytes, 25, 1024);
    rejected("chunk offset outside texture", bytes);
    diagnosticsTests(valid, bytes);
    if (!listCases && selected.empty()) {
        check("format discovery ignores invalid table", tcx::hap::getHapFrameFormat(bytes.data(), bytes.size()) == tcx::hap::HapFormat::DXT1);
    }
    accepted("complex BC1", valid, block);
    for (bool empty : {false, true}) {
        Bytes tables = section(2, {0xa});
        append(tables, section(5, empty ? Bytes{} : Bytes{42}, true));
        append(tables, section(3, {8, 0, 0, 0}));
        Bytes payload = section(1, tables);
        append(payload, block);
        accepted(empty ? "empty unknown instruction section" : "unknown instruction section", section(0xcb, payload), block);
    }
#ifndef TC_HAP_BASELINE
    if (!listCases && selected.empty()) {
        const size_t maximum = tcx::hap::detail::kMaxHapChunks;
        const auto boundary = chunkFrame(Bytes(maximum, 0xa), std::vector<uint32_t>(maximum, 8), Bytes(maximum * 8));
        tcx::hap::HapFormat format;
        check("maximum chunk count validates", tcx::hap::detail::validateHapFrame(
              boundary.data(), boundary.size(), 4, uint32_t(maximum * 4), format));
        const auto over = chunkFrame(Bytes(maximum + 1, 0xa), std::vector<uint32_t>(maximum + 1, 8), Bytes((maximum + 1) * 8));
        check("above maximum chunks rejected with matching dimensions", !tcx::hap::detail::validateHapFrame(
              over.data(), over.size(), 4, uint32_t((maximum + 1) * 4), format));
        check("32-bit chunk allocation products", maximum * uint64_t(24) <= UINT32_MAX &&
              maximum * uint64_t(40) <= UINT32_MAX && maximum * uint64_t(sizeof(std::thread)) <= UINT32_MAX &&
              (maximum - 1) * uint64_t(4) <= INT32_MAX);
    }
#endif
    rejected("above maximum chunk count", chunkFrame(Bytes(1025, 0xa), std::vector<uint32_t>(1025, 8), Bytes(1025 * 8)));
    rejected("ABI section length high bit", {0, 0, 0, 0xab, 0, 0, 0, 0x80});
    accepted("implicit chunk offsets", complexFrame(block, 0xb, false), block);
    accepted("uncompressed BC1", section(0xab, block), block);
    accepted("extended texture header", section(0xab, block, true), block);
    accepted("two implicit chunks", chunkFrame({0xa, 0xa}, {4, 4}, block), block);
    Bytes reordered(block.begin() + 4, block.end());
    reordered.insert(reordered.end(), block.begin(), block.begin() + 4);
    accepted("explicit reordered chunks", chunkFrame({0xa, 0xa}, {4, 4}, reordered, {4, 0}), block);
    Bytes compressed(snappy_max_compressed_length(block.size()));
    size_t compressedSize = compressed.size();
    snappy_compress(reinterpret_cast<const char*>(block.data()), block.size(),
                    reinterpret_cast<char*>(compressed.data()), &compressedSize);
    compressed.resize(compressedSize);
    accepted("Snappy texture", section(0xbb, compressed), block);
    accepted("Snappy chunk", chunkFrame({0xb}, {uint32_t(compressed.size())}, compressed), block);
    Bytes mixed = {0x04, 0x0c, 0x00, 0xf8, 0, 0}; // Snappy literal: first four BC1 bytes.
    mixed.insert(mixed.end(), block.begin() + 4, block.end());
    accepted("mixed compressors", chunkFrame({0xb, 0xa}, {6, 4}, mixed), block);
    Bytes repeated = block;
    append(repeated, block);
    accepted("explicit overlapping chunks", chunkFrame({0xa, 0xa}, {8, 8}, block, {0, 0}), repeated, 8);
    // The same ranges overfill a 4x4 output; input overlap itself is valid.
    rejected("explicit repeated chunks exceed output", chunkFrame({0xa, 0xa}, {8, 8}, block, {0, 0}));
    rejected("later explicit chunk range", chunkFrame({0xa, 0xa}, {4, 4}, block, {0, 7}));
    rejected("missing size table", section(0xcb, section(1, section(2, {0xa}))));
    rejected("missing compressor table", section(0xcb, section(1, section(3, {8, 0, 0, 0}))));
    rejected("compressor count mismatch", chunkFrame({0xa, 0xa}, {8}, block));
    rejected("size count mismatch", chunkFrame({0xa}, {4, 4}, block));
    rejected("offset count mismatch", chunkFrame({0xa}, {8}, block, {0, 0}));
    for (uint8_t type : {uint8_t(3), uint8_t(4)}) {
        Bytes tables = section(2, {0xa});
        append(tables, section(3, type == 3 ? Bytes{8, 0, 0, 0, 1} : Bytes{8, 0, 0, 0}));
        if (type == 4) append(tables, section(4, {0, 0, 0, 0, 1}));
        Bytes payload = section(1, tables);
        append(payload, block);
        rejected("partial table word type " + std::to_string(type), section(0xcb, payload));
    }
    Bytes shortStream = {8}; // Correct length prefix but no literal bytes.
    rejected("incomplete Snappy texture", section(0xbb, shortStream));
    rejected("incomplete Snappy chunk", chunkFrame({0xb}, {1}, shortStream));
    rejected("Snappy output size mismatch", section(0xbb, {9, 0, 0}));
    rejected("Snappy output size overflow", section(0xbb, {0xff, 0xff, 0xff, 0xff, 0x0f}));
    Bytes alpha(16, 0);
    accepted("Hap Alpha", complexFrame(alpha, 0xe), alpha);
    accepted("BC7 texture", complexFrame(alpha, 0xc), alpha);
    accepted("BC4 texture", complexFrame(block, 1), block);
    const auto color = complexFrame(alpha, 0xf);
    const auto mask = complexFrame(block, 1);
    accepted("Hap Q Alpha", multiple(color, mask), alpha);
    bytes = mask;
    put32(bytes, 25, 1024);
    rejected("invalid second texture chunk offset", multiple(color, bytes));
    rejected("invalid first texture chunk offset", multiple(bytes, color));
    bytes = valid;
    put32(bytes, 17, 9);
    rejected("invalid chunk size", bytes);
    put32(bytes, 17, 0xffffffffu);
    put32(bytes, 25, 0xfffffffcu);
    rejected("chunk range sum exceeds 32 bits", bytes);
    bytes = valid;
    put32(bytes, 17, 0);
    rejected("empty chunk", bytes);
    bytes = valid;
    bytes[12] = 0xb;
    rejected("invalid Snappy chunk", bytes);
    bytes[12] = 0;
    rejected("invalid chunk compressor", bytes);
    bytes = valid;
    bytes.pop_back();
    rejected("truncated texture section", bytes);
    for (size_t pos : {size_t(4), size_t(8), size_t(13), size_t(21)}) {
        bytes = valid;
        bytes[pos] = 0x7f;
        rejected("truncated section at " + std::to_string(pos), bytes);
    }
    for (uint8_t type : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(0xab), uint8_t(0xd)}) {
        Bytes empty = section(type, {}, true);
        rejected("empty section type " + std::to_string(type),
                 type == 0xab || type == 0xd ? empty : section(0xcb, empty));
    }
    for (uint8_t type : {uint8_t(2), uint8_t(3), uint8_t(4)}) {
        Bytes tables = section(2, {0xa});
        append(tables, section(3, {8, 0, 0, 0}));
        append(tables, section(type, {0}, true));
        Bytes payload = section(1, tables);
        append(payload, block);
        rejected("inconsistent table type " + std::to_string(type), section(0xcb, payload));
    }
    for (uint8_t type : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)}) {
        const Bytes oversized = {0, 0, 0, type, 0xfc, 0xff, 0xff, 0xff};
        rejected("extended section range type " + std::to_string(type),
                 section(0xcb, type == 1 ? oversized : section(1, oversized)));
    }
    for (uint8_t type : {uint8_t(2), uint8_t(3), uint8_t(4)}) {
        Bytes tables = section(2, {0xa});
        append(tables, section(3, {8, 0, 0, 0}));
        append(tables, section(4, {0, 0, 0, 0}));
        append(tables, section(type, type == 2 ? Bytes{0xa} :
                              type == 3 ? Bytes{8, 0, 0, 0} : Bytes(4)));
        Bytes payload = section(1, tables);
        append(payload, block);
        rejected("duplicate table type " + std::to_string(type), section(0xcb, payload));
    }
    Bytes tables = section(2, {0xa, 0xa});
    append(tables, section(3, {8, 0, 0, 0, 8, 0, 0, 0}));
    Bytes payload = section(1, tables);
    append(payload, block);
    rejected("cumulative implicit chunk size", section(0xcb, payload));
    append(payload, block);
    rejected("cumulative output size", section(0xcb, payload));
    rejected("short output", section(0xab, {1}));
    rejected("long output", section(0xab, Bytes(16)));
    rejected("extended section size sum", {0, 0, 0, 0xcb, 0xfc, 0xff, 0xff, 0xff});
    rejected("truncated second texture", multiple(color, {8, 0, 0, 0xa1}));
    rejected("nested multiple textures", multiple(color, multiple(color, mask)));
    Bytes three = color;
    append(three, mask); append(three, mask);
    rejected("third texture", section(0xd, three));
    rejected("short second texture output", multiple(color, section(0xa1, {1})));
    Bytes badMask = mask;
    put32(badMask, 17, 9);
    rejected("second texture chunk size", multiple(color, badMask));
    rejected("second texture Snappy stream", multiple(color, section(0xb1, {8})));
    accepted("extended multiple textures", section(0xd, [&] { Bytes p = color; append(p, mask); return p; }(), true), alpha);
    accepted("single texture container", section(0xd, valid), block);
    std::printf("HAP tables: %d checks, %d failures\n", checks, failures);
    return failures;
}

#ifdef TC_HAP_CHUNK_TABLE_TEST_MAIN
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--list") listCases = true;
        else if (std::string(argv[i]) == "--case" && i + 1 < argc) selected = argv[++i];
        else if (std::string(argv[i]) == "--entry" && i + 1 < argc) entry = argv[++i];
        else return 2;
    }
    return runChunkTableTests() ? 1 : 0;
}
#endif
