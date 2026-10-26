#include "tcxQuadWarp.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tcx::quadwarp {

namespace {
bool isConvex(const tc::Vec2 (&points)[4]) {
    double winding = 0;
    for (int i = 0; i < 4; ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % 4];
        const auto& c = points[(i + 2) % 4];
        if (!std::isfinite(a.x) || !std::isfinite(a.y)) return false;
        const double cross = (double(b.x) - a.x) * (double(c.y) - b.y) -
                             (double(b.y) - a.y) * (double(c.x) - b.x);
        if (cross == 0 || (i > 0 && (cross > 0) != (winding > 0))) return false;
        winding = cross;
    }
    return true;
}

// Keep projection and inversion in double precision: a small quad's translation
// can be much larger than its extent. The public rendering matrix stays intact.
struct Homography {
    double m[9];
};

Homography multiply(const Homography& a, const Homography& b) {
    Homography result{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                result.m[3 * r + c] += a.m[3 * r + k] * b.m[3 * k + c];
    return result;
}

bool project(const Homography& matrix, const tc::Vec2& point, tc::Vec2& output) {
    const auto& h = matrix.m;
    const double x = h[0] * point.x + h[1] * point.y + h[2];
    const double y = h[3] * point.x + h[4] * point.y + h[5];
    const double w = h[6] * point.x + h[7] * point.y + h[8];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || w <= 0) return false;
    const double px = x / w, py = y / w;
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(px) || !std::isfinite(py) || std::abs(px) > limit || std::abs(py) > limit) return false;
    output = tc::Vec2(static_cast<float>(px), static_cast<float>(py));
    return true;
}

bool validHomography(const tc::Vec2 (&src)[4], const tc::Vec2 (&dst)[4],
                     const tc::Mat4& matrix, Homography& homography) {
    if (!isConvex(src) || !isConvex(dst)) return false;
    for (float value : matrix.m) if (!std::isfinite(value)) return false;
    homography = {{matrix.m[0], matrix.m[1], matrix.m[3],
                   matrix.m[4], matrix.m[5], matrix.m[7],
                   matrix.m[12], matrix.m[13], matrix.m[15]}};
    // getHomography() falls back to identity when its solver fails. Verify
    // corner correspondence so that fallback cannot masquerade as a valid warp.
    const tc::Mat4 identity;
    const bool isIdentity = std::equal(matrix.m, matrix.m + 16, identity.m);
    float scale = 1;
    for (const auto& point : dst) {
        scale = std::max(scale, std::max(std::abs(point.x), std::abs(point.y)));
    }
    for (int i = 0; i < 4; ++i) {
        tc::Vec2 corner;
        if ((isIdentity && src[i] != dst[i]) || !project(homography, src[i], corner) ||
            std::abs(corner.x - dst[i].x) > 1e-4f * scale ||
            std::abs(corner.y - dst[i].y) > 1e-4f * scale) return false;
    }
    return true;
}

bool invertHomography(const Homography& matrix, const tc::Vec2 (&src)[4],
                      const tc::Vec2 (&dst)[4], Homography& inverse) {
    // Express both quads relative to their own first corner and extent before
    // testing singularity. Absolute screen offsets must not affect the cutoff.
    auto extent = [](const tc::Vec2 (&points)[4]) {
        double scale = 0;
        for (const auto& p : points)
            scale = std::max(scale, std::max(std::abs(double(p.x) - points[0].x),
                                            std::abs(double(p.y) - points[0].y)));
        return scale;
    };
    const double s = extent(src), d = extent(dst);
    if (!(s > 0) || !(d > 0)) return false;
    const Homography sourceFrame{{s, 0, src[0].x, 0, s, src[0].y, 0, 0, 1}};
    const Homography targetFrame{{1 / d, 0, -double(dst[0].x) / d,
                                  0, 1 / d, -double(dst[0].y) / d, 0, 0, 1}};
    const auto normalized = multiply(targetFrame, multiply(matrix, sourceFrame));
    const auto& h = normalized.m;
    Homography adjugate{{h[4]*h[8] - h[5]*h[7], h[2]*h[7] - h[1]*h[8], h[1]*h[5] - h[2]*h[4],
                          h[5]*h[6] - h[3]*h[8], h[0]*h[8] - h[2]*h[6], h[2]*h[3] - h[0]*h[5],
                          h[3]*h[7] - h[4]*h[6], h[1]*h[6] - h[0]*h[7], h[0]*h[4] - h[1]*h[3]}};
    const double determinant = h[0]*adjugate.m[0] + h[1]*adjugate.m[3] + h[2]*adjugate.m[6];
    double columnProduct = 1;
    for (int c = 0; c < 3; ++c)
        columnProduct *= std::hypot(h[c], h[3 + c], h[6 + c]);
    if (!std::isfinite(determinant) || !std::isfinite(columnProduct) ||
        std::abs(determinant) <= 64 * std::numeric_limits<double>::epsilon() * columnProduct) return false;
    for (double& value : adjugate.m) value /= determinant;
    inverse = multiply(sourceFrame, multiply(adjugate, targetFrame));
    for (double value : inverse.m) if (!std::isfinite(value)) return false;
    return true;
}
} // namespace

