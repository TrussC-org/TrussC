#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    Image image;
    EventListener opened;
public:
    void setup() override {
        opened = runtime().events().filesOpened.listen([this](FilesOpenedEventArgs& e) {
            if (!e.files.empty()) image.load(e.files.front());
        });
    }
    void draw() override {
        clear(0.15f);
        if (image.isAllocated()) image.draw(0, 0);
    }
};
