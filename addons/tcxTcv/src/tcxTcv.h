#pragma once

// =============================================================================
// tcxTcv - TrussC Video Codec
// =============================================================================
// Custom video codec optimized for UI recordings, desktop capture, and
// motion graphics with many static regions.
//
// Features:
//   - BC7 texture compression (GPU-accelerated decoding)
//   - Block-based delta encoding (SKIP/SOLID/BC7)
//   - Multi-reference frames for loops and flashing
//   - Optional audio track support
//
// Usage:
//   #include <tcxTcv.h>
//
//   // Encoding
//   tcx::TcvEncoder encoder;
//   encoder.begin("output.tcv", width, height, fps);
//   encoder.addFrame(rgbaPixels);
//   encoder.end();
//
//   // Decoding
//   tcx::TcvPlayer player;
//   player.load("video.tcv");
//   player.play();
//   player.update();
//   player.draw(0, 0);
// =============================================================================

#include "tcTcvEncoder.h"
#include "tcTcvPlayer.h"
