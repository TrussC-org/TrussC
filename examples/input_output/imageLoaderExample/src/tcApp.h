#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    Image bikers;
    Image gears;
    Image poster;
    Image transparency;
    Image icon;
};
