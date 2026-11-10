#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace trussc::internal {

struct MatroskaDuration {
    bool isMatroska = false; // Includes WebM; independent of the filename.
    uint64_t nanoseconds = 0; // Missing or invalid declaration.
};

// A metadata reader, not a demuxer. ReadAt(offset, bytes, count) must return
// false on a failed seek or short read. No media blocks are read or decoded.
template<class ReadAt>
class MatroskaDurationReader {
public:
    MatroskaDurationReader(uint64_t fileSize, ReadAt readAt)
        : fileSize_(fileSize), readAt_(readAt) {}

    MatroskaDuration read() {
        MatroskaDuration result;
        uint64_t cursor = 0;
        Element header;
        if (!element(cursor, fileSize_, header) || header.id != 0x1A45DFA3 || header.unknown)
            return result;
        cursor = header.data;
        while (cursor < header.end) {
            Element child;
            if (!element(cursor, header.end, child) || child.unknown) return result;
            if (child.id == 0x4282) { // DocType
                uint8_t text[8]{};
                const auto length = child.end - child.data;
                const auto count = length < sizeof(text) ? size_t(length) : sizeof(text);
                if (!bytes(child.data, text, count, child.end))
                    return result;
                size_t stringLength = 0;
                while (stringLength < count && text[stringLength] != 0) ++stringLength;
                // EBML strings end at the first NUL; inspect at most one
                // byte beyond the longest supported name, regardless of padding.
                if (stringLength == sizeof(text) && length > sizeof(text)) {
                    uint8_t terminator = 0;
                    if (!bytes(child.data + sizeof(text), &terminator, 1, child.end) || terminator != 0)
                        return result;
                }
                result.isMatroska = (stringLength == 8 && std::memcmp(text, "matroska", 8) == 0) ||
                                    (stringLength == 4 && std::memcmp(text, "webm", 4) == 0);
                if (!result.isMatroska) return result;
            }
            cursor = child.end;
        }
        if (!result.isMatroska) return result;
        cursor = header.end;
        while (cursor < fileSize_) {
            Element segment;
            if (!element(cursor, fileSize_, segment)) return result;
            if (segment.id == 0x18538067) { // Only Segment may extend to EOF.
                result.nanoseconds = tracksInSegment(segment);
                return result;
            }
            if (segment.unknown) return result;
            cursor = segment.end;
        }
        return result;
    }

private:
    struct Element {
        uint64_t id = 0, data = 0, end = 0;
        bool unknown = false;
    };
    uint64_t fileSize_;
    ReadAt readAt_;
    // Bound (external-input work): at most 4096 metadata elements per probe,
    // including Seek entries and track fields. Huge lists report unknown.
    size_t remainingElements_ = 4096;

    bool bytes(uint64_t offset, uint8_t* output, size_t count, uint64_t parentEnd) {
        return parentEnd <= fileSize_ && offset <= parentEnd && count <= parentEnd - offset &&
               readAt_(offset, output, count);
    }

    bool vint(uint64_t& cursor, uint64_t parentEnd, bool id, uint64_t& value, bool& unknown) {
        uint8_t first = 0;
        if (!bytes(cursor, &first, 1, parentEnd) || first == 0) return false;
        unsigned length = 1;
        uint8_t marker = 0x80;
        while (!(first & marker)) { marker >>= 1; ++length; }
        if (length > (id ? 4u : 8u)) return false;
        uint8_t buffer[8]{};
        if (!bytes(cursor, buffer, length, parentEnd)) return false;
        value = id ? first : (first & (marker - 1));
        for (unsigned i = 1; i < length; ++i) value = (value << 8) | buffer[i];
        unknown = !id && value == ((uint64_t(1) << (7 * length)) - 1);
        cursor += length;
        return true;
    }

