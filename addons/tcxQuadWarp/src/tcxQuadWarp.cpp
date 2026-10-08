#include "tcxQuadWarp.h"

#include <cmath>
#include <limits>

namespace tcx::quadwarp {

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

void QuadWarp::setInputEnabled(bool enabled) {
    if (inputEnabled_ == enabled) return;
    
    inputEnabled_ = enabled;
    
    if (enabled) {
        mouseMoveListener_ = tc::events().mouseMoved.listen(this, &QuadWarp::onMouseMoved);
        mousePressListener_ = tc::events().mousePressed.listen(this, &QuadWarp::onMousePressed);
        mouseDragListener_ = tc::events().mouseDragged.listen(this, &QuadWarp::onMouseDragged);
        mouseReleaseListener_ = tc::events().mouseReleased.listen(this, &QuadWarp::onMouseReleased);
        keyPressListener_ = tc::events().keyPressed.listen(this, &QuadWarp::onKeyPressed);
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
