#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    Image image;
    vector<fs::path> files;
    EventListener opened;
public:
    void setup() override {
        opened = runtime().events().filesOpened.listen([this](FilesOpenedEventArgs& e) {
            files = e.files;
            for (const auto& file : files) {
                if (image.load(file)) break;
            }
        });
    }
    void draw() override {
        clear(0.15f);
        if (image.isAllocated()) image.draw(0, 0);
        if (files.empty()) {
            drawBitmapStringHighlight("Drop files on the app icon, or use Open With", 20, 20);
        }
        float y = 20;
        for (const auto& file : files) {
            drawBitmapStringHighlight(pathToUtf8(file.filename()), 20, y);
            y += 20;
        }
    }
};
