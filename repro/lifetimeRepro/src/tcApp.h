#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void keyPressed(int key) override;
    void keyReleased(int key) override;

    void mousePressed(const MouseEventArgs& e) override;
    void mouseReleased(const MouseEventArgs& e) override;
    void mouseMoved(const MouseMoveEventArgs& e) override;
    void mouseDragged(const MouseDragEventArgs& e) override;
    void mouseScrolled(const ScrollEventArgs& e) override;

    void windowResized(int width, int height) override;
    void filesDropped(const vector<string>& files) override;
    void exit() override;

private:
    // Test 1 control: member Image, identical content to the scope-local one
    Image memberImg;

    // Test 3: mipmapped dynamic Image, loadData'd AFTER its draw each frame
    Image mipImg;
    // Test 3 control: identical mipmapped dynamic Image, never updated after setup
    Image mipImgControl;

    // Test 2: glyph batch registration state
    uint32_t nextCodepoint = 0x4E00;  // CJK block, sequential
    uint32_t lastRegisteredCp = 0;
    int batchesRegistered = 0;

    void drawRegionFrame(float x, float y, float w, float h, const Color& c);
    static void fillImageSolid(Image& img, const Color& c);
};
