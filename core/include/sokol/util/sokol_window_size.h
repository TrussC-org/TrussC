// Internal C/C++ size conversion shared by native creation and Window sizing.
#ifndef SOKOL_WINDOW_SIZE_INCLUDED
#define SOKOL_WINDOW_SIZE_INCLUDED
#include <float.h>
#include <limits.h>
#include <stdbool.h>

// A positive pixel request overrides the logical extent. Zero keeps native
// default sizing intact. Invalid scales have the same unit fallback as setSize.
static inline double _sapp_tc_window_points(double logical_extent, int pixel_extent, float scale) {
    const float factor = scale > 0.0f && scale <= FLT_MAX ? scale : 1.0f;
    return pixel_extent > 0 ? (double)pixel_extent / factor : logical_extent;
}

// Startup has no prior size to retain when setSize would reject the request.
static inline bool _sapp_tc_window_pixels_representable(int width, int height, float scale) {
    // Bound (size API): pixel quotients must fit the int logical-size getters, as in setSize.
    return _sapp_tc_window_points(0, width, scale) <= (double)INT_MAX &&
           _sapp_tc_window_points(0, height, scale) <= (double)INT_MAX;
}
#endif
