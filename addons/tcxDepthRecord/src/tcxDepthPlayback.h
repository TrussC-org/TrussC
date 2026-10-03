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
// hasUnknownBlocks(). Addons subclass and override readExtraBlock() to consume
// their own block types.
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

#include <algorithm>
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

    ~PlaybackDepthCamera() override { close(); }

    int  getFrameCount() const { return static_cast<int>(index_.size()); }
    void setLoop(bool loop) { loop_ = loop; }
    DepthSensorType getSensorType() const override {
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

protected:
    bool openDevice() override {
        // Start from an empty state, so a failed open leaves no manifest or
        // frame index from a file opened before. Clear the stream's error state
        // as well, so this open starts fresh after setup() closes it.
        if (file_.is_open()) file_.close();
        file_.clear();
        header_ = TcdcHeader{};
        index_.clear();
        fileSize_ = 0;
        cursor_ = 0;
        warnedSkippedBlock_ = false;
        // getDataPath passes absolute paths through unchanged
        std::filesystem::path resolved = getDataPath(path_);
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
        const std::int64_t rgbaBytes = static_cast<std::int64_t>(header_.width) *
                                       static_cast<std::int64_t>(header_.height) * 4;
        if (header_.width < 0 || header_.height < 0 || rgbaBytes > INT_MAX) {
            logError("tcxDepthRecord")
                << "PlaybackDepthCamera: the frame size " << header_.width << "x"
                << header_.height << " is out of range: " << resolved;
            return refuseOpen();
        }
        // Frames are read only up to the end of the file.
        file_.seekg(0, std::ios::end);
        const std::streamoff fileEnd = file_.tellg();
        fileSize_ = fileEnd > 0 ? static_cast<std::uint64_t>(fileEnd) : 0;
        if (header_.indexOffset != 0 && header_.frameCount > 0) {
            file_.seekg(static_cast<std::streamoff>(header_.indexOffset));
            for (std::uint32_t i = 0; i < header_.frameCount; ++i) {
                Entry e;
                if (!tcd_detail::rd(file_, e.ts) || !tcd_detail::rd(file_, e.offset)) break;
                index_.push_back(e);
            }
        }
        logNotice("tcxDepthRecord")
            << "PlaybackDepthCamera: " << index_.size() << " frames ("
            << header_.width << "x" << header_.height << "), "
            << static_cast<int>(header_.streamTypeCount) << " stream type(s)"
            << (hasUnknownBlocks() ? " [has unknown stream(s)]" : "");
        return true;
    }

    void closeDevice() override { if (file_.is_open()) file_.close(); }

    StreamFreshness captureInto(DepthFrame& dst) override {
        StreamFreshness fresh;
        if (index_.empty()) return fresh;
        if (cursor_ >= index_.size()) {
            if (!loop_) return fresh;
            cursor_ = 0;
        }
        const std::uint64_t nextOffset =
            (cursor_ + 1 < index_.size()) ? index_[cursor_ + 1].offset
                                          : header_.indexOffset;
        const std::uint64_t frameEnd = std::min(nextOffset, fileSize_);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(index_[cursor_].offset));

        tcd_detail::applyHeader(header_, dst);
        dst.world.clear();
        dst.depth.clear();
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
                // These end where their own size fields say (see the parsers).
                if (used == 0) break;
                next = payload + used;
            } else {
                // unknown / custom: read the payload and offer it to a subclass.
                std::vector<std::uint8_t> buf(len);
                if (len && !file_.read(reinterpret_cast<char*>(buf.data()), len)) {
                    reportSkippedBlock(type, "block is cut off");
                    break;
                }
                readExtraBlock(type, buf.data(), len, ts);
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
    void reportSkippedBlock(std::uint8_t type, const char* why) {
        if (warnedSkippedBlock_) return;
        warnedSkippedBlock_ = true;
        const char* kind = type == BLOCK_DEPTH ? "depth"
                         : type == BLOCK_COLOR ? "color" : "custom";
        logWarning("tcxDepthRecord")
            << "PlaybackDepthCamera: skipped a " << kind << " block in frame " << cursor_
            << " (" << why << "); later skipped blocks are not reported";
    }

    std::string path_;
    std::ifstream file_;
    TcdcHeader header_{};
    std::vector<Entry> index_;
    std::size_t cursor_ = 0;
    bool loop_ = true;
    std::vector<std::uint8_t> scratch_;
    std::uint64_t fileSize_ = 0;
    bool warnedSkippedBlock_ = false;
};

} // namespace tcx::depthrecord

// Backward compatibility: silent flat-`tcx::` alias (DEPRECATED).
namespace tcx { // deprecated: remove at v1.0.0
    using depthrecord::PlaybackDepthCamera;
}
