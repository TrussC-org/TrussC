#include "unionAdapter.h"
#ifdef STROKE_OVERLAP_CLIPPER2
#include <clipper2/clipper.h>
#include <earcut/earcut.hpp>
#include <array>
#include <chrono>
#include <cmath>
#endif
namespace strokeOverlap {
#ifdef STROKE_OVERLAP_CLIPPER2
namespace {
using Clock = std::chrono::steady_clock;
// Integer grid: 0.001 px. Fixed arc tolerance: 0.01 px at both resolutions.
constexpr double scale = 1000.0;
using Point = std::array<double, 2>;
using Ring = std::vector<Point>;
void triangulate(const Clipper2Lib::PolyPath64& outer, std::vector<tc::Vec2>& output) {
    std::vector<Ring> polygon;
    auto addRing = [&](const Clipper2Lib::Path64& path) {
        Ring ring;
        for (auto p : path) ring.push_back({p.x / scale, p.y / scale});
        polygon.push_back(std::move(ring));
    };
    addRing(outer.Polygon());
    // Direct children are holes. Islands inside those holes are separate outers.
    for (const auto& hole : outer) addRing(hole->Polygon());
    auto indices = mapbox::earcut<uint32_t>(polygon);
    std::vector<Point> flat;
    for (const auto& ring : polygon) flat.insert(flat.end(), ring.begin(), ring.end());
    for (auto index : indices) output.emplace_back(float(flat[index][0]), float(flat[index][1]));
    for (const auto& hole : outer)
        for (const auto& island : *hole) triangulate(*island, output);
}
}
bool unionAvailable() { return true; }
bool unionStroke(const std::vector<tc::Vec2>& points, float width,
                 std::vector<tc::Vec2>& output, UnionTiming& timing) {
    auto start = Clock::now();
    output.clear();
    Clipper2Lib::Path64 path;
    path.reserve(points.size());
    for (auto p : points) path.emplace_back(std::llround(p.x * scale), std::llround(p.y * scale));
    Clipper2Lib::ClipperOffset offset(2.0, 0.01 * scale);
    offset.AddPath(path, Clipper2Lib::JoinType::Round, Clipper2Lib::EndType::Round);
    Clipper2Lib::PolyTree64 tree;
    offset.Execute(width * 0.5 * scale, tree);
    auto split = Clock::now();
    for (const auto& outer : tree) triangulate(*outer, output);
    auto end = Clock::now();
    timing.clipperMs += std::chrono::duration<double, std::milli>(split - start).count();
    timing.earcutMs += std::chrono::duration<double, std::milli>(end - split).count();
    return offset.ErrorCode() == 0;
}
#else
bool unionAvailable() { return false; }
bool unionStroke(const std::vector<tc::Vec2>&, float, std::vector<tc::Vec2>& output, UnionTiming&) {
    output.clear();
    return false;
}
#endif
}
