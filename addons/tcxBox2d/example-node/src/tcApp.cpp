// =============================================================================
// box2dNodeExample - tcxBox2d + Node integration sample
// =============================================================================
// Body inherits from tc::Node and can be integrated into the Node tree.
// Managed with shared_ptr and added via root.addChild(),
// allowing batch processing with root.updateTree() / root.drawTree().
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

    // Root node (created with shared_ptr)
    std::shared_ptr<Node> root = std::make_shared<Node>();

    // Hold bodies with shared_ptr
    std::vector<std::shared_ptr<box2d::CircleBody>> circles;
    std::vector<std::shared_ptr<box2d::RectBody>> rects;
    std::vector<std::shared_ptr<box2d::PolyShape>> polygons;

    void setup() override {
        // Initialize physics world (gravity: 300px/sec² downward)
        world.setup(Vec2(0, 300));

        // Create boundary walls at screen edges
        world.createBounds();

        // Add root node to scene graph for automatic update/draw
        addChild(root);

        // Place initial objects
        for (int i = 0; i < 3; ++i) {
            addCircle(200 + i * 100, 100, 25 + i * 10);
        }

        for (int i = 0; i < 2; ++i) {
            addRect(250 + i * 150, 200, 50, 30);
        }

        // Add hexagon
        addPolygon(400, 50, 30, 6);
    }

    void addCircle(float x, float y, float radius) {
        auto circle = std::make_shared<box2d::CircleBody>();
        circle->setup(world, x, y, radius);
        circle->setRestitution(0.7f);

        root->addChild(circle);  // Add to Node tree
        circles.push_back(circle);
    }

    void addRect(float x, float y, float w, float h) {
        auto rect = std::make_shared<box2d::RectBody>();
        rect->setup(world, x, y, w, h);
        rect->setRestitution(0.3f);

        root->addChild(rect);
        rects.push_back(rect);
    }

    void addPolygon(float x, float y, float radius, int sides) {
        auto poly = std::make_shared<box2d::PolyShape>();
        poly->setupRegular(world, x, y, radius, sides);
        poly->setRestitution(0.5f);

        root->addChild(poly);
        polygons.push_back(poly);
    }

    void update() override {
        // Advance physics simulation
        world.update();

        // No need to call root->updateTree() manually
    }

    void draw() override {
        clear(0.12f);

        // Nodes are drawn automatically after this function.
        // Set a default color for them.
        setColor(1.0f, 0.78f, 0.4f);

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
        drawBitmapString("Middle click: Add hexagon", 10, 52);
        drawBitmapString("C: Clear all", 10, 68);
        drawBitmapString("Bodies: " + std::to_string(world.getBodyCount()), 10, 84);
        drawBitmapString("Node children: " + std::to_string(root->getChildCount()), 10, 100);

        // Show Node coordinates (for debugging)
        if (!circles.empty()) {
            auto& c = circles[0];
            drawBitmapString("Circle[0] Node x,y: " +
                std::to_string((int)c->getX()) + ", " + std::to_string((int)c->getY()), 10, 126);
        }
        
        // Reset color for automatic drawing
        setColor(1.0f, 0.78f, 0.4f);
    }

    void mousePressed(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            // Start dragging if body exists at point
            box2d::Body* body = world.getBodyAtPoint(pos.x, pos.y);
            if (body) {
                world.startDrag(body, pos.x, pos.y);
            } else {
                // Otherwise add a circle
                addCircle(pos.x, pos.y, randomFloat(15, 40));
            }
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            addRect(pos.x, pos.y, randomFloat(30, 60), randomFloat(20, 40));
        }
        else if (button == MOUSE_BUTTON_MIDDLE) {
            // Middle click: add random polygon (3-8 sides)
            int sides = 3 + rand() % 6;
            addPolygon(pos.x, pos.y, randomFloat(20, 40), sides);
        }
    }

    void mouseDragged(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            world.updateDrag(pos.x, pos.y);
        }
    }

    void mouseReleased(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            world.endDrag();
        }
    }

    void keyPressed(int key) override {
        if (key == 'c' || key == 'C') {
            // C key: clear all
            root->removeAllChildren();
            circles.clear();
            rects.clear();
            polygons.clear();
            world.clear();
            world.createBounds();
        }
    }
};

int main() {
    WindowSettings settings;
    settings.width = 800;
    settings.height = 600;
    settings.title = "box2dNodeExample";

    return runApp<tcApp>(settings);
}
