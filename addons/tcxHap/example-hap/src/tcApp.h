#pragma once

#include <TrussC.h>
#include "tcxHapPlayer.h"

using namespace std;
using namespace tc;
using namespace tcx::hap;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void filesDropped(const vector<string>& files) override;

private:
    HapPlayer player_;
    string statusText_ = "Drop a HAP-encoded .mov file to play";
};
