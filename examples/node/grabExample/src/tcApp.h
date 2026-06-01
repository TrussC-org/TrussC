#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// DraggableRect - Drag to move
// =============================================================================
class DraggableRect : public RectNode {
public:
    using Ptr = shared_ptr<DraggableRect>;

    Color bgColor = Color::fromHSB(0.6f, 0.5f, 0.6f);

    DraggableRect(float w, float h) {
        setSize(w, h);
        enableEvents();
    }

    void update() override {
        // Apply accumulated drag in update (once per frame)
        if (isDragging_ && hasPendingDrag_) {
            float dx = pendingDragPos_.x - dragOffset_.x;
            float dy = pendingDragPos_.y - dragOffset_.y;
            setPos(getX() + dx, getY() + dy);
            hasPendingDrag_ = false;
        }
    }

    void draw() override {
        Color color = isDragging_ ? bgColor * 1.3f :
                      isMouseOver() ? bgColor * 1.1f : bgColor;
        setColor(color);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        // Border
        noFill();
        setColor(1.0f, 1.0f, 1.0f, 0.5f);
        drawRect(0, 0, getWidth(), getHeight());

        // Label
        setColor(1.0f, 1.0f, 1.0f);
        drawBitmapString("Drag me!", 10, getHeight() / 2 + 4);
    }

protected:
    bool isDragging_ = false;
    Vec2 dragOffset_;
    Vec2 pendingDragPos_;
    bool hasPendingDrag_ = false;

    bool onMousePress(const MouseEventArgs& e) override {
        if (e.button == MOUSE_BUTTON_LEFT) {
            isDragging_ = true;
            dragOffset_ = e.pos;
            hasPendingDrag_ = false;
            return true;  // Grab
        }
        return false;
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        if (e.button == MOUSE_BUTTON_LEFT) {
            isDragging_ = false;
            hasPendingDrag_ = false;
        }
        return true;
    }

    bool onMouseDrag(const MouseDragEventArgs& e) override {
        if (isDragging_) {
            // Store latest position for processing in update
            pendingDragPos_ = e.pos;
            hasPendingDrag_ = true;
            return true;
        }
        return false;
    }
};

// =============================================================================
// DrawingCanvas - Drag to draw lines (with clipping)
// =============================================================================
class DrawingCanvas : public RectNode {
public:
    using Ptr = shared_ptr<DrawingCanvas>;

    DrawingCanvas(float w, float h) {
        setSize(w, h);
        enableEvents();
        setClipping(true);  // Enable scissor clipping
    }

    void update() override {
        // Process pending points in update (once per frame)
        if (isDrawing_ && !pendingPoints_.empty()) {
            for (const auto& pt : pendingPoints_) {
                currentLine_.push_back(pt);
            }
            pendingPoints_.clear();
        }
    }

    void draw() override {
        // Background
        setColor(0.15f, 0.15f, 0.2f);
        fill();
        drawRect(0, 0, getWidth(), getHeight());

        // Draw all lines (clipping is handled by RectNode via setClipping(true))
        setColor(1.0f, 0.8f, 0.2f);
        setStrokeWeight(3.0f);
        for (const auto& line : lines_) {
            if (line.size() >= 2) {
                beginStroke();
                for (const auto& pt : line) {
                    vertex(pt.x, pt.y);
                }
                endStroke();
            }
        }

        // Draw current line
        if (isDrawing_ && currentLine_.size() >= 2) {
            setColor(1.0f, 0.5f, 0.2f);
            beginStroke();
            for (const auto& pt : currentLine_) {
                vertex(pt.x, pt.y);
            }
            endStroke();
        }

        // Border
        noFill();
        setColor(0.5f, 0.5f, 0.6f);
        drawRect(0, 0, getWidth(), getHeight());

        // Label
        setColor(0.7f, 0.7f, 0.75f);
        drawBitmapString("Draw here (try dragging outside!)", 10, 20);
        drawBitmapString(format("Lines: {}", lines_.size()), 10, getHeight() - 10);
    }

    void clear() {
        lines_.clear();
        currentLine_.clear();
        pendingPoints_.clear();
    }

protected:
    vector<vector<Vec2>> lines_;
    vector<Vec2> currentLine_;
    vector<Vec2> pendingPoints_;  // Accumulated points for this frame
    bool isDrawing_ = false;

    bool onMousePress(const MouseEventArgs& e) override {
        if (e.button == MOUSE_BUTTON_LEFT) {
            isDrawing_ = true;
            currentLine_.clear();
            pendingPoints_.clear();
            currentLine_.push_back(e.pos);
            return true;  // Grab
        }
        return false;
    }

    bool onMouseRelease(const MouseEventArgs& e) override {
        if (e.button == MOUSE_BUTTON_LEFT && isDrawing_) {
            isDrawing_ = false;
            // Process any remaining pending points
            for (const auto& pt : pendingPoints_) {
                currentLine_.push_back(pt);
            }
            pendingPoints_.clear();
            if (currentLine_.size() >= 2) {
                lines_.push_back(currentLine_);
            }
            currentLine_.clear();
        }
        return true;
    }

    bool onMouseDrag(const MouseDragEventArgs& e) override {
        if (isDrawing_) {
            // Accumulate points for processing in update
            pendingPoints_.push_back(e.pos);
            return true;
        }
        return false;
    }
};

// =============================================================================
// Main app
// =============================================================================
class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    DraggableRect::Ptr draggable_;
    DrawingCanvas::Ptr canvas_;
};
