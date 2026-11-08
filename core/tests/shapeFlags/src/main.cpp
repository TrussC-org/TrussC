#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>

namespace {
enum class Builder { Shape, Stroke, Lines };

void begin(Builder builder) {
    switch (builder) {
        case Builder::Shape: tc::beginShape(); break;
        case Builder::Stroke: tc::beginStroke(); break;
        case Builder::Lines: tc::beginLines(); break;
    }
}
} // namespace

TC_CORE_TEST_MAIN() {
    int failures = 0;
    auto& state = tc::internal::currentWindowContext();
    for (auto previous : {Builder::Shape, Builder::Stroke, Builder::Lines}) {
        for (auto next : {Builder::Shape, Builder::Stroke, Builder::Lines}) {
            tc::beginLines();
            tc::endLines();
            begin(previous);
            tc::vertex(1, 2, 3);
            begin(next);
            tc::vertex(4, 5, 6);
            const bool flags = state.shapeStarted == (next == Builder::Shape) &&
                               state.strokeStarted == (next == Builder::Stroke) &&
                               state.linesStarted == (next == Builder::Lines);
            const bool routed = next == Builder::Shape ? state.shapeVertices.size() == 1 && state.shapeVertices[0] == tc::Vec3(4, 5, 6) :
                                next == Builder::Stroke ? state.strokeVertices.size() == 1 && state.strokeVertices[0].pos == tc::Vec3(4, 5, 6) :
                                                          state.linesVertices.size() == 1 && state.linesVertices[0].pos == tc::Vec3(4, 5, 6);
            std::printf("builder %d -> %d: %s\n", int(previous), int(next), flags && routed ? "PASS" : "FAIL");
            if (!flags || !routed) ++failures;
        }
    }
    tc::beginLines();
    tc::endLines();
    return failures ? 1 : 0;
}
