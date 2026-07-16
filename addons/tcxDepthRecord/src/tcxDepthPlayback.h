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
// =============================================================================

#include "tcxDepthRecordFormat.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace tcx::depthrecord {

using namespace tc;

class PlaybackDepthCamera : public DepthCamera {
public:
    explicit PlaybackDepthCamera(const std::string& path) : path_(path) {}

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
            return false;
        }
        index_.clear();
        if (header_.indexOffset != 0 && header_.frameCount > 0) {
            file_.seekg(static_cast<std::streamoff>(header_.indexOffset));
            for (std::uint32_t i = 0; i < header_.frameCount; ++i) {
                Entry e;
                if (!tcd_detail::rd(file_, e.ts) || !tcd_detail::rd(file_, e.offset)) break;
                index_.push_back(e);
            }
        }
        cursor_ = 0;
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
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(index_[cursor_].offset));

        tcd_detail::applyHeader(header_, dst);
        dst.world.clear();
        dst.depth.clear();
        if (dst.color.isAllocated()) dst.color = Pixels{};

        double ts = 0.0;
        tcd_detail::rd(file_, ts);
        dst.timestamp = ts;

        // TLV block loop until the next frame's offset.
        while (static_cast<std::uint64_t>(file_.tellg()) < nextOffset) {
            std::uint8_t type = 0; std::uint32_t len = 0;
            if (!tcd_detail::rd(file_, type) || !tcd_detail::rd(file_, len)) break;
            if (type == BLOCK_DEPTH) {
                tcd_detail::parseDepthPayload(file_, header_, dst, scratch_);
                fresh.depth = true;
            } else if (type == BLOCK_COLOR) {
                tcd_detail::parseColorPayload(file_, header_, dst, scratch_);
                fresh.color = true;
            } else {
                // unknown / custom: read the payload and offer it to a subclass.
                std::vector<std::uint8_t> buf(len);
                if (len) file_.read(reinterpret_cast<char*>(buf.data()), len);
                readExtraBlock(type, buf.data(), len, ts);
            }
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

    std::string path_;
    std::ifstream file_;
    TcdcHeader header_{};
    std::vector<Entry> index_;
    std::size_t cursor_ = 0;
    bool loop_ = true;
    std::vector<std::uint8_t> scratch_;
};

} // namespace tcx::depthrecord

// Backward compatibility: silent flat-`tcx::` alias (DEPRECATED).
namespace tcx { // deprecated: remove at v1.0.0
    using depthrecord::PlaybackDepthCamera;
}