QuadWarp::QuadWarp() {
    // Default points
    setSourceRect(tc::Rect(0, 0, 100, 100));
    setTargetRect(tc::Rect(0, 0, 100, 100));
}

void QuadWarp::setup() {
    setInputEnabled(true);
}

void QuadWarp::setSourceRect(const tc::Rect& r) {
    srcPoints[0].set(r.x, r.y);
    srcPoints[1].set(r.x + r.width, r.y);
    srcPoints[2].set(r.x + r.width, r.y + r.height);
    srcPoints[3].set(r.x, r.y + r.height);
}

void QuadWarp::setTargetRect(const tc::Rect& r) {
    dstPoints[0].set(r.x, r.y);
    dstPoints[1].set(r.x + r.width, r.y);
    dstPoints[2].set(r.x + r.width, r.y + r.height);
    dstPoints[3].set(r.x, r.y + r.height);
}

void QuadWarp::update() {
    // Matrix is calculated on demand in getMatrix()
}

void QuadWarp::draw() {
    // Deprecated or empty? 
    // For compatibility, let's call drawUI() if enabled
    if (inputEnabled_) {
        drawUI();
    }
}

void QuadWarp::drawUI() {
    tc::pushStyle();
    
    // Draw outline
    tc::setColor(uiColor_);
    tc::noFill();
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        tc::drawLine(dstPoints[i].x, dstPoints[i].y, dstPoints[next].x, dstPoints[next].y);
    }

    // Draw anchors
    for (int i = 0; i < 4; i++) {
        if (i == selectedIndex_) {
            tc::setColor(uiHoverColor_); // Selected uses hover color (filled)
            tc::fill();
        } else if (i == hoverIndex_) {
            tc::setColor(uiHoverColor_); // Hover uses hover color (outline)
            tc::noFill();
        } else {
            tc::setColor(uiColor_);      // Normal uses UI color (outline)
            tc::noFill();
        }
        
        tc::drawRect(dstPoints[i].x - anchorSize_ / 2, 
                     dstPoints[i].y - anchorSize_ / 2, 
                     anchorSize_, anchorSize_);
        
        // Label
        tc::setColor(1.0f);
        tc::fill(); // Label always filled
        tc::drawBitmapString(std::to_string(i + 1), dstPoints[i].x + 8, dstPoints[i].y + 8);
    }

    tc::popStyle();
}

tc::Mat4 QuadWarp::getMatrix() const {
    // Calculate 3x3 homography matrix
    tc::Mat3 h = tc::Mat3::getHomography(srcPoints, dstPoints);

    // Convert to 4x4 (row-major, multMatrix handles column-major conversion)
    return tc::Mat4::fromHomography(h);
}

bool QuadWarp::screenToSource(const tc::Vec2& screen, tc::Vec2& source) const {
    const tc::Mat4 matrix = getMatrix();
    Homography homography, inverse;
    if (!validHomography(srcPoints, dstPoints, matrix, homography) ||
        !invertHomography(homography, srcPoints, dstPoints, inverse)) return false;
    return project(inverse, screen, source);
}

bool QuadWarp::sourceToScreen(const tc::Vec2& source, tc::Vec2& screen) const {
    const tc::Mat4 matrix = getMatrix();
    Homography homography;
    if (!validHomography(srcPoints, dstPoints, matrix, homography)) return false;
    return project(homography, source, screen);
}

void QuadWarp::setInputEnabled(bool enabled) {
    if (inputEnabled_ == enabled) return;
    
    inputEnabled_ = enabled;
    
    if (enabled) {
        mouseMoveListener_ = tc::events().mouseMoved.listen(this, &QuadWarp::onMouseMoved, tc::EventPriority::BeforeApp);
        mousePressListener_ = tc::events().mousePressed.listen(this, &QuadWarp::onMousePressed, tc::EventPriority::BeforeApp);
        mouseDragListener_ = tc::events().mouseDragged.listen(this, &QuadWarp::onMouseDragged, tc::EventPriority::BeforeApp);
        mouseReleaseListener_ = tc::events().mouseReleased.listen(this, &QuadWarp::onMouseReleased, tc::EventPriority::BeforeApp);
        keyPressListener_ = tc::events().keyPressed.listen(this, &QuadWarp::onKeyPressed, tc::EventPriority::BeforeApp);
    } else {
        mouseMoveListener_.disconnect();
        mousePressListener_.disconnect();
        mouseDragListener_.disconnect();
        mouseReleaseListener_.disconnect();
        keyPressListener_.disconnect();
        selectedIndex_ = -1;
        hoverIndex_ = -1;
    }
}

