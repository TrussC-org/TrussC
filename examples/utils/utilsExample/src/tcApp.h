#pragma once

#include "tcBaseApp.h"
using namespace tc;
#include <iostream>
#include <vector>
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

private:
    // Data for string testing
    string testString_ = "Hello, TrussC World!";
    vector<string> splitResult_;
};
