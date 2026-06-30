#pragma once

// =============================================================================
// tcxDepthRecorder.h - record any DepthCamera to a .tcdc file
// =============================================================================
//
//   DepthRecorder rec;
//   rec.start("clip.tcdc");                 // default: depth + color
//   rec.start("clip.tcdc", REC_DEPTH);      // depth only
//   ...
//   cam->update();
//   if (cam->isFrameNew()) rec.record(*cam);
//   rec.stop();
//
// Camera-agnostic (reads cam.currentFrame()). Each frame is a sequence of TLV
// blocks, so it is seekable and forward-compatible. Addons extend it: subclass
// and override writeExtraBlocks() to append custom blocks (e.g. body tracking)
// via writeBlock() - those types are recorded in the file's stream manifest so
// any reader knows they exist (and an official player skips them).
//
// =============================================================================

#include "tcxDepthRecordFormat.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tcx::depthrecord {

using namespace tc;

// Which official streams to record (bitmask). Default REC_ALL.
enum RecordStream : std::uint32_t {
    REC_DEPTH = 1,
    REC_COLOR = 2,
    REC_ALL   = REC_DEPTH | REC_COLOR,
};

class DepthRecorder {
public:
    bool start(const std::string& path,
               std::uint32_t streams = REC_ALL,
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
        streams_ = streams;
        depthCodec_ = depthCodec;
        colorCodec_ = colorCodec;
        headerWritten_ = false;
        offsets_.clear();
        timestamps_.clear();
        typeCount_ = 0;
        return true;
    }

    void record(const DepthCamera& cam) {
        if (!file_.is_open()) return;
        const DepthFrame& f = cam.currentFrame();

        if (!headerWritten_) {
            TcdcHeader h = tcd_detail::makeHeader(f, depthCodec_, colorCodec_,
                                                  cam.getSensorType());
            tcd_detail::wr(file_, h);
            headerWritten_ = true;
        }

        offsets_.push_back(static_cast<std::uint64_t>(file_.tellp()));
        timestamps_.push_back(f.timestamp);
        tcd_detail::wr(file_, f.timestamp);

        if ((streams_ & REC_DEPTH) && !f.depth.empty()) {
            tcd_detail::writeDepthBlock(file_, f, depthCodec_, scratch_, comp_);
            noteType(BLOCK_DEPTH);
        }
        if ((streams_ & REC_COLOR) && f.hasColor()) {
            tcd_detail::writeColorBlock(file_, f, colorCodec_, comp_);
            noteType(BLOCK_COLOR);
        }
        writeExtraBlocks(cam);   // addon hook (appends custom TLV blocks)
    }

    void stop() {
        if (!file_.is_open()) return;
        if (headerWritten_) {
            const std::uint64_t indexOffset = static_cast<std::uint64_t>(file_.tellp());
            for (std::size_t i = 0; i < offsets_.size(); ++i) {
                tcd_detail::wr(file_, timestamps_[i]);
                tcd_detail::wr(file_, offsets_[i]);
            }
            // patch manifest + frameCount + indexOffset
            file_.seekp(offsetof(TcdcHeader, streamTypeCount));
            tcd_detail::wr<std::uint8_t>(file_, typeCount_);
            file_.write(reinterpret_cast<const char*>(types_), TCDC_MAX_STREAM_TYPES);
            file_.seekp(offsetof(TcdcHeader, frameCount));
            tcd_detail::wr<std::uint32_t>(file_, static_cast<std::uint32_t>(offsets_.size()));
            file_.seekp(offsetof(TcdcHeader, indexOffset));
            tcd_detail::wr<std::uint64_t>(file_, indexOffset);
        }
        file_.close();
    }

    bool isRecording() const { return file_.is_open(); }
    int  getFrameCount() const { return static_cast<int>(offsets_.size()); }

protected:
    // Override to append custom TLV blocks per frame (call writeBlock()).
    virtual void writeExtraBlocks(const DepthCamera& /*cam*/) {}

    // Write one custom block and record its type in the manifest.
    void writeBlock(std::uint8_t type, const void* data, std::uint32_t len) {
        if (!file_.is_open()) return;
        tcd_detail::wr<std::uint8_t>(file_, type);
        tcd_detail::wr<std::uint32_t>(file_, len);
        if (len) file_.write(reinterpret_cast<const char*>(data), len);
        noteType(type);
    }

private:
    void noteType(std::uint8_t t) {
        for (std::uint8_t i = 0; i < typeCount_; ++i) if (types_[i] == t) return;
        if (typeCount_ < TCDC_MAX_STREAM_TYPES) types_[typeCount_++] = t;
    }

    std::ofstream file_;
    std::uint32_t streams_ = REC_ALL;
    DepthCodecId depthCodec_ = DepthCodecId::HiloLZ4;
    ColorCodecId colorCodec_ = ColorCodecId::LZ4;
    bool headerWritten_ = false;
    std::vector<std::uint64_t> offsets_;
    std::vector<double> timestamps_;
    std::vector<std::uint8_t> scratch_, comp_;
    std::uint8_t types_[TCDC_MAX_STREAM_TYPES] = {};
    std::uint8_t typeCount_ = 0;
};

} // namespace tcx::depthrecord

// Backward compatibility: silent flat-`tcx::` aliases (DEPRECATED).
namespace tcx { // deprecated: remove at v1.0.0
    using depthrecord::RecordStream;
    using depthrecord::REC_DEPTH;
    using depthrecord::REC_COLOR;
    using depthrecord::REC_ALL;
    using depthrecord::DepthRecorder;
}
