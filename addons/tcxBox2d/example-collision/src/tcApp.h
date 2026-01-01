// =============================================================================
// example-collision - Collision callback demonstration
// =============================================================================

#pragma once

#include <TrussC.h>
#include <tcxBox2d.h>

using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    box2d::World world;

    // Bodies
    std::vector<std::shared_ptr<box2d::CircleBody>> balls;
    std::shared_ptr<box2d::RectBody> floor;

    // Collision state for visualization
    struct CollisionInfo {
        Vec2 point;
        float timer = 0;
        Color color;
    };
    std::vector<CollisionInfo> collisionMarkers;

    // Event listeners
    EventListener enterListener;
    EventListener exitListener;
    std::vector<EventListener> ballListeners;

    // Stats
    int enterCount = 0;
    int stayCount = 0;
    int exitCount = 0;

    void setup() override;
    void update() override;
    void draw() override;

    void mousePressed(Vec2 pos, int button) override;
    void mouseDragged(Vec2 pos, int button) override;
    void mouseReleased(Vec2 pos, int button) override;
    void keyPressed(int key) override;

private:
    void setupFloorCollider();
    void addBall(float x, float y);
};
