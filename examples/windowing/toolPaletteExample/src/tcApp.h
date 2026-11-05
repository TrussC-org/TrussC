#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// What the palette chooses and the canvas paints with. Both Apps hold it
// through a shared_ptr, so neither can outlive the other's use of it.
struct Brush {
    int color = 0;
    int size = 1;
};

inline const vector<Color>& swatchColors() {
    static const vector<Color> colors = {
        Color(0.95f, 0.30f, 0.30f), Color(0.98f, 0.70f, 0.20f), Color(0.95f, 0.90f, 0.35f),
        Color(0.35f, 0.80f, 0.45f), Color(0.30f, 0.60f, 0.95f), Color(0.80f, 0.45f, 0.90f),
    };
    return colors;
}
inline const vector<float>& brushRadii() {
    static const vector<float> radii = {4.0f, 9.0f, 16.0f};
    return radii;
}

// The palette window: six colour swatches and three brush sizes. Click to
// choose; the current choice has a white outline.
class PaletteApp : public App {
public:
    explicit PaletteApp(shared_ptr<Brush> brush) : brush_(std::move(brush)) {}

    void draw() override {
        clear(0.18f, 0.19f, 0.23f);
        setColor(0.7f);
        drawBitmapString("colour", 14, 22);
        drawBitmapString("size", 14, 146);

        const auto& colors = swatchColors();
        for (int i = 0; i < (int)colors.size(); i++) {
            const Rect r = swatchRect(i);
            fill();
            setColor(colors[i]);
            drawRect(r.x, r.y, r.width, r.height);
            if (i == brush_->color) outline(r.x - 3, r.y - 3, r.width + 6, r.height + 6);
        }

        const auto& radii = brushRadii();
        for (int i = 0; i < (int)radii.size(); i++) {
            const Rect r = sizeRect(i);
            fill();
            setColor(colors[brush_->color]);
            drawCircle(r.getCenterX(), r.getCenterY(), radii[i]);
            if (i == brush_->size) outline(r.x - 2, r.y - 2, r.width + 4, r.height + 4);
        }
    }

    void mousePressed(Vec2 pos, int button) override {
        if (button != MOUSE_BUTTON_LEFT) return;
        for (int i = 0; i < (int)swatchColors().size(); i++)
            if (swatchRect(i).contains(pos.x, pos.y)) brush_->color = i;
        for (int i = 0; i < (int)brushRadii().size(); i++)
            if (sizeRect(i).contains(pos.x, pos.y)) brush_->size = i;
    }

private:
    // 3 x 2 swatches, then one row of sizes.
    static Rect swatchRect(int i) { return Rect(14 + (i % 3) * 56, 34 + (i / 3) * 56, 48, 48); }
    static Rect sizeRect(int i) { return Rect(14 + i * 56, 156, 48, 48); }

    static void outline(float x, float y, float w, float h) {
        noFill();
        setColor(1.0f);
        setStrokeWeight(2.0f);
        drawRect(x, y, w, h);
        fill();
    }

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
    void mouseReleased(Vec2 pos, int button) override;

private:
    struct Dot { Vec2 pos; Color color; float radius; };

    void openPalette();
    void paint(Vec2 pos);

    shared_ptr<Brush> brush_ = make_shared<Brush>();
    shared_ptr<Window> palette_;
    vector<Dot> dots_;
    Vec2 last_;
    bool painting_ = false;
};