void QuadWarp::onMouseMoved(tc::MouseMoveEventArgs& e) {
    hoverIndex_ = -1;
    float minDist = anchorSize_;

    for (int i = 0; i < 4; i++) {
        float d = e.pos.distance(dstPoints[i]);
        if (d < minDist) {
            minDist = d;
            hoverIndex_ = i;
        }
    }
}

void QuadWarp::onMousePressed(tc::MouseEventArgs& e) {
    // Keep selection unless clicked outside
    int newSelection = -1;
    
    // Use hover index if valid
    if (hoverIndex_ != -1) {
        newSelection = hoverIndex_;
    } else {
        float minDist = anchorSize_;
        for (int i = 0; i < 4; i++) {
            float d = e.pos.distance(dstPoints[i]);
            if (d < minDist) {
                minDist = d;
                newSelection = i;
            }
        }
    }

    // Only deselect if clicked somewhere else (and not on a point)
    // Actually, standard behavior is: click background -> deselect
    selectedIndex_ = newSelection;
}

void QuadWarp::onMouseDragged(tc::MouseDragEventArgs& e) {
    if (selectedIndex_ == -1) return;

    dstPoints[selectedIndex_].set(e.pos.x, e.pos.y);
}

void QuadWarp::onMouseReleased(tc::MouseEventArgs& e) {
    // Do not reset selection on release
}

void QuadWarp::onKeyPressed(tc::KeyEventArgs& e) {
    if (selectedIndex_ == -1) return;

    if (e.key == tc::KEY_LEFT)  dstPoints[selectedIndex_].x -= nudgeAmount_;
    if (e.key == tc::KEY_RIGHT) dstPoints[selectedIndex_].x += nudgeAmount_;
    if (e.key == tc::KEY_UP)    dstPoints[selectedIndex_].y -= nudgeAmount_;
    if (e.key == tc::KEY_DOWN)  dstPoints[selectedIndex_].y += nudgeAmount_;
}

void QuadWarp::save(const std::string& path) {
    tc::Json json;
    
    // Save source points
    tc::Json srcArr = tc::Json::array();
    for (int i = 0; i < 4; i++) {
        tc::Json p;
        p["x"] = srcPoints[i].x;
        p["y"] = srcPoints[i].y;
        srcArr.push_back(p);
    }
    json["quadwarp"]["src"] = srcArr;

    // Save destination points
    tc::Json dstArr = tc::Json::array();
    for (int i = 0; i < 4; i++) {
        tc::Json p;
        p["x"] = dstPoints[i].x;
        p["y"] = dstPoints[i].y;
        dstArr.push_back(p);
    }
    json["quadwarp"]["dst"] = dstArr;

    if (!tc::saveJson(json, path)) {
        tc::logError("QuadWarp") << "Cannot save to " << path;
        return;
    }
    tc::logNotice("QuadWarp") << "Saved to " << path;
}

bool QuadWarp::load(const std::string& path) {
    auto fail = [&path](const std::string& reason) {
        tc::logWarning("QuadWarp") << "Cannot load " << path << ": " << reason;
        return false;
    };

    try {
        const tc::Json json = tc::loadJson(path);
        if (!json.is_object()) {
            return fail("expected a JSON object (file missing, unreadable or invalid)");
        }
        const auto q = json.find("quadwarp");
        if (q == json.end() || !q->is_object()) {
            return fail("quadwarp must be an object");
        }

        auto readPoints = [&](const char* name, tc::Vec2 (&points)[4]) {
            const auto array = q->find(name);
            if (array == q->end() || !array->is_array() || array->size() < 4) {
                return fail(std::string(name) + " must be an array of at least 4 points");
            }
            for (int i = 0; i < 4; ++i) {
                const auto& p = array->at(i);
                const std::string point = std::string(name) + " point " + std::to_string(i);
                if (!p.is_object()) {
                    return fail(point + " must be an object");
                }
                const auto x = p.find("x");
                const auto y = p.find("y");
                if (x == p.end() || y == p.end() || !x->is_number() || !y->is_number()) {
                    return fail(point + " must have numeric x and y");
                }
                const double px = x->get<double>();
                const double py = y->get<double>();
                const double max = std::numeric_limits<float>::max();
                if (!std::isfinite(px) || !std::isfinite(py) ||
                    std::abs(px) > max || std::abs(py) > max) {
                    return fail(point + " coordinates must be finite floats");
                }
                points[i].set(static_cast<float>(px), static_cast<float>(py));
            }
            return true;
        };

        tc::Vec2 src[4];
        tc::Vec2 dst[4];
        if (!readPoints("src", src) || !readPoints("dst", dst)) {
            return false;
        }
        for (int i = 0; i < 4; ++i) {
            srcPoints[i] = src[i];
            dstPoints[i] = dst[i];
        }
    } catch (const tc::Json::exception& e) {
        return fail(std::string("invalid JSON: ") + e.what());
    }
    tc::logNotice("QuadWarp") << "Loaded from " << path;
    return true;
}

} // namespace tcx::quadwarp
