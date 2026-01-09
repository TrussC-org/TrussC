#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("strokeExample - Space to toggle mode");
}

void tcApp::draw() {
    clear(0.1f);

    if (history.size() < 2) return;

    setColor(colors::hotPink);
    setStrokeWeight(30.0f);
    setStrokeCap(StrokeCap::Round);
    setStrokeJoin(StrokeJoin::Round);

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
    string info = mode + " | weight=" + to_string((int)getStrokeWeight()) + " (click to toggle)";
    drawBitmapString(info, 10, 20);
}

void tcApp::mouseMoved(Vec2 pos) {
    history.push_back(pos);
    if (history.size() > maxHistory) {
        history.erase(history.begin());
    }
}

void tcApp::mousePressed(Vec2 pos, int button) {
    useStroke = !useStroke;
}
