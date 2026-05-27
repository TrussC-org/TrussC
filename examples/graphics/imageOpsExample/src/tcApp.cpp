#include "tcApp.h"
#include <cmath>

void tcApp::setup() {
    const int W = 256;
    const int H = 256;

    // Each Image carries its own pixels + GPU texture, so we have to build
    // the same pattern in every one before applying its operation. After
    // the op, the underlying texture is rebuilt automatically by Image —
    // we just need a single update() pass once we're inside a render pass
    // (handled in draw()).
    buildPattern(source_,     W, H);

    buildPattern(halve1_,     W, H); halve1_.halve();                    // 256 -> 128
    buildPattern(halve3_,     W, H); halve3_.halve(); halve3_.halve(); halve3_.halve();  // 256 -> 32

    buildPattern(resizeUp_,   W, H); resizeUp_.resize(400, 400);          // upscale (Bicubic)
    buildPattern(resizeDown_, W, H); resizeDown_.resize(32,  32);         // hard downscale (BoxArea) — same size as `halve x3` for side-by-side comparison
    buildPattern(resizeAniso_,W, H); resizeAniso_.resize(400, 40);        // mixed: Bicubic wider + BoxArea much shorter (10:1)

    buildPattern(cropInside_, W, H); cropInside_.crop(64, 64, 128, 128);  // strictly in-bounds
    buildPattern(cropClamp_,  W, H); cropClamp_.crop(192, 192, 256, 256); // partly out-of-bounds, clamp-to-edge

    buildPattern(mirrorH_,    W, H); mirrorH_.mirrorH();
    buildPattern(mirrorV_,    W, H); mirrorV_.mirrorV();
    buildPattern(mirrorBoth_, W, H); mirrorBoth_.mirror(true, true);
}

// Build the source pattern directly via setColor. The mix of a high-
// frequency checker, a smooth radial highlight, and a corner marker
// makes the effect of each operation visually obvious:
//   - the checker shows aliasing / mip quality
//   - the radial gradient shows resampling smoothness
//   - the red corner marker shows mirror orientation at a glance
void tcApp::buildPattern(Image& img, int w, int h) {
    img.allocate(w, h, 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Background checker (16x16 cells, black / light grey).
            int cx = x / 16;
            int cy = y / 16;
            bool checker = ((cx + cy) & 1) == 0;
            float base = checker ? 0.85f : 0.05f;

            // Soft radial highlight from the centre. Sub-pixel maths so the
            // gradient survives bicubic resamples without banding.
            float dx = (x - w * 0.5f) / (w * 0.5f);
            float dy = (y - h * 0.5f) / (h * 0.5f);
            float r = std::sqrt(dx * dx + dy * dy);
            float ring = 1.0f - std::min(1.0f, r);

            float vR = base * 0.6f + ring * 0.4f;
            float vG = base * 0.7f + ring * 0.5f;
            float vB = base * 0.9f + ring * 0.6f;

            // Bottom-left red marker so mirror flips are unambiguous.
            // (A diamond shape rather than a square so it's not visually
            // symmetric.)
            int mx = w / 8;
            int my = h * 7 / 8;
            int span = w / 8;
            int adx = std::abs(x - mx);
            int ady = std::abs(y - my);
            if (adx + ady < span) {
                vR = 1.0f;
                vG = 0.15f;
                vB = 0.15f;
            }

            img.setColor(x, y, Color(vR, vG, vB, 1.0f));
        }
    }
}

void tcApp::draw() {
    clear(0.12f);

    // setColor changes pixels_ on the CPU but the texture upload has to
    // happen inside a render pass. We do it once on the first draw call.
    if (!updatedOnce_) {
        source_.update();
        halve1_.update();
        halve3_.update();
        resizeUp_.update();
        resizeDown_.update();
        resizeAniso_.update();
        cropInside_.update();
        cropClamp_.update();
        mirrorH_.update();
        mirrorV_.update();
        mirrorBoth_.update();
        updatedOnce_ = true;
    }

    setColor(1.0f, 1.0f, 1.0f);

    // 4 columns x 3 rows grid. Each cell renders the image at a fixed
    // 200x200 box regardless of its actual pixel dimensions, so size
    // differences (e.g. halve x3 -> 32x32 stretched to 200) are visible.
    const float cellW   = 200.0f;
    const float cellH   = 200.0f;
    const float labelH  = 24.0f;
    const float margin  = 12.0f;

    auto cell = [&](Image& img, int col, int row, const string& label) {
        float x = margin + col * (cellW + margin);
        float y = margin + row * (cellH + labelH + margin);
        // Label sits in the top reserved strip of the cell. drawBitmapString's
        // y is the top of the text (not the baseline), so we add a small
        // top margin and leave ~3 px of breathing room before the image
        // starts at y + labelH.
        setColor(0.7f, 0.7f, 0.7f);
        drawBitmapString(label, x, y + 8);
        setColor(1.0f, 1.0f, 1.0f);
        img.draw(x, y + labelH, cellW, cellH);
    };

    // Row 0: source vs halve
    cell(source_,      0, 0, "source 256x256");
    cell(halve1_,      1, 0, "halve  ->128");
    cell(halve3_,      2, 0, "halve x3 ->32");
    cell(resizeUp_,    3, 0, "resize 400x400");

    // Row 1: resize variants
    cell(resizeDown_,  0, 1, "resize 32x32");
    cell(resizeAniso_, 1, 1, "resize 400x40");
    cell(cropInside_,  2, 1, "crop(64,64,128,128)");
    cell(cropClamp_,   3, 1, "crop OOB (clamp)");

    // Row 2: mirror
    cell(mirrorH_,     0, 2, "mirrorH");
    cell(mirrorV_,     1, 2, "mirrorV");
    cell(mirrorBoth_,  2, 2, "mirror(t,t) = 180");

    setColor(0.5f, 0.5f, 0.5f);
    // drawBitmapString's y is the top of the text and the bitmap font is
    // ~13 px tall, so place the top about 20 px above the window bottom
    // to keep the descenders inside the visible area.
    drawBitmapString("imageOpsExample - watch the red diamond marker for mirror direction",
                     margin, getHeight() - 20);
}
