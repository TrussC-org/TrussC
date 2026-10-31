#pragma once

// =============================================================================
// tcxDepthPlayback.h - replay a .tcdc recording AS a DepthCamera
// =============================================================================
//
//   shared_ptr<DepthCamera> cam = make_shared<PlaybackDepthCamera>("clip.tcdc");
//   cam->enableDepth(); cam->enableColor();
//   cam->setup();
//   cam->update();
//   if (cam->isFrameNew()) cam->toMesh({.colors = true}).draw();
//
// Reads each frame's TLV blocks; depth/color are decoded, any unknown block type
// (e.g. an addon's body-tracking block) is skipped. The file's stream manifest
// lets you check what's inside up front: hasBlockType()/getBlockTypes()/
// hasUnknownBlocks(). Addons provide a PlaybackDevice subclass overriding
// readExtraBlock() to consume their own block types.
//
// A header whose frame size is negative or too large for an RGBA image is
// refused at open. Block sizes are checked before anything is allocated or
// decoded: every block must end within its frame, and depth/color sizes must
// match the frame dimensions. A block that fails a check is skipped (its stream
// is left empty and not marked new for that frame) and the first one is
// reported with a warning.
//
// =============================================================================

#include "tcxDepthRecordFormat.h"
#include <fstream>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace tcx::depthrecord {

using namespace tc;

class PlaybackDepthCamera : public DepthCamera {
public:
    explicit PlaybackDepthCamera(const std::string& path) : path_(path) {}

    int getFrameCount() const { return frameCount_; }
    void setLoop(bool loop) {
        loop_ = loop;
        if (auto device = playback_.lock()) device->loop_ = loop;
    }
    DepthSensorType getSensorType() const override { return sensorType_; }
    std::vector<std::uint8_t> getBlockTypes() const { return blockTypes_; }
    bool hasBlockType(std::uint8_t type) const {
        return std::find(blockTypes_.begin(), blockTypes_.end(), type) != blockTypes_.end();
    }
    bool hasUnknownBlocks() const { return unknownBlocks_; }

protected:
    // Custom stream decoders subclass this Device, never the camera handle.
    struct PlaybackDevice : Device {
        std::atomic<bool> loop_{true};
        int getFrameCount() const { return static_cast<int>(std::min<std::uint32_t>(header_.frameCount, INT_MAX)); }
        DepthSensorType getSensorType() const {
            return static_cast<DepthSensorType>(header_.sensorType);
        }

        // --- stream manifest (what the file contains, without scanning frames) ---
        std::vector<std::uint8_t> getBlockTypes() const {
            return {header_.streamTypes, header_.streamTypes + header_.streamTypeCount};
        }
        bool hasBlockType(std::uint8_t type) const {
            for (std::uint8_t i = 0; i < header_.streamTypeCount; ++i)
                if (header_.streamTypes[i] == type) return true;
            return false;
        }
        // True if the file carries a block type this build doesn't decode (depth/
        // color) - i.e. it's playable but something (e.g. an addon stream) is ignored.
        bool hasUnknownBlocks() const {
            for (std::uint8_t i = 0; i < header_.streamTypeCount; ++i) {
                const std::uint8_t t = header_.streamTypes[i];
                if (t != BLOCK_DEPTH && t != BLOCK_COLOR && t != BLOCK_INFRARED &&
                    !decodesBlockType(t)) {
                    return true;
                }
            }
            return false;
        }

