#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void mouseMoved(Vec2 pos) override;
    void mousePressed(Vec2 pos, int button) override;
    void keyPressed(int key) override;

private:
    vector<Vec2> history;
    static constexpr int maxHistory = 100;
    bool useStroke = true;
    int styleIndex = 0;  // 0: ROUND-ROUND, 1: MITER-BUTT, 2: BEVEL-SQUARE
};
