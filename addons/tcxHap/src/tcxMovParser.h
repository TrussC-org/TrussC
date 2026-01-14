#pragma once

// =============================================================================
// tcxMovParser - Lightweight QuickTime/ISO BMFF MOV Parser
// =============================================================================
// Parses MOV container to extract video/audio track information and frame data.
// Designed for HAP codec support but works with any MOV file.

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>
#include <cstring>

namespace tcx::hap {

// FourCC constants for HAP codecs
constexpr uint32_t FOURCC_HAP1 = 0x48617031; // 'Hap1' - HAP (DXT1)
constexpr uint32_t FOURCC_HAP5 = 0x48617035; // 'Hap5' - HAP Alpha (DXT5)
constexpr uint32_t FOURCC_HAPY = 0x48617059; // 'HapY' - HAPQ (YCoCg DXT5)
constexpr uint32_t FOURCC_HAPM = 0x4861704D; // 'HapM' - HAPQ Alpha
constexpr uint32_t FOURCC_HAPA = 0x48617041; // 'HapA' - HAP Alpha Only

// Common atom types
constexpr uint32_t ATOM_FTYP = 0x66747970;
constexpr uint32_t ATOM_MOOV = 0x6D6F6F76;
constexpr uint32_t ATOM_MVHD = 0x6D766864;
constexpr uint32_t ATOM_TRAK = 0x7472616B;
constexpr uint32_t ATOM_TKHD = 0x746B6864;
constexpr uint32_t ATOM_MDIA = 0x6D646961;
constexpr uint32_t ATOM_MDHD = 0x6D646864;
constexpr uint32_t ATOM_HDLR = 0x68646C72;
constexpr uint32_t ATOM_MINF = 0x6D696E66;
constexpr uint32_t ATOM_STBL = 0x7374626C;
constexpr uint32_t ATOM_STSD = 0x73747364;
constexpr uint32_t ATOM_STTS = 0x73747473;
constexpr uint32_t ATOM_STSC = 0x73747363;
constexpr uint32_t ATOM_STSZ = 0x7374737A;
constexpr uint32_t ATOM_STCO = 0x7374636F;
constexpr uint32_t ATOM_CO64 = 0x636F3634;
constexpr uint32_t ATOM_MDAT = 0x6D646174;

// Handler types
constexpr uint32_t HANDLER_VIDE = 0x76696465; // 'vide'
constexpr uint32_t HANDLER_SOUN = 0x736F756E; // 'soun'

// Audio codec FourCCs
constexpr uint32_t FOURCC_SOWT = 0x736F7774; // 'sowt' - 16-bit LE PCM
constexpr uint32_t FOURCC_TWOS = 0x74776F73; // 'twos' - 16-bit BE PCM
constexpr uint32_t FOURCC_LPCM = 0x6C70636D; // 'lpcm' - Linear PCM
constexpr uint32_t FOURCC_FL32 = 0x666C3332; // 'fl32' - 32-bit float
constexpr uint32_t FOURCC_MP3  = 0x2E6D7033; // '.mp3' - MP3
constexpr uint32_t FOURCC_MP4A = 0x6D703461; // 'mp4a' - AAC

// -----------------------------------------------------------------------------
// Sample (frame) information
// -----------------------------------------------------------------------------
struct MovSample {
    uint64_t offset = 0;      // File offset
    uint32_t size = 0;        // Sample size in bytes
    uint32_t duration = 0;    // Duration in timescale units
    double timestamp = 0.0;   // Timestamp in seconds
};

// -----------------------------------------------------------------------------
// Track information
// -----------------------------------------------------------------------------
struct MovTrack {
    uint32_t trackId = 0;
    uint32_t handlerType = 0; // HANDLER_VIDE or HANDLER_SOUN
    uint32_t codecFourCC = 0;
    uint32_t timescale = 0;
    uint64_t duration = 0;    // In timescale units