        bool open(const std::string& path) try {
            // Start from an empty state, so a failed open leaves no manifest or
            // frame index from a file opened before. Clear the stream's error state
            // as well, so this open starts fresh after setup() closes it.
            if (file_.is_open()) file_.close();
            file_.clear();
            header_ = TcdcHeader{};
            skippedBlocks_ = 0;
            fileSize_ = 0;
            cursor_ = 0;
            std::vector<std::uint8_t>().swap(scratch_);
            warnedSkippedBlock_ = false;
            // getDataPath passes absolute paths through unchanged
            std::filesystem::path resolved = getDataPath(path);
            slots_ = {};
            file_.open(resolved, std::ios::binary);
            if (!file_) {
                logError("tcxDepthRecord") << "PlaybackDepthCamera: cannot open " << resolved;
                return false;
            }
            if (!tcd_detail::rd(file_, header_) ||
                std::memcmp(header_.magic, "TCDC", 4) != 0) {
                logError("tcxDepthRecord") << "PlaybackDepthCamera: not a .tcdc file: " << resolved;
                return refuseOpen();
            }
            if (header_.version != TCDC_VERSION) {
                logError("tcxDepthRecord") << "PlaybackDepthCamera: unsupported recording version";
                return refuseOpen();
            }
            if (header_.streamTypeCount > TCDC_MAX_STREAM_TYPES) {
                logError("tcxDepthRecord")
                    << "PlaybackDepthCamera: the stream manifest lists "
                    << static_cast<int>(header_.streamTypeCount) << " block types, more than the "
                    << TCDC_MAX_STREAM_TYPES << " it can hold: " << resolved;
                return refuseOpen();
            }
            // Every depth plane has width x height samples, drawn as an RGBA image
            // with int sizes and indices. DepthRecorder copies the first frame's
            // size, and a DepthFrame starts at 0x0, so a 0x0 header is kept (the
            // file plays without depth).
            if (header_.width < 0 || header_.height < 0) {
                logError("tcxDepthRecord")
                    << "PlaybackDepthCamera: the frame size " << header_.width << "x"
                    << header_.height << " is out of range: " << resolved;
                return refuseOpen();
            }
            // Frames are read only up to the end of the file.
            file_.seekg(0, std::ios::end);
            const std::streamoff fileEnd = file_.tellg();
            fileSize_ = fileEnd > 0 ? static_cast<std::uint64_t>(fileEnd) : 0;
            // Bound (security): index count times 16 must fit the remaining file before reading; no full index is allocated (under 64 MiB / 1,000,000 resident entries). Overflow refuses the file with an Error.
            if (header_.frameCount && (header_.indexOffset < sizeof(TcdcHeader) ||
                header_.indexOffset > fileSize_ || header_.frameCount > (fileSize_ - header_.indexOffset) / 16)) {
                logError("tcxDepthRecord") << "PlaybackDepthCamera: index does not fit the file";
                return refuseOpen();
            }
            Entry previous;
            for (std::uint32_t i = 0; i < header_.frameCount; ++i) {
                Entry entry;
                if (!readEntry(i, entry) || entry.offset < sizeof(TcdcHeader) ||
                    entry.offset > header_.indexOffset || header_.indexOffset - entry.offset < sizeof(double) ||
                    (i && (entry.offset <= previous.offset || entry.offset - previous.offset < sizeof(double)))) {
                    logError("tcxDepthRecord") << "PlaybackDepthCamera: invalid frame index";
                    return refuseOpen();
                }
                previous = entry;
            }
            logNotice("tcxDepthRecord")
                << "PlaybackDepthCamera: " << header_.frameCount << " frames ("
                << header_.width << "x" << header_.height << "), "
                << static_cast<int>(header_.streamTypeCount) << " stream type(s)"
                << (hasUnknownBlocks() ? " [has unknown stream(s)]" : "");
            return true;
        } catch (const std::exception&) {
            logError("tcxDepthRecord") << "PlaybackDepthCamera: open allocation failed";
            return refuseOpen();
        }

