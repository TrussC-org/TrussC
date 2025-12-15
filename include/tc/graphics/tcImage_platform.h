#pragma once

// =============================================================================
// tcImage_platform.h - Image クラスのプラットフォーム固有実装
// =============================================================================

namespace trussc {

// スクリーンキャプチャ（プラットフォーム固有）
// TODO: FBO を実装してそこからピクセルを読む方式に変更
inline bool Image::grabScreenPlatform(int x, int y, int w, int h) {
    // sokol_gfx には直接フレームバッファを読む API がない
    // FBO (オフスクリーンレンダリング) を実装するまでは未対応

    // 暫定: 灰色で埋める（デバッグ用）
    if (pixels_ && allocated_) {
        for (int py = 0; py < height_; py++) {
            for (int px = 0; px < width_; px++) {
                int index = (py * width_ + px) * channels_;
                pixels_[index] = 128;      // R
                pixels_[index + 1] = 128;  // G
                pixels_[index + 2] = 128;  // B
                if (channels_ == 4) {
                    pixels_[index + 3] = 255;  // A
                }
            }
        }
        updateTexture();
    }

    // 未実装を示す
    return false;
}

} // namespace trussc
