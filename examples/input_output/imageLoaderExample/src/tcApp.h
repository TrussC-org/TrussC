#pragma once

#include "tcBaseApp.h"

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    tc::Image bikers;
    tc::Image gears;
    tc::Image poster;
    tc::Image transparency;
    tc::Image icon;
};
