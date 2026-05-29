#pragma once

// =============================================================================
// tcDepthRecorder.h - record any DepthCamera to a .tcdc file
// =============================================================================
//
//   DepthRecorder rec;
//   rec.start("recording.tcdc");
//   ...
//   cam->update();
//   if (cam->isFrameNew()) rec.record(*cam);   // appends currentFrame()
//   ...
//   rec.stop();                                 // writes the index + finalizes
//
// Camera-agnostic: it only reads cam.currentFrame(), so it records Orbbec,
// Kinect, the SyntheticDepthCamera, anything. Each frame is compressed
// independently (depth = hi/lo + LZ4, color = LZ4), so the file is seekable.
//
// =============================================================================

#include "tcDepthRecordFormat.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace tcx {

using namespace tc;

class DepthRecorder {
public:
    // Open `path` for writing (relative paths resolve via getDataPath). Returns
    // false if the file can't be opened.
    bool start(const std::string& path,
               DepthCodecId depthCodec = DepthCodecId::HiloLZ4,
               ColorCodecId colorCodec = ColorCodecId::LZ4) {
        std::filesystem::path p(path);
        std::string resolved = p.is_relative() ? getDataPath(path) : path;
        std::filesystem::path rp(resolved);
        if (rp.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(rp.parent_path(), ec);
        }
        file_.open(rp, std::ios::binary | std::ios::trunc);
        if (!file_) {
            logError("tcxDepthRecord") << "DepthRecorder: cannot open " << resolved;
            return false;
        }
        depthCodec_ = depthCodec;
        colorCodec_ = colorCodec;
        headerWritten_ = false;
        offsets_.clear();
        timestamps_.clear();
        return true;
    }

    // Append the camera's current frame.
    void record(const DepthCamera& cam) {
        if (!file_.is_open()) return;
        const DepthFrame& f = cam.currentFrame();

        // Which streams does THIS frame carry.
        std::uint8_t present = 0;
        if (!f.depth.empty())  present |= STREAM_DEPTH;
        if (f.hasColor())      present |= STREAM_COLOR;
        if (present == 0) return;  // nothing to record

        if (!headerWritten_) {
            TcdcHeader h = tcd_detail::makeHeader(f, present, depthCodec_,
                                                  colorCodec_, cam.getSensorType());
            tcd_detail::wr(file_, h);
            headerWritten_ = true;
        }

        offsets_.push_back(static_cast<std::uint64_t>(file_.tellp()));
        timestamps_.push_back(f.timestamp);
        tcd_detail::writeFrame(file_, f, present, depthCodec_, colorCodec_,
                               scratch_, comp_);
    }

    // Write the frame index and patch the header (frameCount, indexOffset).
    void stop() {
        if (!file_.is_open()) return;
        if (headerWritten_) {
            const std::uint64_t indexOffset = static_cast<std::uint64_t>(file_.tellp());
            for (std::size_t i = 0; i < offsets_.size(); ++i) {
                tcd_detail::wr(file_, timestamps_[i]);
                tcd_detail::wr(file_, offsets_[i]);
            }
            file_.seekp(offsetof(TcdcHeader, frameCount));
            tcd_detail::wr<std::uint32_t>(file_, static_cast<std::uint32_t>(offsets_.size()));
            file_.seekp(offsetof(TcdcHeader, indexOffset));
            tcd_detail::wr<std::uint64_t>(file_, indexOffset);
        }
        file_.close();
    }

    bool isRecording() const { return file_.is_open(); }
    int  getFrameCount() const { return static_cast<int>(offsets_.size()); }

private:
    std::ofstream file_;
    DepthCodecId depthCodec_ = DepthCodecId::HiloLZ4;
    ColorCodecId colorCodec_ = ColorCodecId::LZ4;
    bool headerWritten_ = false;
    std::vector<std::uint64_t> offsets_;
    std::vector<double> timestamps_;
    std::vector<std::uint8_t> scratch_;
    std::vector<std::uint8_t> comp_;
};

} // namespace tcx