        StreamFreshness captureInto(DepthFrame& dst) override {
            StreamFreshness fresh;
            if (!header_.frameCount) return fresh;
            if (cursor_ >= header_.frameCount) {
                if (!loop_) return fresh;
                cursor_ = 0;
            }
            Slot* slot = nullptr;
            for (auto& candidate : slots_) if (candidate.frame == &dst) slot = &candidate;
            if (!slot) for (auto& candidate : slots_) if (!candidate.frame) { slot = &candidate; break; }
            if (!slot) {
                reportSkippedBlock(0, "more than three live playback slots");
                return fresh;
            }
            slot->frame = &dst;
            Entry current, following;
            if (!readEntry(cursor_, current) ||
                (cursor_ + 1 < header_.frameCount &&
                 (!readEntry(cursor_ + 1, following) || following.offset <= current.offset || following.offset - current.offset < sizeof(double)))) {
                reportSkippedBlock(0, "index read failed");
                ++cursor_;
                return fresh;
            }
            const std::uint64_t frameEnd = std::min(cursor_ + 1 < header_.frameCount ? following.offset : header_.indexOffset, fileSize_);
            file_.clear();
            file_.seekg(static_cast<std::streamoff>(current.offset));

            tcd_detail::applyHeader(header_, dst);
            std::vector<Vec3>().swap(dst.world);
            const std::uint64_t depthSamples = static_cast<std::uint64_t>(header_.width) * header_.height;
            if (dst.depth.capacity() != depthSamples) std::vector<std::uint16_t>().swap(dst.depth);
            else dst.depth.clear();
            if (dst.color.isAllocated()) dst.color = Pixels{};

            double ts = 0.0;
            tcd_detail::rd(file_, ts);
            dst.timestamp = ts;

            // TLV block loop until the end of the frame (the next frame's offset).
            // Each block must end within the frame; the one that doesn't ends the
            // frame, since where the next block starts can't be known.
            while (true) {
                const std::streamoff at = file_.tellg();
                if (at < 0 || static_cast<std::uint64_t>(at) >= frameEnd) break;
                std::uint8_t type = 0; std::uint32_t len = 0;
                if (!tcd_detail::rd(file_, type) || !tcd_detail::rd(file_, len)) break;
                const std::uint64_t payload = static_cast<std::uint64_t>(at) + 5;
                if (payload + len > frameEnd) {
                    reportSkippedBlock(type, "block runs past the end of its frame");
                    break;
                }
                const std::uint64_t room = frameEnd - payload;
                std::uint64_t next = payload + len;
                if (type == BLOCK_DEPTH || type == BLOCK_COLOR) {
                    std::uint64_t used = 0;
                    const char* why = nullptr;
                    const bool ok = (type == BLOCK_DEPTH)
                        ? tcd_detail::parseDepthPayload(file_, header_, len, room, dst, scratch_, used, why)
                        : tcd_detail::parseColorPayload(file_, header_, len, room, dst, scratch_, used, why);
                    if (ok) {
                        if (type == BLOCK_DEPTH) fresh.depth = true;
                        else fresh.color = true;
                    } else {
                        // The parser left the stream empty, so an earlier block of
                        // this type in the frame no longer counts.
                        if (type == BLOCK_DEPTH) fresh.depth = false;
                        else fresh.color = false;
                        reportSkippedBlock(type, why);
                    }
                    // Skip unknown trailing fields, while preserving the legacy
                    // color length that is four bytes short of its known payload.
                    if (used == 0) break;
                    next = payload + std::max<std::uint64_t>(len, used);
                } else if (len > tcd_detail::playbackMetadataBytes) {
                    reportSkippedBlock(type, "custom block exceeds 64 MiB", len, tcd_detail::playbackMetadataBytes);
                } else {
                    // Preserve the hook for subclasses that consume a type
                    // without advertising it through decodesBlockType().
                    // Bound (security): custom block metadata is limited to 64 MiB before copy. Overflow skips the whole block and is counted.
                    if (scratch_.capacity() != len) std::vector<std::uint8_t>().swap(scratch_);
                    if (!tcd_detail::playbackResize(scratch_, len)) {
                        logError("tcxDepthRecord") << "PlaybackDepthCamera: custom block allocation failed";
                        break;
                    }
                    if (len && !file_.read(reinterpret_cast<char*>(scratch_.data()), len)) {
                        reportSkippedBlock(type, "block is cut off");
                        break;
                    }
                    readExtraBlock(type, scratch_.data(), len, ts);
                }
                // The next block starts right after this one, however much of it
                // was read.
                file_.clear();
                file_.seekg(static_cast<std::streamoff>(next));
            }
            ++cursor_;
            return fresh;
        }

