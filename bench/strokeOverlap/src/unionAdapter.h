#pragma once
#include <TrussC.h>
namespace strokeOverlap {
struct UnionTiming { double clipperMs=0, earcutMs=0; };
bool unionAvailable();
// Output is a flat list of non-overlapping triangles, including hole handling.
bool unionStroke(const std::vector<tc::Vec2>& points, float width,
                 std::vector<tc::Vec2>& outTriangles, UnionTiming& timing);
}
