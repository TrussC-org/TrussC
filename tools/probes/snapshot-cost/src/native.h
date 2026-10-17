#pragma once
#include <sokol/sokol_gfx.h>
#include <vector>
struct GpuSample { int block; bool copy; double sceneMs, copyMs, totalMs; };
struct ProbeNative;
ProbeNative* probeCreate();
void probeBegin(ProbeNative*, int block, bool copy, bool measured);
void probeEnd(ProbeNative*, const sg_swapchain&);
void probePoll(ProbeNative*, std::vector<GpuSample>&, bool drain = false);
void probeDestroy(ProbeNative*);
