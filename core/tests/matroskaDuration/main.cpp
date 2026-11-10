#include "tc/video/internal/tcMatroskaDuration.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
using trussc::internal::MatroskaDuration;

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
Bytes join(Bytes left, const Bytes& right) {
    left.insert(left.end(), right.begin(), right.end());
    return left;
}
Bytes number(uint64_t value, size_t length = 0) {
    if (!length) {
        length = 1;
        while (length < 8 && (value >> (length * 8))) ++length;
    }
    Bytes result(length);
    for (size_t i = 0; i < length; ++i) {
        result[length - i - 1] = uint8_t(value);
        value >>= 8;
    }
    return result;
}
Bytes sizeVint(uint64_t size) {
    size_t length = 1;
    while (size >= (uint64_t(1) << (length * 7)) - 1) ++length;
    auto result = number(size, length);
    result[0] |= uint8_t(1u << (8 - length));
    return result;
}
Bytes element(uint64_t id, const Bytes& data) {
    return join(join(number(id), sizeVint(data.size())), data);
}
Bytes header(const std::string& type = "matroska") {
    return element(0x1A45DFA3, element(0x4282, Bytes(type.begin(), type.end())));
}
Bytes track(uint64_t type, uint64_t duration = 0, bool declared = true) {
    // DefaultDuration before TrackType is also legal.
    auto data = declared ? element(0x23E383, number(duration)) : Bytes{};
    return element(0xAE, join(data, element(0x83, number(type))));
}
Bytes file(const Bytes& tracks, bool unknownSegment = false, const std::string& type = "matroska") {
    auto segment = unknownSegment ? join(number(0x18538067), Bytes{0xFF})
                                  : join(number(0x18538067), sizeVint(tracks.size()));
    return join(header(type), join(segment, tracks));
}
MatroskaDuration parse(const Bytes& data) {
    return trussc::internal::readMatroskaDuration(data.size(), [&data](uint64_t offset, uint8_t* output, size_t count) {
        check(count <= 8, "unbounded read");
        check(offset <= data.size() && count <= data.size() - size_t(offset), "read past EOF");
        std::copy_n(data.data() + size_t(offset), count, output);
        return true;
    });
}
void expect(const Bytes& data, uint64_t duration, const char* message) {
    auto result = parse(data);
    check(result.isMatroska && result.nanoseconds == duration, message);
}
Bytes seekHead(uint64_t position, uint64_t target = 0x1654AE6B) {
    return element(0x114D9B74, element(0x4DBB,
        join(element(0x53AB, number(target)), element(0x53AC, number(position, 8)))));
}
void validFiles() {
    for (uint64_t duration : {41666667ULL, 33333333ULL, 16666667ULL, 33366667ULL, 46511628ULL}) {
        const auto tracks = element(0x1654AE6B, track(1, duration));
        expect(file(tracks), duration, "CFR / VFR declared duration");
        expect(file(tracks, true, "webm"), duration, "WebM / unknown-size Segment");
    }
    const auto tracks = element(0x1654AE6B, join(track(2, 20000000), join(track(1, 41666667), track(1, 16666667))));
    expect(file(tracks, false, std::string("matroska\0", 9)), 41666667, "NUL-padded Matroska DocType");
    expect(file(tracks, false, std::string("webm\0\0\0\0", 8)), 41666667, "NUL-padded WebM DocType");
    expect(file(tracks, false, std::string("matroska") + std::string(4096, '\0')),
           41666667, "long DocType padding uses bounded reads");
    check(!parse(file(tracks, false, "matroskax")).isMatroska, "DocType prefix is not a match");
    check(!parse(file(tracks, false, "webmx")).isMatroska, "WebM DocType prefix is not a match");
    expect(file(tracks), 41666667, "audio first, first video chosen");
    expect(file(element(0x1654AE6B, join(track(1, 0, false), track(1, 41666667)))), 0, "first video lacks duration");
    expect(file(element(0x1654AE6B, track(2, 41666667))), 0, "audio only");
    expect(file(element(0x1654AE6B, track(1, 0))), 0, "zero duration");
    expect(file(element(0x1654AE6B, track(1, 0, false))), 0, "absent duration");
    check(!parse(Bytes{0, 0, 0, 0}).isMatroska, "other containers");
    check(!parse(file(tracks, false, "other")).isMatroska, "other EBML DocType");

    const auto wideSegment = join(join(header(), number(0x18538067)),
        join(Bytes{0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, tracks));
    expect(wideSegment, 41666667, "eight-byte unknown Segment size");
    expect(file(element(0x1654AE6B, track(1, UINT64_MAX))), UINT64_MAX, "eight-byte duration");
    const auto wideSizeTracks = join(number(0x1654AE6B),
        join(number((uint64_t(1) << 56) | track(1, 41666667).size(), 8), track(1, 41666667)));
    expect(file(wideSizeTracks), 41666667, "eight-byte known size vint");

    const auto cluster = element(0x1F43B675, Bytes(256, 0));
    expect(file(join(cluster, tracks)), 41666667, "skip sized Cluster");
    const auto head = seekHead(seekHead(0).size() + cluster.size());
    const auto indexed = file(join(head, join(cluster, tracks)));
    expect(indexed, 41666667, "SeekHead locates Tracks");
    // The SeekHead can skip a Cluster with unknown size without reading it.
    const auto unknownCluster = join(number(0x1F43B675), Bytes{0xFF, 0, 0, 0});
    expect(file(join(seekHead(head.size() + unknownCluster.size()), join(unknownCluster, tracks))),
           41666667, "indexed Tracks after unknown-size Cluster");
    const auto segmentData = header().size() + number(0x18538067).size() +
        sizeVint(head.size() + cluster.size() + tracks.size()).size();
    const auto payloadStart = segmentData + head.size() + number(0x1F43B675).size() + sizeVint(256).size();
    const auto result = trussc::internal::readMatroskaDuration(indexed.size(), [&](uint64_t offset, uint8_t* output, size_t count) {
        check(offset + count <= payloadStart || offset >= payloadStart + 256, "read Cluster payload");
        std::copy_n(indexed.data() + size_t(offset), count, output);
        return true;
    });
    check(result.nanoseconds == 41666667, "bounded indexed read");
}
void malformedFiles() {
    const auto tracks = element(0x1654AE6B, track(1, 41666667));
    const auto videoFields = join(element(0x83, number(1)), element(0x23E383, number(41666667)));
    for (const auto& repeated : {element(0x23E383, number(16666667)),
                                 element(0x83, number(2)), element(0x83, number(1))}) {
        const auto invalidVideo = element(0xAE, join(videoFields, repeated));
        expect(file(element(0x1654AE6B, join(invalidVideo, track(1, 33333333)))), 0,
               "repeated track field invalidates first video declaration");
    }
    // Presence is per entry, and the first TrackType determines track selection.
    const auto repeatedAudioType = element(0xAE, join(element(0x83, number(2)), videoFields));
    expect(file(element(0x1654AE6B, join(repeatedAudioType, track(1, 33333333)))), 33333333,
           "repeated TrackType cannot change an audio entry into the first video");
    const auto complete = file(tracks);
    for (size_t length = 0; length < complete.size(); ++length) {
        const auto result = parse(Bytes(complete.begin(), complete.begin() + length));
        check(result.nanoseconds == 0, "truncated input accepted");
    }
    expect(file(join(number(0x1654AE6B), Bytes{0xFF})), 0, "unknown-size Tracks");
    expect(file(element(0x1654AE6B, join(number(0xAE), Bytes{0xFF}))), 0, "unknown-size TrackEntry");
    expect(file(element(0x1654AE6B, element(0xAE, join(number(0x23E383), Bytes{0xFF})))), 0, "unknown-size field");
    expect(file(join(join(number(0x1F43B675), Bytes{0xFF}), tracks)), 0, "unindexed unknown-size Cluster");
    expect(file(Bytes{0x08, 0, 0, 0, 0, 0x80}), 0, "ID vint longer than four bytes");
    expect(file(Bytes{0xEC, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 0, "size vint longer than eight bytes");
    expect(file(Bytes{0xEC, 0x88}), 0, "element past parent");
    // Bytes exist in the file but outside the TrackEntry parent.
    expect(file(element(0x1654AE6B, join(element(0xAE,
        join(element(0x83, Bytes{1}), Bytes{0x23, 0xE3, 0x83, 0x88})), number(41666667, 8)))),
        0, "duration bytes exist outside TrackEntry parent");
    expect(file(element(0x1654AE6B, element(0xAE,
        join(element(0x83, Bytes{1}), element(0x23E383, Bytes(9, 1)))))), 0, "oversized unsigned integer");
    expect(file(element(0x1654AE6B, element(0xAE,
        join(element(0x83, Bytes{1}), element(0x23E383, {}))))), 0, "empty unsigned integer");
    expect(file(join(seekHead(UINT64_MAX), tracks)), 0, "SeekPosition outside Segment");
    expect(file(join(seekHead(0), tracks)), 0, "SeekPosition points at wrong element");
    expect(file(join(element(0x114D9B74, join(number(0x4DBB), Bytes{0xFF})), tracks)), 0, "unknown-size Seek");
    // Back references do not cause recursive SeekHead traversal.
    expect(file(join(seekHead(0, 0x114D9B74), tracks)), 41666667, "irrelevant SeekHead reference");
    const auto failedRead = trussc::internal::readMatroskaDuration(complete.size(), [](uint64_t, uint8_t*, size_t) { return false; });
    check(!failedRead.isMatroska && failedRead.nanoseconds == 0, "failed read");

    const auto shortRead = trussc::internal::readMatroskaDuration(complete.size(), [&](uint64_t offset, uint8_t* output, size_t count) {
        if (offset >= header().size()) return false;
        std::copy_n(complete.data() + size_t(offset), count, output);
        return true;
    });
    check(shortRead.isMatroska && shortRead.nanoseconds == 0, "failed seek after DocType");

    Bytes list;
    for (size_t i = 0; i < 5000; ++i) list = join(list, element(0xEC, {}));
    expect(file(join(list, tracks)), 0, "Segment element cap");
    expect(file(element(0x1654AE6B, join(list, track(1, 41666667)))), 0, "Tracks element cap");
    expect(file(element(0x1654AE6B, element(0xAE, join(list,
        join(element(0x83, Bytes{1}), element(0x23E383, number(41666667))))))), 0, "TrackEntry element cap");
    expect(file(join(element(0x114D9B74, list), tracks)), 0, "SeekHead element cap");
    Bytes nested;
    for (size_t i = 0; i < 5000; ++i) nested = element(0xEC, nested);
    expect(file(join(nested, tracks)), 41666667, "unknown nesting skipped without recursion");
}
} // namespace

int main() {
    validFiles();
    malformedFiles();
    std::cout << "Matroska DefaultDuration tests passed\n";
}
