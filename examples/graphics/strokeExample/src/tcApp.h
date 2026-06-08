#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void mouseMoved(const MouseMoveEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    void mousePressed(const MouseEventArgs& e) override;
    void keyPressed(int key) override;

private:
    void addPoint(Vec2 pos);
    vector<Vec2> history;
    static constexpr int maxHistory = 100;
    bool useStroke = true;
    int styleIndex = 0;  // 0: ROUND-ROUND, 1: MITER-BUTT, 2: BEVEL-SQUARE
};
