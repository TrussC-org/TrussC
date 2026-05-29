#pragma once

// =============================================================================
// tcxDepthRecord.h - record & replay depth recordings (.tcdc)
// =============================================================================
//
// Built on tcxDepthCamera's canonical DepthFrame, so it records ANY DepthCamera
// and replays the result through the same DepthCamera interface.
//
//   #include <tcxDepthRecord.h>
//   using namespace tcx;
//
//   // record
//   DepthRecorder rec; rec.start("clip.tcdc");
//   cam->update(); if (cam->isFrameNew()) rec.record(*cam);
//   rec.stop();
//
//   // replay (same interface as a live camera)
//   shared_ptr<DepthCamera> p = make_shared<PlaybackDepthCamera>("clip.tcdc");
//   p->enableDepth(); p->enableColor(); p->setup();
//   p->update(); p->toMesh({.colors = true}).draw();
//
// Compression goes through core's tc::compress (depth = hi/lo + LZ4, color =
// LZ4; both lossless). Codecs are tagged in the file, so QOI / JPEG-XS / zstd
// can be added later without a format change.
//
// Headers:
//   tcDepthRecordFormat.h - the .tcdc format + read/write helpers
//   tcDepthRecorder.h     - DepthRecorder (sink)
//   tcDepthPlayback.h     - PlaybackDepthCamera (source : DepthCamera)
//
// =============================================================================

#include "tcDepthRecordFormat.h"
#include "tcDepthRecorder.h"
#include "tcDepthPlayback.h"