        // Override to consume a custom block type. Return true if handled. Also
        // override decodesBlockType() to report it as known (so hasUnknownBlocks()
        // doesn't flag it). Default: ignore (skipped).
        virtual bool readExtraBlock(std::uint8_t /*type*/, const std::uint8_t* /*data*/,
                                    std::uint32_t /*len*/, double /*timestamp*/) {
            return false;
        }
        virtual bool decodesBlockType(std::uint8_t /*type*/) const { return false; }

    private:
        struct Entry { double ts = 0.0; std::uint64_t offset = 0; };

        // A refused open keeps no manifest and doesn't hold the file open.
        bool refuseOpen() {
            header_ = TcdcHeader{};
            file_.close();
            file_.clear();
            return false;
        }

        // Warns about the first skipped block since open; a damaged file would
        // otherwise log on every frame of every loop.
        void reportSkippedBlock(std::uint8_t type, const char* why, std::uint64_t needed = 0, std::uint64_t capacity = 0) {
            ++skippedBlocks_;
            if (warnedSkippedBlock_) return;
            warnedSkippedBlock_ = true;
            const char* kind = type == BLOCK_DEPTH ? "depth"
                             : type == BLOCK_COLOR ? "color" : "custom";
            auto warning = logWarning("tcxDepthRecord");
            warning << "PlaybackDepthCamera: skipped a " << kind << " block in frame " << cursor_
                    << " (" << why << ")";
            if (capacity) warning << "; needed " << needed << " bytes, capacity " << capacity << " bytes";
            warning << "; later skipped blocks are not reported";
        }

        std::ifstream file_;
        TcdcHeader header_{};
        bool readEntry(std::size_t index, Entry& entry) {
            file_.clear();
            file_.seekg(static_cast<std::streamoff>(header_.indexOffset + std::uint64_t(index) * 16));
            return tcd_detail::rd(file_, entry.ts) && tcd_detail::rd(file_, entry.offset) &&
                entry.offset >= sizeof(TcdcHeader) && entry.offset <= header_.indexOffset &&
                header_.indexOffset - entry.offset >= sizeof(double);
        }
        std::uint64_t skippedBlocks_ = 0;
        struct Slot { const DepthFrame* frame = nullptr; };
        std::array<Slot, 3> slots_{};
        friend struct PlaybackBudgetTestAccess;
        std::size_t cursor_ = 0;
        std::vector<std::uint8_t> scratch_;
        std::uint64_t fileSize_ = 0;
        bool warnedSkippedBlock_ = false;
    };

    // Overrides may return a PlaybackDevice subclass with custom TLV hooks.
    virtual std::shared_ptr<PlaybackDevice> makePlaybackDevice() {
        return std::make_shared<PlaybackDevice>();
    }
    std::shared_ptr<Device> openDevice() override {
        auto device = makePlaybackDevice();
        device->loop_ = loop_;
        const bool opened = device->open(path_);
        playback_ = device;
        frameCount_ = device->getFrameCount();
        sensorType_ = device->getSensorType();
        blockTypes_ = device->getBlockTypes();
        unknownBlocks_ = device->hasUnknownBlocks();
        return opened ? device : nullptr;
    }

private:
    std::string path_;
    bool loop_ = true;
    // Immutable metadata survives close(), without retaining an open file.
    int frameCount_ = 0;
    DepthSensorType sensorType_ = DepthSensorType::Unknown;
    std::vector<std::uint8_t> blockTypes_;
    bool unknownBlocks_ = false;
    // The handle only observes the run's device; close() releases ownership.
    std::weak_ptr<PlaybackDevice> playback_;

};

} // namespace tcx::depthrecord

// Backward compatibility: silent flat-`tcx::` alias (DEPRECATED).
namespace tcx { // deprecated: remove at v1.0.0
    using depthrecord::PlaybackDepthCamera;
}
