#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// jsonXmlExample - JSON/XML Read/Write Demo

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Display messages
    vector<string> messages;

    void addMessage(const string& msg);
    void testJson();
    void testXml();
};
