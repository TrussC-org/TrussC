// =============================================================================
// box2dBasicExample - Basic tcxBox2d addon sample
// =============================================================================
// Click to spawn circles that fall with physics simulation.
// Simple example without Node tree - each Body is drawn individually.
// =============================================================================

#include "tcBaseApp.h"
#include <tcxBox2d.h>
#include <vector>
#include <memory>
#include <cstdlib>

using namespace tc;
using namespace tcx;

// Generate random float (min to max)
float randomFloat(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

class tcApp : public tc::App {
public:
    box2d::World world;
    std::vector<std::shared_ptr<box2d::CircleBody>> circles;
    std::vector<std::shared_ptr<box2d::RectBody>> rects;

    void setup() override {
        // Initialize physics world (gravity: 300px/sec² downward)
        world.setup(Vec2(0, 300));

        // Create boundary walls at screen edges
        world.createBounds();

        // Place initial objects (stacked loosely)
        auto c1 = std::make_shared<box2d::CircleBody>();
        c1->setup(world, 350, 80, 30);
        circles.push_back(c1);

        auto c2 = std::make_shared<box2d::CircleBody>();
        c2->setup(world, 380, 150, 25);
        circles.push_back(c2);

        auto c3 = std::make_shared<box2d::CircleBody>();
        c3->setup(world, 340, 200, 35);
        circles.push_back(c3);

        auto c4 = std::make_shared<box2d::CircleBody>();
        c4->setup(world, 400, 120, 20);
        circles.push_back(c4);

        auto c5 = std::make_shared<box2d::CircleBody>();
        c5->setup(world, 360, 250, 28);
        circles.push_back(c5);
    }

    void update() override {
        // Advance physics simulation
        world.update();

        // Sync Box2D positions to Node's x, y, rotation
        for (auto& c : circles) c->updateTree();
        for (auto& r : rects) r->updateTree();
    }

    void draw() override {
        clear(30);

        // Draw all circles (drawTree applies position/rotation)
        setColor(1.0f, 0.78f, 0.4f);
        for (auto& circle : circles) {
            circle->drawTree();
        }

        // Draw all rectangles
        setColor(0.4f, 0.78f, 1.0f);
        for (auto& rect : rects) {
            rect->drawTree();
        }

        // Draw spring line if dragging
        if (world.isDragging()) {
            Vec2 anchor = world.getDragAnchor();
            setColor(1.0f, 0.4f, 0.4f);
            drawLine(anchor.x, anchor.y, getMouseX(), getMouseY());
        }

        // Display usage instructions
        setColor(1.0f);
        drawBitmapString("Left click: Add circle / Drag body", 10, 20);
        drawBitmapString("Right click: Add rectangle", 10, 36);
        drawBitmapString("C: Clear all", 10, 52);
        drawBitmapString("Bodies: " + std::to_string(world.getBodyCount()), 10, 68);
    }

    void mousePressed(int x, int y, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            // Start dragging if body exists at point
            box2d::Body* body = world.getBodyAtPoint((float)x, (float)y);
            if (body) {
                world.startDrag(body, (float)x, (float)y);
            } else {
                // Otherwise add a circle
                auto circle = std::make_shared<box2d::CircleBody>();
                circle->setup(world, (float)x, (float)y, randomFloat(15, 40));
                circle->setRestitution(0.7f);  // Bouncy
                circles.push_back(circle);
            }
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            // Right click: add rectangle
            auto rect = std::make_shared<box2d::RectBody>();
            rect->setup(world, (float)x, (float)y, randomFloat(30, 60), randomFloat(20, 40));
            rect->setRestitution(0.3f);
            rects.push_back(rect);
        }
    }

    void mouseDragged(int x, int y, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            world.updateDrag((float)x, (float)y);
        }
    }

    void mouseReleased(int x, int y, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            world.endDrag();
        }
    }

    void keyPressed(int key) override {
        if (key == 'c' || key == 'C') {
            // C key: clear all
            circles.clear();
            rects.clear();
            world.clear();
            world.createBounds();
        }
    }
};

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.title = "box2dBasicExample";

    return runApp<tcApp>(settings);
}