    // Video specific
    uint32_t width = 0;
    uint32_t height = 0;

    // Audio specific
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;

    // Sample table
    std::vector<MovSample> samples;

    bool isVideo() const { return handlerType == HANDLER_VIDE; }
    bool isAudio() const { return handlerType == HANDLER_SOUN; }
    bool isHap() const {
        return codecFourCC == FOURCC_HAP1 ||
               codecFourCC == FOURCC_HAP5 ||
               codecFourCC == FOURCC_HAPY ||
               codecFourCC == FOURCC_HAPM ||
               codecFourCC == FOURCC_HAPA;
    }

    bool isPcm() const {
        return codecFourCC == FOURCC_SOWT ||
               codecFourCC == FOURCC_TWOS ||
               codecFourCC == FOURCC_LPCM ||
               codecFourCC == FOURCC_FL32;
    }

    bool isMp3() const {
        return codecFourCC == FOURCC_MP3;
    }

    bool isBigEndianPcm() const {
        return codecFourCC == FOURCC_TWOS;
    }

    bool isFloatPcm() const {
        return codecFourCC == FOURCC_FL32;
    }

    double getDurationSeconds() const {
        return timescale > 0 ? (double)duration / timescale : 0.0;
    }
};

// -----------------------------------------------------------------------------
// Movie information
// -----------------------------------------------------------------------------
struct MovInfo {
    uint32_t timescale = 0;
    uint64_t duration = 0;
    std::vector<MovTrack> tracks;

    double getDurationSeconds() const {
        return timescale > 0 ? (double)duration / timescale : 0.0;
    }

    const MovTrack* getVideoTrack() const {
        for (const auto& track : tracks) {
            if (track.isVideo()) return &track;
        }
        return nullptr;
    }

    const MovTrack* getAudioTrack() const {
        for (const auto& track : tracks) {
            if (track.isAudio()) return &track;
        }
        return nullptr;
    }

    bool hasHapVideo() const {
        auto* video = getVideoTrack();
        return video && video->isHap();
    }
};

// -----------------------------------------------------------------------------
// MOV Parser
// -----------------------------------------------------------------------------
class MovParser {
public:
    MovParser() = default;
    ~MovParser() { close(); }

    // Non-copyable but movable
    MovParser(const MovParser&) = delete;
    MovParser& operator=(const MovParser&) = delete;

    MovParser(MovParser&& other) noexcept
        : file_(std::move(other.file_))
        , fileSize_(other.fileSize_)
        , info_(std::move(other.info_)) {
        other.fileSize_ = 0;
    }

    MovParser& operator=(MovParser&& other) noexcept {
        if (this != &other) {
            close();
            file_ = std::move(other.file_);
            fileSize_ = other.fileSize_;
            info_ = std::move(other.info_);
            other.fileSize_ = 0;
        }
        return *this;
    }

    bool open(const std::string& path) {
        close();
        file_.open(path, std::ios::binary);
        if (!file_.is_open()) return false;

        // Get file size
        file_.seekg(0, std::ios::end);
        fileSize_ = file_.tellg();
        file_.seekg(0, std::ios::beg);

        return parse();
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
        }
        info_ = MovInfo();
    }

    bool isOpen() const { return file_.is_open(); }
    const MovInfo& getInfo() const { return info_; }

    // Read sample data from file
    bool readSample(const MovTrack& track, size_t sampleIndex, std::vector<uint8_t>& data) {
        if (sampleIndex >= track.samples.size()) return false;

        const auto& sample = track.samples[sampleIndex];
        data.resize(sample.size);

        file_.seekg(sample.offset);
        file_.read(reinterpret_cast<char*>(data.data()), sample.size);

        return file_.good();
    }

    // Static helper to check if file is HAP without full parse
    static bool isHapFile(const std::string& path) {
        MovParser parser;
        if (!parser.open(path)) return false;
        return parser.getInfo().hasHapVideo();
    }

