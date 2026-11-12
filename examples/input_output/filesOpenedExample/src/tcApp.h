#pragma once
#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
    void filesOpened(FilesOpenedEventArgs& e);
    Image image;
    vector<fs::path> files;
    EventListener opened;
public:
    void setup() override;
    void draw() override;
};
