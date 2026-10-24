#pragma once

// =============================================================================
// tcxMovParser - Lightweight QuickTime/ISO BMFF MOV Parser
// =============================================================================
// Parses MOV container to extract video/audio track information and frame data.
// Designed for HAP codec support but works with any MOV file.

#include <TrussC.h>

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <memory>
#include <cstring>
#include <cmath>
#include <array>
#include "impl/tcxMovIndex.h"

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
constexpr uint32_t ATOM_WAVE = 0x77617665; // 'wave' - sound description extension
constexpr uint32_t ATOM_ENDA = 0x656E6461; // 'enda' - endianness (1 = little-endian)

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

// formatSpecificFlags of a version 2 'lpcm' sound description
constexpr uint32_t LPCM_FLAG_FLOAT          = 1u << 0;
constexpr uint32_t LPCM_FLAG_BIG_ENDIAN     = 1u << 1;
constexpr uint32_t LPCM_FLAG_SIGNED_INTEGER = 1u << 2;
constexpr uint32_t LPCM_FLAG_NON_INTERLEAVED = 1u << 5;

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
    // True when the sound description has an 'enda' atom set to 1 (in its
    // 'wave' extension), i.e. the PCM samples are little-endian
    bool endaLittleEndian = false;
    // Sound description version (0, 1 or 2)
    uint16_t soundVersion = 0;
    // formatSpecificFlags of a version 2 sound description ('lpcm': see
    // LPCM_FLAG_*)
    uint32_t lpcmFlags = 0;

    // Read-only paged sample table; references last until another page is read.
    // For a PCM track whose 'stsz' gives one constant size,
    // each entry is a whole chunk (pcmChunked is true; an entry holds
    // size / getPcmFrameBytes() audio frames). Otherwise one entry per sample.
    detail::MovSamples<MovSample> samples;
    bool pcmChunked = false;
    // Audio frames in samples (when pcmChunked)
    uint64_t pcmFrameCount = 0;

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

    bool isAac() const {
        return codecFourCC == FOURCC_MP4A;
    }

    bool isBigEndianPcm() const {
        // 'twos' is always big-endian. 'fl32' is big-endian by default in
        // QuickTime and little-endian only with an 'enda' atom set to 1.
        // 'lpcm' carries its byte order in the format flags.
        if (codecFourCC == FOURCC_TWOS) return true;
        if (codecFourCC == FOURCC_FL32) return !endaLittleEndian;
        if (codecFourCC == FOURCC_LPCM) return (lpcmFlags & LPCM_FLAG_BIG_ENDIAN) != 0;
        return false;
    }

    bool isFloatPcm() const {
        if (codecFourCC == FOURCC_LPCM) return (lpcmFlags & LPCM_FLAG_FLOAT) != 0;
        return codecFourCC == FOURCC_FL32;
    }

    // Bits per channel sample of a PCM track ('fl32' is always 32)
    int getPcmBits() const {
        if (codecFourCC == FOURCC_FL32) return 32;
        return bitsPerSample;
    }

    // Bytes per PCM frame (all channels), or 0 when the description does not
    // give 8 to 64 bits per channel in whole bytes. Used in place of the
    // 'stsz' size for constant-size PCM tracks.
    uint32_t getPcmFrameBytes() const {
        if (!isPcm()) return 0;
        const int bits = getPcmBits();
        if (bits <= 0 || bits > 64 || bits % 8 != 0 || channels == 0) return 0;
        return static_cast<uint32_t>(bits / 8) * channels;
    }

    // Whether the PCM format can be decoded (16-bit signed integer or 32-bit
    // float, interleaved, at least one channel, a sample rate above 0).
    // Otherwise why says what is not supported.
    bool isPcmFormatSupported(std::string& why) const {
        if (!isPcm()) { why = "not a PCM codec"; return false; }
        if (soundVersion > 2) {
            why = "sound description version " + std::to_string(soundVersion);
            return false;
        }
        if (codecFourCC == FOURCC_LPCM) {
            if (soundVersion != 2) { why = "'lpcm' needs a version 2 sound description"; return false; }
            if (lpcmFlags & LPCM_FLAG_NON_INTERLEAVED) { why = "non-interleaved 'lpcm'"; return false; }
            const bool isFloat = (lpcmFlags & LPCM_FLAG_FLOAT) != 0;
            if (isFloat && bitsPerSample != 32) {
                why = std::to_string(bitsPerSample) + "-bit float";
                return false;
            }
            if (!isFloat && !(lpcmFlags & LPCM_FLAG_SIGNED_INTEGER)) {
                why = "unsigned integer samples";
                return false;
            }
        }
        const int bits = getPcmBits();
        if (bits != 16 && bits != 32) { why = std::to_string(bits) + "-bit samples"; return false; }
        if (bits == 32 && !isFloatPcm()) { why = "32-bit integer samples"; return false; }
        if (bits == 16 && isFloatPcm()) { why = "16-bit float samples"; return false; }
        if (channels == 0) { why = "0 channels"; return false; }
        if (sampleRate == 0) { why = "sample rate 0"; return false; }
        return true;
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
        , info_(std::move(other.info_))
        , path_(std::move(other.path_))
        , tableSource_(std::move(other.tableSource_)) {
        rejectedCount_ = other.rejectedCount_;
        warned_ = other.warned_;
        metadataBytes_ = other.metadataBytes_;
        residentEntries_ = other.residentEntries_;
        other.fileSize_ = 0;
    }

    MovParser& operator=(MovParser&& other) noexcept {
        if (this != &other) {
            close();
            file_ = std::move(other.file_);
            fileSize_ = other.fileSize_;
            info_ = std::move(other.info_);
            path_ = std::move(other.path_);
            tableSource_ = std::move(other.tableSource_);
            rejectedCount_ = other.rejectedCount_;
            warned_ = other.warned_;
            metadataBytes_ = other.metadataBytes_;
            residentEntries_ = other.residentEntries_;
            other.fileSize_ = 0;
        }
        return *this;
    }

    bool open(const tc::fs::path& path) {
        close();

        try {
            path_ = path;
            // Create a completely fresh fstream object (not reusing old one)
            file_ = std::ifstream(path, std::ios::binary);
            if (!file_.is_open()) return false;

            // Get file size
            file_.seekg(0, std::ios::end);
            fileSize_ = file_.tellg();
            file_.seekg(0, std::ios::beg);

            if (parse() && !rejected_ && !allocationError_) return true;
        } catch (const std::exception&) { allocationError_ = true; }
        if (allocationError_) tc::logError("MovParser") << "MOV allocation failed";
        close();
        return false;
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
        }
        // Reset to fresh state by assigning default-constructed object
        file_ = std::ifstream();
        info_ = MovInfo();
        tableSource_.reset();
        metadataBytes_ = residentEntries_ = 0;
        rejected_ = false;
        allocationError_ = false;
    }

    bool isOpen() const { return file_.is_open(); }
    const MovInfo& getInfo() const { return info_; }

    // Read sample data from file
    bool readSample(const MovTrack& track, size_t sampleIndex, std::vector<uint8_t>& data) {
        data.clear();
        if (sampleIndex >= track.samples.size()) return false;

        const auto sample = track.samples[sampleIndex];
        if (!track.samples.good()) return reject("sample table read failed", false);
        // The sample must lie fully inside the file; this is checked before
        // the buffer is sized
        // Bound (security): untrusted MOV sample sizes must fit the file to keep reads and memory bounded by input. Overflow rejects the sample via false and is counted.
        if (!sample.size || sample.size > detail::movFrameBytes || sample.size > fileSize_ || sample.offset > fileSize_ - sample.size) {
            return reject("sample is empty or exceeds file range or 128 MiB frame budget", false, sample.size, detail::movFrameBytes);
        }
        // Bound (security): encoded frames fit 128 MiB before allocation; exact growth includes old storage in the 512 MiB player set. Overflow rejects the sample and is counted.
        if (!detail::resizeMovBuffer(data, sample.size)) { tc::logError("MovParser") << "MOV sample allocation failed"; return false; }

        if (!readCapturedSample(sample, data.data(), data.size())) {
            data.clear();
            return false;
        }
        return true;
    }

    // Whether a sample lies fully inside the file
    bool isSampleInFile(const MovTrack& track, size_t sampleIndex) const {
        if (sampleIndex >= track.samples.size()) return false;
        const auto sample = track.samples[sampleIndex];
        return track.samples.good() && sample.size && sample.size <= fileSize_ && sample.offset <= fileSize_ - sample.size;
    }

    // Read a sample into dst, which must hold the sample's size. Returns
    // false for a sample that does not lie fully inside the file.
    bool readSampleTo(const MovTrack& track, size_t sampleIndex, uint8_t* dst) {
        if (sampleIndex >= track.samples.size()) return false;
        const auto sample = track.samples[sampleIndex];
        if (!track.samples.good()) return reject("sample table read failed", false);
        return readCapturedSample(sample, dst, sample.size);
    }

    uint64_t getFileSize() const { return fileSize_; }

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
    friend class HapPlayer;
    friend bool loadPcmTrack(MovParser&, const MovTrack&, tc::SoundBuffer&);
    friend struct HapBudgetTestAccess;
    std::ifstream file_;
    uint64_t fileSize_ = 0;
    MovInfo info_;
    tc::fs::path path_;
    std::shared_ptr<detail::MovTableSource> tableSource_;
    const std::shared_ptr<detail::MovTableSource>& tableSource() {
        if (!tableSource_) tableSource_ = std::make_shared<detail::MovTableSource>(path_);
        return tableSource_;
    }

    // Set while a track is parsed when one of its atoms or tables fails a
    // check; parseTrak() then does not keep the track
    bool trackDamaged_ = false;
    bool rejected_ = false, allocationError_ = false;
    uint64_t metadataBytes_ = 0, residentEntries_ = 0;
    uint64_t rejectedCount_ = 0;
    bool warned_ = false;
    bool reject(const char* why, bool wholeFile = true, uint64_t needed = 0, uint64_t capacity = 0, const char* units = "bytes") {
        rejected_ = rejected_ || wholeFile;
        ++rejectedCount_;
        if (!std::exchange(warned_, true)) {
            auto warning = tc::logWarning("MovParser");
            warning << "MOV input rejected: " << why;
            if (capacity) warning << " (needed " << needed << " " << units << "; capacity " << capacity << " " << units << ")";
        }
        return false;
    }

    // Capture a page entry by value and check the real destination capacity:
    // a file can change between an audio size pass and its subsequent read.
    bool readCapturedSample(const MovSample& sample, uint8_t* dst, size_t capacity) {
        if (!sample.size || sample.size > capacity || sample.size > fileSize_ ||
            sample.offset > fileSize_ - sample.size) return false;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(sample.offset));
        file_.read(reinterpret_cast<char*>(dst), sample.size);
        return file_.good();
    }

    // Read big-endian integers. A failed read returns 0 (the stream state
    // tells the caller that it failed).
    uint16_t readU16() {
        uint8_t buf[2] = {0, 0};
        if (!file_.read(reinterpret_cast<char*>(buf), 2)) return 0;
        return (buf[0] << 8) | buf[1];
    }

    uint32_t readU32() {
        uint8_t buf[4] = {0, 0, 0, 0};
        if (!file_.read(reinterpret_cast<char*>(buf), 4)) return 0;
        return (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) |
               (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);
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

    // Current read position, or false when the stream has failed
    bool position(uint64_t& pos) {
        if (!file_.good()) return false;
        std::streampos p = file_.tellg();
        if (p < 0) return false;
        pos = static_cast<uint64_t>(p);
        return true;
    }

    // Read the header of the next child atom of a container that ends at
    // endPos. Returns false when there is no further valid child: the stream
    // has failed, the header does not fit, the size is below 8, or the child
    // ends past its parent. bad is set for the last three, so a caller can
    // tell an atom that fails these checks from the end of the container.
    bool readChild(uint64_t endPos, uint32_t& atomType, uint64_t& atomEnd, bool& bad) {
        bad = false;
        uint64_t atomStart = 0;
        if (!position(atomStart) || atomStart >= endPos) return false;
        if (endPos - atomStart < 8) { bad = true; return false; }
        uint32_t atomSize = readU32();
        atomType = readU32();
        if (!file_.good()) { bad = true; return false; }
        // Bound (security): untrusted MOV atom lengths must fit the enclosing atom to bound parser reads. Overflow returns false, marks the atom invalid and is counted by the enclosing parser.
        if (atomSize < 8 || atomSize > endPos - atomStart) { bad = true; return false; }
        atomEnd = atomStart + atomSize;
        return true;
    }

    void markTrackDamaged(const MovTrack& track, const char* atom, const char* what) {
        (void)track; (void)atom;
        reject(what, false);
        trackDamaged_ = true;
    }

    // Parse entire file
    bool parse() {
        while (file_.good() && file_.tellg() < static_cast<std::streampos>(fileSize_)) {
            uint64_t atomStart = file_.tellg();
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();
            if (!file_.good()) break;

            // Reject malformed atoms (< 8 bytes is impossible for standard atom
            // header; 0 and 1 are legal extensions). Without this the dataSize
            // computation underflows and the seek wraps, causing an infinite loop.
            if (atomSize > 0 && atomSize < 8 && atomSize != 1) break;

            uint64_t dataSize;
            if (atomSize == 1) {
                // Extended size
                uint64_t extSize = readU64();
                if (!file_.good()) break;
                if (extSize < 16) break;  // guard underflow
                dataSize = extSize - 16;
            } else if (atomSize == 0) {
                // Atom extends to end of file
                dataSize = fileSize_ - file_.tellg();
            } else {
                dataSize = atomSize - 8;
            }

            uint64_t dataStart = static_cast<uint64_t>(file_.tellg());
            // Progress guard: atom must advance past its start and stay within file.
            if (dataSize > fileSize_ - dataStart) break;
            uint64_t atomEnd = dataStart + dataSize;
            if (atomEnd <= atomStart) break;

            if (atomType == ATOM_MOOV) {
                parseMoov(atomEnd);
                if (rejected_ || allocationError_) return false;
            }

            // Skip to next atom
            file_.seekg(atomEnd);
        }

        return !info_.tracks.empty();
    }

    void parseMoov(uint64_t endPos) {
        uint32_t atomType = 0;
        uint64_t atomEnd = 0;
        bool bad = false;
        // A child atom that fails the checks ends the walk; the tracks
        // already parsed stay
        while (readChild(endPos, atomType, atomEnd, bad)) {
            if (atomType == ATOM_MVHD) {
                parseMvhd();
            } else if (atomType == ATOM_TRAK) {
                parseTrak(atomEnd);
                if (rejected_ || allocationError_) return;
            }

            file_.seekg(atomEnd);
        }
        if (bad) {
            reject("moov child does not fit its parent; later tracks skipped", false);
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
        trackDamaged_ = false;

        uint32_t atomType = 0;
        uint64_t atomEnd = 0;
        bool bad = false;
        while (readChild(endPos, atomType, atomEnd, bad)) {
            if (atomType == ATOM_TKHD) {
                parseTkhd(track);
            } else if (atomType == ATOM_MDIA) {
                parseMdia(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }
        if (bad && !trackDamaged_) markTrackDamaged(track, "trak", "child atom does not fit");
        if (!file_.good() && !trackDamaged_) markTrackDamaged(track, "trak", "read failed");

        if (trackDamaged_) return;

        track.samples.setTiming(track.getDurationSeconds(), track.timescale, track.sampleRate);

        if (track.handlerType != 0) {
            // Bound (security): track descriptors, stream buffers and pages share 64 MiB and 1,000,000 resident entries. Overflow rejects the file and is counted.
            const uint64_t entries = track.samples.reservedEntries();
            const uint64_t bytes = track.samples.reservedBytes() + 3 * sizeof(MovTrack);
            // Include the worst in-progress track/timing pages and vector
            // replacement; even empty derived views reserve source pages. No cap is preallocated.
            if (residentEntries_ + entries + 1280 > 1000000) {
                reject("reserved MOV table/sample entry capacity exceeded", true,
                       residentEntries_ + entries + 1280, 1000000, "entries");
                return;
            }
            if (metadataBytes_ + bytes + 256u * 1024 > 64u * 1024 * 1024) {
                reject("track metadata exceeds 64 MiB including vector replacement", true,
                       metadataBytes_ + bytes + 256u * 1024, 64u * 1024 * 1024);
                return;
            }
            try {
                info_.tracks.push_back(std::move(track));
                metadataBytes_ += bytes;
                residentEntries_ += entries;
            } catch (const std::exception&) { allocationError_ = true; }
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
        uint32_t atomType = 0;
        uint64_t atomEnd = 0;
        bool bad = false;
        while (readChild(endPos, atomType, atomEnd, bad)) {
            if (atomType == ATOM_MDHD) {
                parseMdhd(track);
            } else if (atomType == ATOM_HDLR) {
                parseHdlr(track);
            } else if (atomType == ATOM_MINF) {
                parseMinf(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }
        if (bad && !trackDamaged_) markTrackDamaged(track, "mdia", "child atom does not fit");
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
        uint32_t atomType = 0;
        uint64_t atomEnd = 0;
        bool bad = false;
        while (readChild(endPos, atomType, atomEnd, bad)) {
            if (atomType == ATOM_STBL) {
                parseStbl(track, atomEnd);
            }

            file_.seekg(atomEnd);
        }
        if (bad && !trackDamaged_) markTrackDamaged(track, "minf", "child atom does not fit");
    }

    // Sample sizes from 'stsz': either one size for every sample (kept as
    // the (size, count) pair, not expanded) or one size per sample
    struct SampleSizes {
        uint32_t constantSize = 0;  // 0: sizes are in 'sizes'
        uint32_t count = 0;
        detail::MovTable<uint32_t> sizes;

    };

    void parseStbl(MovTrack& track, uint64_t endPos) {
        // Temporary storage for sample table data
        SampleSizes sampleSizes;
        detail::MovTable<uint64_t> chunkOffsets;
        detail::MovTable<std::pair<uint32_t, uint32_t>> sampleToChunk; // firstChunk, samplesPerChunk
        detail::MovTable<std::pair<uint32_t, uint32_t>> timeToSample;  // count, delta

        uint32_t atomType = 0;
        uint64_t atomEnd = 0;
        bool bad = false;
        while (readChild(endPos, atomType, atomEnd, bad)) {
            if (atomType == ATOM_STSD) {
                parseStsd(track, atomEnd);
            } else if (atomType == ATOM_STTS) {
                if (!parseStts(timeToSample, atomEnd)) {
                    reject("stts count does not fit the atom; timing table ignored", false);
                    timeToSample.clear();
                    file_.clear();
                }
            } else if (atomType == ATOM_STSC) {
                if (!parseStsc(sampleToChunk, atomEnd)) {
                    markTrackDamaged(track, "stsc", "entry count does not fit in the atom");
                }
            } else if (atomType == ATOM_STSZ) {
                if (!parseStsz(sampleSizes, atomEnd)) {
                    markTrackDamaged(track, "stsz", "entry count does not fit in the atom");
                }
            } else if (atomType == ATOM_STCO) {
                if (!parseStco(chunkOffsets, atomEnd)) {
                    markTrackDamaged(track, "stco", "entry count does not fit in the atom");
                }
            } else if (atomType == ATOM_CO64) {
                if (!parseCo64(chunkOffsets, atomEnd)) {
                    markTrackDamaged(track, "co64", "entry count does not fit in the atom");
                }
            }

            if (trackDamaged_) return;
            file_.seekg(atomEnd);
        }
        if (bad) {
            markTrackDamaged(track, "stbl", "child atom does not fit");
            return;
        }

        // Build sample list
        if (!buildSamples(track, sampleSizes, chunkOffsets, sampleToChunk, timeToSample)) {
            markTrackDamaged(track, "stsz", "sample layout or table read failed");
        }
    }

    void parseStsd(MovTrack& track, uint64_t atomEnd) {
        file_.seekg(4, std::ios::cur); // version + flags
        uint32_t entryCount = readU32();

        uint64_t pos = 0;
        // Bound (security): sample-description counts and each description fit their atom before reads. Overflow rejects the track and is counted.
        if (!position(pos) || pos > atomEnd || entryCount > (atomEnd - pos) / 8) {
            markTrackDamaged(track, "stsd", "description count does not fit the atom"); return;
        }
        if (entryCount > 0) {
            uint64_t entryStart = static_cast<uint64_t>(file_.tellg());
            uint32_t entrySize = readU32();
            track.codecFourCC = readU32();
            if (entrySize < 36 || entrySize > atomEnd - entryStart) {
                markTrackDamaged(track, "stsd", "description does not fit the atom"); return;
            }

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
                parseSoundDescription(track, entryStart, entrySize);
            }
        }
    }

    // Sound description (QuickTime SoundDescription v0 / v1 / v2). The
    // stream is at the version field.
    void parseSoundDescription(MovTrack& track, uint64_t entryStart, uint32_t entrySize) {
        const uint16_t version = readU16();
        file_.seekg(2, std::ios::cur);  // revision
        file_.seekg(4, std::ios::cur);  // vendor
        track.soundVersion = version;
        track.channels = readU16();
        track.bitsPerSample = readU16();
        file_.seekg(2, std::ios::cur);  // compression id
        file_.seekg(2, std::ios::cur);  // packet size
        // 16.16 fixed point, rounded to whole Hz
        const uint64_t rateFixed = readU32();
        track.sampleRate = static_cast<uint32_t>((rateFixed + 0x8000) >> 16);

        if ((version == 1 && entrySize < 52) || (version == 2 && entrySize < 72)) {
            markTrackDamaged(track, "stsd", "sound description is cut off"); return;
        }
        if (version == 1) {
            const uint32_t samplesPerPacket = readU32();
            const uint32_t bytesPerPacket = readU32();  // per channel
            const uint32_t bytesPerFrame = readU32();   // all channels per packet
            // Legacy bytesPerSample is 2 even for ffmpeg's 32-bit float
            // entries; bytesPerPacket/bytesPerFrame describe the storage.
            readU32();
            std::string why;
            if (track.isPcmFormatSupported(why) &&
                (samplesPerPacket == 0 ||
                 uint64_t(bytesPerPacket) != uint64_t(samplesPerPacket) * track.getPcmBits() / 8 ||
                 uint64_t(bytesPerFrame) != uint64_t(samplesPerPacket) * track.getPcmFrameBytes())) {
                markTrackDamaged(track, "stsd", "PCM packet layout is not supported");
                return;
            }
        } else if (version == 2) {
            // In v2 the v0 fields above hold fixed values; the real ones follow
            file_.seekg(4, std::ios::cur);  // sizeOfStructOnly
            const uint64_t rateBits = readU64();
            const uint32_t numChannels = readU32();
            file_.seekg(4, std::ios::cur);  // always 0x7F000000
            const uint32_t bitsPerChannel = readU32();
            track.lpcmFlags = readU32();
            const uint32_t bytesPerPacket = readU32();
            const uint32_t framesPerPacket = readU32();

            double rate = 0.0;
            std::memcpy(&rate, &rateBits, sizeof(rate));
            // The decoder accepts an int sample rate; reject rates whose
            // rounded value cannot be represented by it.
            const double roundedRate = std::round(rate);
            track.sampleRate = (std::isfinite(roundedRate) && roundedRate >= 1.0 &&
                                roundedRate <= 2147483647.0)
                ? static_cast<uint32_t>(roundedRate) : 0;
            track.channels = numChannels <= 0xFFFF ? static_cast<uint16_t>(numChannels) : 0;
            track.bitsPerSample = bitsPerChannel <= 0xFFFF ? static_cast<uint16_t>(bitsPerChannel) : 0;
            std::string why;
            if (track.isPcmFormatSupported(why) &&
                (framesPerPacket == 0 ||
                 uint64_t(bytesPerPacket) != uint64_t(framesPerPacket) * track.getPcmFrameBytes())) {
                markTrackDamaged(track, "stsd", "PCM packet layout is not supported");
                return;
            }
        }

        // Extension atoms follow the version-specific fields
        uint64_t extStart = 0;
        if (!position(extStart)) return;
        uint64_t entryEnd = entryStart + entrySize;
        if (entrySize >= 8 && extStart < entryEnd && entryEnd <= fileSize_) {
            parseSoundExtensions(track, extStart, entryEnd);
        }
    }

    // Walk the atoms after a sound description's fixed fields (directly or
    // inside 'wave') and pick up 'enda'. QuickTime puts 'enda' inside a
    // single 'wave' extension, so 'wave' is read one level deep: a 'wave'
    // inside 'wave' is skipped.
    void parseSoundExtensions(MovTrack& track, uint64_t pos, uint64_t endPos,
                              bool insideWave = false) {
        while (pos + 8 <= endPos) {
            file_.seekg(pos);
            uint32_t atomSize = readU32();
            uint32_t atomType = readU32();
            if (!file_.good() || atomSize < 8 || pos + atomSize > endPos) break;

            if (atomType == ATOM_WAVE) {
                if (!insideWave) parseSoundExtensions(track, pos + 8, pos + atomSize, true);
            } else if (atomType == ATOM_ENDA && atomSize >= 10) {
                track.endaLittleEndian = (readU16() & 0xFF) == 1;
            }
            pos += atomSize;
        }
    }

    // Read the version/flags and entry count of a table atom that ends at
    // atomEnd. Returns false when the stream fails or when count entries of
    // entrySize bytes do not fit between the count and atomEnd.
    bool readTableCount(uint64_t atomEnd, uint32_t entrySize, uint32_t& count) {
        file_.seekg(4, std::ios::cur); // version + flags
        count = readU32();
        uint64_t pos = 0;
        if (!position(pos) || pos > atomEnd) return false;
        // Bound (security): untrusted MOV table counts must fit the atom before allocating entries. Overflow rejects the table via false and is counted by the enclosing parser.
        return count <= (atomEnd - pos) / entrySize;
    }

    template<class T>
    bool table(detail::MovTable<T>& dst, uint64_t atomEnd, uint32_t stride) {
        uint32_t count = 0;
        if (!readTableCount(atomEnd, stride, count)) return false;
        const auto pos = file_.tellg();
        return dst.open(count ? tableSource() : nullptr, uint64_t(pos), count, stride);
    }
    bool parseStts(detail::MovTable<std::pair<uint32_t, uint32_t>>& dst, uint64_t end) { return table(dst, end, 8); }
    bool parseStsc(detail::MovTable<std::pair<uint32_t, uint32_t>>& dst, uint64_t end) { return table(dst, end, 12); }
    bool parseStco(detail::MovTable<uint64_t>& dst, uint64_t end) { return table(dst, end, 4); }
    bool parseCo64(detail::MovTable<uint64_t>& dst, uint64_t end) { return table(dst, end, 8); }
    bool parseStsz(SampleSizes& dst, uint64_t atomEnd) {
        file_.seekg(4, std::ios::cur);
        dst.constantSize = readU32();
        dst.count = readU32();
        uint64_t pos = 0;
        if (!position(pos) || pos > atomEnd) return false;
        // Bound (security): size counts must fit the atom before paging; resident pages stay below 1,000,000 entries and 64 MiB. Overflow rejects the track via false and is counted.
        if (!dst.constantSize && dst.count > (atomEnd - pos) / 4) return false;
        return dst.constantSize || dst.sizes.open(dst.count ? tableSource() : nullptr, pos, dst.count, 4);
    }

    bool buildSamples(MovTrack& track, const SampleSizes& sizes,
                      const detail::MovTable<uint64_t>& offsets,
                      const detail::MovTable<std::pair<uint32_t, uint32_t>>& chunks,
                      const detail::MovTable<std::pair<uint32_t, uint32_t>>& times) {
        uint64_t limit = sizes.count;
        if (!times.empty()) {
            uint64_t timed = 0;
            for (size_t i = 0; i < times.size(); ++i) {
                std::pair<uint32_t, uint32_t> timing;
                if (!times.read(i, timing)) return false;
                timed += timing.first;
            }
            limit = std::min(limit, timed);
        }
        const uint32_t pcmBytes = track.isAudio() && sizes.constantSize ? track.getPcmFrameBytes() : 0;
        track.pcmChunked = pcmBytes != 0;
        // Bound (security): checkpoint growth must fit the remaining metadata and resident-entry budgets before allocation. Overflow rejects the file and is counted.
        constexpr uint64_t metadataLimit = 64u * 1024 * 1024, entryLimit = 1000000;
        const uint64_t overhead = 256u * 1024 + 3 * sizeof(MovTrack);
        if (metadataBytes_ + overhead >= metadataLimit || residentEntries_ + 1280 >= entryLimit)
            return reject("MOV metadata budget exhausted");
        if (!track.samples.open(sizes.sizes, sizes.constantSize, limit, offsets, chunks, pcmBytes,
                                fileSize_, metadataLimit - metadataBytes_ - overhead,
                                entryLimit - residentEntries_ - 1280)) return false;
        track.pcmFrameCount = pcmBytes ? track.samples.pcmFrames() : 0;
        return true;
    }
};

} // namespace tcx::hap
