// Internal C/C++ size conversion shared by native creation and Window sizing.
#ifndef SOKOL_WINDOW_SIZE_INCLUDED
#define SOKOL_WINDOW_SIZE_INCLUDED
#include <float.h>

// A positive pixel request overrides the logical extent. Zero keeps native
// default sizing intact. Invalid scales have the same unit fallback as setSize.
static inline double _sapp_tc_window_points(double logical_extent, int pixel_extent, float scale) {
    const float factor = scale > 0.0f && scale <= FLT_MAX ? scale : 1.0f;
    return pixel_extent > 0 ? (double)pixel_extent / factor : logical_extent;
}
#endif