    // Get FourCC as string for debugging
    static std::string fourccToString(uint32_t fourcc) {
        char str[5] = {0};
        str[0] = (fourcc >> 24) & 0xFF;
        str[1] = (fourcc >> 16) & 0xFF;
        str[2] = (fourcc >> 8) & 0xFF;
        str[3] = fourcc & 0xFF;
        return std::string(str);
    }

private:
    std::ifstream file_;
    uint64_t fileSize_ = 0;
    MovInfo info_;

    // Read big-endian integers
    uint16_t readU16() {
        uint8_t buf[2];
        file_.read(reinterpret_cast<char*>(buf), 2);
        return (buf[0] << 8) | buf[1];
    }

    uint32_t readU32() {
        uint8_t buf[4];
        file_.read(reinterpret_cast<char*>(buf), 4);
        return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    }

    uint64_t readU64() {
        uint64_t high = readU32();
        uint64_t low = readU32();
        return (high << 32) | low;
    }

    // Read fixed-point 16.16
    float readFixed32() {
        int32_t val = static_cast<int32_t>(readU32());
        return val / 65536.0f;
    }

    // Parse entire file
    bool parse() {
        while (file_.tellg() < static_cast<std::streampos>(fileSize_)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            uint64_t dataSize;
            if (atomSize == 1) {
                // Extended size
                dataSize = readU64() - 16;
            } else if (atomSize == 0) {
                // Atom extends to end of file
                dataSize = fileSize_ - file_.tellg();
            } else {
                dataSize = atomSize - 8;
            }

            uint64_t atomEnd = file_.tellg();
            atomEnd += dataSize;

            if (atomType == ATOM_MOOV) {
                parseMoov(atomEnd);
            }

            // Skip to next atom
            file_.seekg(atomEnd);
        }

        return !info_.tracks.empty();
    }

    void parseMoov(uint64_t endPos) {
        while (file_.tellg() < static_cast<std::streampos>(endPos)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            if (atomSize < 8) break;
            uint64_t atomEnd = atomStart + atomSize;

            if (atomType == ATOM_MVHD) {
                parseMvhd();
            } else if (atomType == ATOM_TRAK) {
                parseTrak(atomEnd);
            }

            file_.seekg(atomEnd);
        }
    }

    void parseMvhd() {
        uint8_t version = file_.get();
        file_.seekg(3, std::ios::cur); // flags

        if (version == 1) {
            file_.seekg(16, std::ios::cur); // creation/modification time
            info_.timescale = readU32();
            info_.duration = readU64();
        } else {
            file_.seekg(8, std::ios::cur); // creation/modification time
            info_.timescale = readU32();
            info_.duration = readU32();
        }
    }

