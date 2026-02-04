// =============================================================================
// tcPixels.cpp - Pixels implementation (requires TrussC.h for getDataPath)
// =============================================================================

#include "TrussC.h"

namespace trussc {

bool Pixels::save(const fs::path& path) const {
    if (!allocated_ || !data_ || format_ == PixelFormat::F32) return false;

    // Convert relative paths to data path
    fs::path savePath = path;
    if (path.is_relative()) {
        savePath = getDataPath(path.string());
    }

    auto ext = savePath.extension().string();
    auto pathStr = savePath.string();
    int result = 0;

    if (ext == ".png" || ext == ".PNG") {
        result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
    } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG") {
        result = stbi_write_jpg(pathStr.c_str(), width_, height_, channels_, data_, 90);
    } else if (ext == ".bmp" || ext == ".BMP") {
        result = stbi_write_bmp(pathStr.c_str(), width_, height_, channels_, data_);
    } else {
        // Default is PNG
        result = stbi_write_png(pathStr.c_str(), width_, height_, channels_, data_, width_ * channels_);
    }

    return result != 0;
}

} // namespace trussc