    bool element(uint64_t& cursor, uint64_t parentEnd, Element& output) {
        if (remainingElements_ == 0) return false;
        --remainingElements_;
        uint64_t size = 0;
        bool unused = false;
        if (!vint(cursor, parentEnd, true, output.id, unused) ||
            !vint(cursor, parentEnd, false, size, output.unknown)) return false;
        output.data = cursor;
        if (!output.unknown && size > parentEnd - cursor) return false;
        output.end = output.unknown ? parentEnd : cursor + size;
        return true;
    }

    bool integer(const Element& field, uint64_t& value, size_t limit = 8) {
        const uint64_t length = field.end - field.data;
        uint8_t buffer[8]{};
        if (field.unknown || length == 0 || length > limit ||
            !bytes(field.data, buffer, size_t(length), field.end)) return false;
        value = 0;
        for (size_t i = 0; i < length; ++i) value = (value << 8) | buffer[i];
        return true;
    }

    uint64_t trackDuration(const Element& tracks) {
        uint64_t cursor = tracks.data;
        while (cursor < tracks.end) {
            Element entry;
            if (!element(cursor, tracks.end, entry) || entry.unknown) return 0;
            if (entry.id == 0xAE) { // TrackEntry
                uint64_t fieldCursor = entry.data, type = 0, duration = 0;
                bool hasType = false, hasDuration = false, invalid = false;
                while (fieldCursor < entry.end) {
                    Element field;
                    if (!element(fieldCursor, entry.end, field) || field.unknown) return 0;
                    if (field.id == 0x83) { // TrackType
                        if (hasType) invalid = true;
                        else if (!integer(field, type)) return 0;
                        hasType = true;
                    }
                    if (field.id == 0x23E383) { // DefaultDuration
                        if (hasDuration) invalid = true;
                        else if (!integer(field, duration)) return 0;
                        hasDuration = true;
                    }
                    fieldCursor = field.end;
                }
                if (type == 1) return invalid ? 0 : duration; // Do not substitute a later video track.
            }
            cursor = entry.end;
        }
        return 0;
    }

    bool seekTracks(const Element& head, uint64_t& position, bool& found) {
        uint64_t cursor = head.data;
        while (cursor < head.end) {
            Element seek;
            if (!element(cursor, head.end, seek) || seek.unknown) return false;
            if (seek.id == 0x4DBB) { // Seek
                uint64_t fieldCursor = seek.data, target = 0, offset = 0;
                bool hasPosition = false;
                while (fieldCursor < seek.end) {
                    Element field;
                    if (!element(fieldCursor, seek.end, field) || field.unknown) return false;
                    if (field.id == 0x53AB && !integer(field, target, 4)) return false; // SeekID
                    if (field.id == 0x53AC) { // SeekPosition, relative to Segment data
                        if (!integer(field, offset)) return false;
                        hasPosition = true;
                    }
                    fieldCursor = field.end;
                }
                if (target == 0x1654AE6B && hasPosition && !found) {
                    position = offset;
                    found = true;
                }
            }
            cursor = seek.end;
        }
        return true;
    }

    uint64_t tracksInSegment(const Element& segment) {
        uint64_t cursor = segment.data;
        while (cursor < segment.end) {
            Element child;
            if (!element(cursor, segment.end, child) || child.unknown) return 0;
            if (child.id == 0x1654AE6B) return trackDuration(child); // Tracks
            if (child.id == 0x114D9B74) { // SeekHead
                uint64_t offset = 0;
                bool found = false;
                if (!seekTracks(child, offset, found)) return 0;
                if (found) {
                    if (offset >= segment.end - segment.data) return 0;
                    uint64_t target = segment.data + offset;
                    Element tracks;
                    if (!element(target, segment.end, tracks) || tracks.unknown || tracks.id != 0x1654AE6B)
                        return 0;
                    return trackDuration(tracks);
                }
            }
            cursor = child.end; // Skip payloads, including sized Clusters.
        }
        return 0;
    }
};

template<class ReadAt>
MatroskaDuration readMatroskaDuration(uint64_t fileSize, ReadAt readAt) {
    return MatroskaDurationReader<ReadAt>(fileSize, readAt).read();
}

} // namespace trussc::internal