    void parseTrak(uint64_t endPos) {
        MovTrack track;

        while (file_.tellg() < static_cast<std::streampos>(endPos)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            if (atomSize < 8) break;
            uint64_t atomEnd = atomStart + atomSize;

            if (atomType == ATOM_TKHD) {
                parseTkhd(track);
            } else if (atomType == ATOM_MDIA) {
                parseMdia(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }

        // Build sample table with timestamps
        buildSampleTimestamps(track);

        if (track.handlerType != 0) {
            info_.tracks.push_back(std::move(track));
        }
    }

    void parseTkhd(MovTrack& track) {
        uint8_t version = file_.get();
        file_.seekg(3, std::ios::cur); // flags

        if (version == 1) {
            file_.seekg(16, std::ios::cur); // creation/modification time
            track.trackId = readU32();
            file_.seekg(4, std::ios::cur); // reserved
            file_.seekg(8, std::ios::cur); // duration
        } else {
            file_.seekg(8, std::ios::cur); // creation/modification time
            track.trackId = readU32();
            file_.seekg(4, std::ios::cur); // reserved
            file_.seekg(4, std::ios::cur); // duration
        }

        file_.seekg(8, std::ios::cur);  // reserved
        file_.seekg(2, std::ios::cur);  // layer
        file_.seekg(2, std::ios::cur);  // alternate group
        file_.seekg(2, std::ios::cur);  // volume
        file_.seekg(2, std::ios::cur);  // reserved
        file_.seekg(36, std::ios::cur); // matrix

        track.width = static_cast<uint32_t>(readFixed32());
        track.height = static_cast<uint32_t>(readFixed32());
    }

    void parseMdia(MovTrack& track, uint64_t endPos) {
        while (file_.tellg() < static_cast<std::streampos>(endPos)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            if (atomSize < 8) break;
            uint64_t atomEnd = atomStart + atomSize;

            if (atomType == ATOM_MDHD) {
                parseMdhd(track);
            } else if (atomType == ATOM_HDLR) {
                parseHdlr(track);
            } else if (atomType == ATOM_MINF) {
                parseMinf(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }
    }

    void parseMdhd(MovTrack& track) {
        uint8_t version = file_.get();
        file_.seekg(3, std::ios::cur); // flags

        if (version == 1) {
            file_.seekg(16, std::ios::cur); // creation/modification time
            track.timescale = readU32();
            track.duration = readU64();
        } else {
            file_.seekg(8, std::ios::cur); // creation/modification time
            track.timescale = readU32();
            track.duration = readU32();
        }
    }

    void parseHdlr(MovTrack& track) {
        file_.seekg(4, std::ios::cur); // version + flags
        file_.seekg(4, std::ios::cur); // pre_defined
        track.handlerType = readU32();
    }

    void parseMinf(MovTrack& track, uint64_t endPos) {
        while (file_.tellg() < static_cast<std::streampos>(endPos)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            if (atomSize < 8) break;
            uint64_t atomEnd = atomStart + atomSize;

            if (atomType == ATOM_STBL) {
                parseStbl(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }
    }

    void parseStbl(MovTrack& track, uint64_t endPos) {
        // Temporary storage for sample table data
        std::vector<uint32_t> sampleSizes;
        std::vector<uint64_t> chunkOffsets;
        std::vector<std::pair<uint32_t, uint32_t>> sampleToChunk; // firstChunk, samplesPerChunk
        std::vector<std::pair<uint32_t, uint32_t>> timeToSample;  // count, delta

        while (file_.tellg() < static_cast<std::streampos>(endPos)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();

            if (atomSize < 8) break;
            uint64_t atomEnd = atomStart + atomSize;

            if (atomType == ATOM_STSD) {
                parseStsd(track);
            } else if (atomType == ATOM_STTS) {
                parseStts(timeToSample);
            } else if (atomType == ATOM_STSC) {
                parseStsc(sampleToChunk);
            } else if (atomType == ATOM_STSZ) {
                parseStsz(sampleSizes);
            } else if (atomType == ATOM_STCO) {
                parseStco(chunkOffsets);
            } else if (atomType == ATOM_CO64) {
                parseCo64(chunkOffsets);
            }

            file_.seekg(atomEnd);
        }

        // Build sample list
        buildSamples(track, sampleSizes, chunkOffsets, sampleToChunk, timeToSample);
    }

    void parseStsd(MovTrack& track) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        if (entryCount > 0) {
            uint32_t entrySize = readU32();
            track.codecFourCC = readU32();

            file_.seekg(6, std::ios::cur);  // reserved
            file_.seekg(2, std::ios::cur);  // data reference index

            if (track.isVideo()) {
                file_.seekg(2, std::ios::cur);  // version
                file_.seekg(2, std::ios::cur);  // revision
                file_.seekg(4, std::ios::cur);  // vendor
                file_.seekg(4, std::ios::cur);  // temporal quality
                file_.seekg(4, std::ios::cur);  // spatial quality
                track.width = readU16();
                track.height = readU16();
            } else if (track.isAudio()) {
                file_.seekg(2, std::ios::cur);  // version
                file_.seekg(2, std::ios::cur);  // revision
                file_.seekg(4, std::ios::cur);  // vendor
                track.channels = readU16();
                track.bitsPerSample = readU16();
                file_.seekg(2, std::ios::cur);  // compression id
                file_.seekg(2, std::ios::cur);  // packet size
                track.sampleRate = readU16();   // Only integer part
                file_.seekg(2, std::ios::cur);  // Fixed point fraction
            }
        }
    }

    void parseStts(std::vector<std::pair<uint32_t, uint32_t>>& timeToSample) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        timeToSample.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; i++) {
            uint32_t count = readU32();
            uint32_t delta = readU32();
            timeToSample.push_back({count, delta});
        }
    }

    void parseStsc(std::vector<std::pair<uint32_t, uint32_t>>& sampleToChunk) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        sampleToChunk.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; i++) {
            uint32_t firstChunk = readU32();
            uint32_t samplesPerChunk = readU32();
            file_.seekg(4, std::ios::cur); // sample description index
            sampleToChunk.push_back({firstChunk, samplesPerChunk});
        }
    }

    void parseStsz(std::vector<uint32_t>& sampleSizes) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t sampleSize = readU32();
        uint32_t sampleCount = readU32();

        sampleSizes.reserve(sampleCount);
        if (sampleSize == 0) {
            // Variable size samples
            for (uint32_t i = 0; i < sampleCount; i++) {
                sampleSizes.push_back(readU32());
            }
        } else {
            // Constant size samples
            for (uint32_t i = 0; i < sampleCount; i++) {
                sampleSizes.push_back(sampleSize);
            }
        }
    }

