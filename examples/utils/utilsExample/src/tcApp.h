#pragma once

#include <TrussC.h>
#include <iostream>
using namespace std;
using namespace tc;

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
