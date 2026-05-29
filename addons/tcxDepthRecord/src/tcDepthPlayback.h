#pragma once

// =============================================================================
// tcDepthPlayback.h - replay a .tcdc recording AS a DepthCamera
// =============================================================================
//
// PlaybackDepthCamera IS a DepthCamera, so an app plays a recording through the
// exact same interface it uses for live hardware - just swap the constructor:
//
//   shared_ptr<DepthCamera> cam = make_shared<PlaybackDepthCamera>("recording.tcdc");
//   cam->enableDepth(); cam->enableColor();
//   cam->setup();
//   ...
//   cam->update();
//   if (cam->isFrameNew()) cam->toMesh({.colors = true}).draw();
//
// Every frame is an independent seek target (intra), so it loops/seeks cheaply.
//
// =============================================================================

#include "tcDepthRecordFormat.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace tcx {

using namespace tc;

class PlaybackDepthCamera : public DepthCamera {
public:
    explicit PlaybackDepthCamera(const std::string& path) : path_(path) {}

    int  getFrameCount() const { return static_cast<int>(index_.size()); }
    void setLoop(bool loop) { loop_ = loop; }
    DepthSensorType getSensorType() const override {
        return static_cast<DepthSensorType>(header_.sensorType);
    }

protected:
    bool openDevice() override {
        std::filesystem::path p(path_);
        std::string resolved = p.is_relative() ? getDataPath(path_) : path_;
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
            << "PlaybackDepthCamera: loaded " << index_.size() << " frames ("
            << header_.width << "x" << header_.height << ") from " << resolved;
        return true;
    }

    void closeDevice() override {
        if (file_.is_open()) file_.close();
    }

    StreamFreshness captureInto(DepthFrame& dst) override {
        StreamFreshness fresh;
        if (index_.empty()) return fresh;
        if (cursor_ >= index_.size()) {
            if (!loop_) return fresh;
            cursor_ = 0;
        }
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(index_[cursor_].offset));
        if (!tcd_detail::readFrame(file_, header_, dst, scratch_)) return fresh;
        ++cursor_;
        fresh.depth = !dst.depth.empty();
        fresh.color = dst.hasColor();
        return fresh;
    }

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

} // namespace tcx