    void parseStco(std::vector<uint64_t>& chunkOffsets) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        chunkOffsets.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; i++) {
            chunkOffsets.push_back(readU32());
        }
    }

    void parseCo64(std::vector<uint64_t>& chunkOffsets) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        chunkOffsets.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; i++) {
            chunkOffsets.push_back(readU64());
        }
    }

    void buildSamples(MovTrack& track,
                      const std::vector<uint32_t>& sampleSizes,
                      const std::vector<uint64_t>& chunkOffsets,
                      const std::vector<std::pair<uint32_t, uint32_t>>& sampleToChunk,
                      const std::vector<std::pair<uint32_t, uint32_t>>& timeToSample) {

        if (sampleSizes.empty() || chunkOffsets.empty() || sampleToChunk.empty()) {
            return;
        }

        track.samples.reserve(sampleSizes.size());

        size_t sampleIndex = 0;
        size_t stscIndex = 0;

        for (size_t chunkIndex = 0; chunkIndex < chunkOffsets.size(); chunkIndex++) {
            // Find samples per chunk for this chunk
            while (stscIndex + 1 < sampleToChunk.size() &&
                   chunkIndex + 1 >= sampleToChunk[stscIndex + 1].first) {
                stscIndex++;
            }

            uint32_t samplesInChunk = sampleToChunk[stscIndex].second;
            uint64_t offset = chunkOffsets[chunkIndex];

            for (uint32_t i = 0; i < samplesInChunk && sampleIndex < sampleSizes.size(); i++) {
                MovSample sample;
                sample.offset = offset;
                sample.size = sampleSizes[sampleIndex];
                offset += sample.size;

                track.samples.push_back(sample);
                sampleIndex++;
            }
        }
    }

    void buildSampleTimestamps(MovTrack& track) {
        if (track.samples.empty() || track.timescale == 0) return;

        // For now, assume constant frame rate (simplified)
        double frameDuration = track.getDurationSeconds() / track.samples.size();

        for (size_t i = 0; i < track.samples.size(); i++) {
            track.samples[i].timestamp = i * frameDuration;
            track.samples[i].duration = static_cast<uint32_t>(
                frameDuration * track.timescale);
        }
    }
};

} // namespace tcx::hap
