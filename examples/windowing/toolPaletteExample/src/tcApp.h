#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

const vector<Color> COLORS = {
    Color(0.95f, 0.30f, 0.30f), Color(0.98f, 0.70f, 0.20f), Color(0.95f, 0.90f, 0.35f),
    Color(0.35f, 0.80f, 0.45f), Color(0.30f, 0.60f, 0.95f), Color(0.80f, 0.45f, 0.90f),
};
const vector<float> RADII = {4.0f, 9.0f, 16.0f};

// What the palette chooses and the canvas paints with, shared by both Apps.
struct Brush {
    int color = 0;
    int size = 1;
};

// The palette window: a row of colors and a row of brush sizes. Click to
// choose; the current choice has a white outline.
class PaletteApp : public App {
public:
    explicit PaletteApp(shared_ptr<Brush> brush) : brush_(std::move(brush)) {}

    void draw() override {
        clear(0.18f, 0.19f, 0.23f);
        for (int i = 0; i < (int)COLORS.size(); i++) {
            setColor(COLORS[i]);
            drawRect(colorRect(i));
        }
        for (int i = 0; i < (int)RADII.size(); i++) {
            setColor(COLORS[brush_->color]);
            drawCircle(sizeRect(i).getCenter(), RADII[i]);
        }
        noFill();
        setColor(1.0f);
        drawRect(colorRect(brush_->color));
        drawRect(sizeRect(brush_->size));
        fill();
    }

    void mousePressed(Vec2 pos, int button) override {
        for (int i = 0; i < (int)COLORS.size(); i++)
            if (colorRect(i).contains(pos.x, pos.y)) brush_->color = i;
        for (int i = 0; i < (int)RADII.size(); i++)
            if (sizeRect(i).contains(pos.x, pos.y)) brush_->size = i;
    }

private:
    static void drawRect(const Rect& r) { tc::drawRect(r.x, r.y, r.width, r.height); }
    static Rect colorRect(int i) { return Rect(12 + (i % 3) * 60, 12 + (i / 3) * 60, 52, 52); }
    static Rect sizeRect(int i) { return Rect(12 + i * 60, 140, 52, 52); }

    shared_ptr<Brush> brush_;
};

// The canvas window: drag to paint, C clears, P opens the palette again.
class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;

private:
    struct Dot { Vec2 pos; Color color; float radius; };

    void openPalette();

    shared_ptr<Brush> brush_ = make_shared<Brush>();
    shared_ptr<Window> palette_;
    vector<Dot> dots_;
    Vec2 last_;
};
