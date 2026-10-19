// =============================================================================
// box2dBasicExample - tcxBox2d Mod API sample (recommended starting point)
// =============================================================================
// Click to spawn shapes that fall with 2D physics. Each shape is a plain Node
// carrying two Mods:
//   - RigidBody2D       : the physics body (shape + material), drives the node
//   - ColliderRenderer2D: draws the shape
// The world auto-steps each frame (no world.update() in update()), and the Mods
// sync the node transform automatically. Shapes flash white on impact via the
// RigidBody2D::onCollisionEnter event.
// =============================================================================

#include <TrussC.h>
#include <tcxBox2d.h>
#include <algorithm>
#include <cstdlib>

using namespace tc;
using namespace tcx;
using namespace tcx::box2d;

static float randomFloat(float min, float max) {
    return min + (float)rand() / (float)RAND_MAX * (max - min);
}

// A physics-driven shape: RigidBody2D + ColliderRenderer2D, flashes on impact.
class Shape : public Node {
public:
    Shape(const Shape2D& shape, const Color& color, const Vec2& pos)
        : shape_(shape), baseColor_(color), startPos_(pos) {}

    void setup() override {
        setPos(startPos_.x, startPos_.y);                 // before the body is created
        auto* rb = addMod<RigidBody2D>(shape_);
        rb->setRestitution(0.55f);

        renderer_ = addMod<ColliderRenderer2D>();
        renderer_->setColor(baseColor_);
        renderer_->setFilled(true);

        beganL_ = rb->onCollisionEnter.listen(this, &Shape::onHit);
    }

    void update() override {
        // Decay the impact flash, then push the (brightened) color to the renderer.
        flash_ = std::max(0.0f, flash_ - (float)getDeltaTime() * 4.0f);
        if (renderer_) {
            renderer_->setColor(Color(baseColor_.r + (1.0f - baseColor_.r) * flash_,
                                      baseColor_.g + (1.0f - baseColor_.g) * flash_,
                                      baseColor_.b + (1.0f - baseColor_.b) * flash_));
        }
    }

    void onHit(Contact2D& c) { (void)c; flash_ = 1.0f; }

private:
    Shape2D            shape_;
    Color              baseColor_;
    Vec2               startPos_;
    ColliderRenderer2D* renderer_ = nullptr;
    EventListener      beganL_;
    float              flash_ = 0.0f;
};

class tcApp : public tc::App {
public:
    void setup() override {
        // Point the default world at gravity + screen-edge walls. Auto-update is
        // ON, so we never call world.update() ourselves.
        defaultWorld().setup(Vec2(0, 600));
        defaultWorld().createBounds();

        // A few shapes to start.
        for (int i = 0; i < 5; ++i) {
            spawnCircle(randomFloat(250, 550), randomFloat(60, 250));
        }
    }

    void draw() override {
        clear(0.12f);

        // Shapes draw themselves (ColliderRenderer2D) via the Node tree.
        setColor(1.0f);
        drawBitmapString("Left click: circle   Right click: box", 10, 20);
        drawBitmapString("Bodies: " + std::to_string(defaultWorld().getBodyCount()), 10, 36);
    }

    void mousePressed(const MouseEventArgs& e) override {
        if (e.button == MOUSE_BUTTON_LEFT)  spawnCircle(e.pos.x, e.pos.y);
        if (e.button == MOUSE_BUTTON_RIGHT) spawnBox(e.pos.x, e.pos.y);
    }

private:
    Color randomColor() {
        return Color(randomFloat(0.3f, 0.95f), randomFloat(0.3f, 0.95f), randomFloat(0.3f, 0.95f));
    }
    void spawnCircle(float x, float y) {
        addChild(std::make_shared<Shape>(Shape2D::circle(randomFloat(15, 35)), randomColor(), Vec2(x, y)));
    }
    void spawnBox(float x, float y) {
        addChild(std::make_shared<Shape>(Shape2D::box(randomFloat(30, 60), randomFloat(30, 60)), randomColor(), Vec2(x, y)));
    }
};

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.title = "box2dBasicExample";

    return TC_RUN_APP(tcApp, settings);
}
