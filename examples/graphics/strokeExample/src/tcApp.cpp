#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("strokeExample - Space to toggle mode");
}

void tcApp::draw() {
    clear(0.1f);

    if (history.size() < 2) return;

    setColor(colors::hotPink);
    setStrokeWeight(30.0f);

    // Apply style based on styleIndex
    string styleName;
    switch (styleIndex) {
        case 0:
            setStrokeCap(StrokeCap::Round);
            setStrokeJoin(StrokeJoin::Round);
            styleName = "ROUND-ROUND";
            break;
        case 1:
            setStrokeCap(StrokeCap::Butt);
            setStrokeJoin(StrokeJoin::Miter);
            styleName = "MITER-BUTT";
            break;
        case 2:
            setStrokeCap(StrokeCap::Square);
            setStrokeJoin(StrokeJoin::Bevel);
            styleName = "BEVEL-SQUARE";
            break;
    }

    if (useStroke) {
        beginStroke();
    } else {
        noFill();
        beginShape();
    }

    for (auto& p : history) {
        vertex(p);
    }

    if (useStroke) {
        endStroke();
    } else {
        endShape();
    }

    // Mode indicator
    setColor(colors::white);
    string mode = useStroke ? "beginStroke" : "beginShape";
    string info = mode + " | " + styleName + " (click: mode, space: style)";
    drawBitmapString(info, 10, 20);
}

void tcApp::addPoint(Vec2 pos) {
    history.push_back(pos);
    if (history.size() > maxHistory) {
        history.erase(history.begin());
    }
}

// Hover (desktop) and drag (touch / mouse drag) both extend the line
void tcApp::mouseMoved(const MouseMoveEventArgs& e) {
    addPoint(e.pos);
}

void tcApp::mouseDragged(const MouseDragEventArgs& e) {
    addPoint(e.pos);
}

void tcApp::mousePressed(const MouseEventArgs& e) {
    useStroke = !useStroke;
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        styleIndex = (styleIndex + 1) % 3;
    }
}
