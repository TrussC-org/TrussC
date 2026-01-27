// =============================================================================
// example-collision - Collision callback demonstration
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    world.setup(Vec2(0, 300));
    world.createBounds();

    // Create floor
    floor = std::make_shared<box2d::RectBody>();
    floor->setup(world, getWindowWidth() / 2.0f, getWindowHeight() - 50.0f,
                 getWindowWidth() - 100.0f, 20.0f);
    floor->setStatic();
    addChild(floor);

    // Setup collision callback for floor
    setupFloorCollider();

    // Create initial balls
    for (int i = 0; i < 3; ++i) {
        addBall(200.0f + i * 150.0f, 100.0f);
    }
}

void tcApp::setupFloorCollider() {
    auto* collider = floor->getCollider();
    if (!collider) return;

    // onCollisionEnter - flash green
    enterListener = collider->onCollisionEnter.listen([this](box2d::CollisionEvent& e) {
        enterCount++;
        collisionMarkers.push_back({
            e.contactPoint,
            0.5f,
            Color(0.2f, 1.0f, 0.4f)  // green
        });
        logNotice("Collision") << "Enter! Contact at ("
            << (int)e.contactPoint.x << ", " << (int)e.contactPoint.y << ")";
    });

    // onCollisionExit - just count (no marker, contact point not reliable)
    exitListener = collider->onCollisionExit.listen([this](box2d::CollisionEvent& e) {
        exitCount++;
    });
}

void tcApp::addBall(float x, float y) {
    auto ball = std::make_shared<box2d::CircleBody>();
    ball->setup(world, x, y, 25);
    ball->setRestitution(0.7f);

    // Add collision callback to ball
    auto* collider = ball->getCollider();
    if (collider) {
        ballListeners.emplace_back();
        collider->onCollisionEnter.listen(ballListeners.back(), [this](box2d::CollisionEvent& e) {
            enterCount++;
            collisionMarkers.push_back({
                e.contactPoint,
                0.3f,
                Color(0.2f, 0.8f, 1.0f)  // cyan for ball collisions
            });
        });
    }

    addChild(ball);
    balls.push_back(ball);
}

void tcApp::update() {
    world.update();

    // Update collision markers
    float dt = 1.0f / 60.0f;
    for (auto& marker : collisionMarkers) {
        marker.timer -= dt;
    }
    collisionMarkers.erase(
        std::remove_if(collisionMarkers.begin(), collisionMarkers.end(),
            [](const CollisionInfo& m) { return m.timer <= 0; }),
        collisionMarkers.end()
    );
}

void tcApp::draw() {
    clear(0.1f);

    // Note: Node children (bodies) are drawn AFTER this draw() returns
    // So we need to set up drawing state for them here

    // Draw collision markers first (they'll appear behind bodies)
    for (auto& marker : collisionMarkers) {
        float alpha = marker.timer / 0.5f;
        setColor(marker.color.r, marker.color.g, marker.color.b, alpha);
        fill();
        drawCircle(marker.point.x, marker.point.y, 5);
    }

    // Reset drawing state for bodies (drawn after by Node system)
    noFill();
    setColor(0.8f);

    // Draw drag line
    if (world.isDragging()) {
        Vec2 anchor = world.getDragAnchor();
        setColor(1.0f, 0.4f, 0.4f);
        drawLine(anchor.x, anchor.y, getMouseX(), getMouseY());
    }

    // Draw UI
    setColor(1.0f);
    drawBitmapString("Collision Callback Demo", 10, 20);
    drawBitmapString("Click to add balls, drag to move", 10, 40);
    drawBitmapString("", 10, 60);
    drawBitmapString("Collision Events:", 10, 80);
    drawBitmapString("  Enter (green): " + std::to_string(enterCount), 10, 100);
    drawBitmapString("  Exit (red): " + std::to_string(exitCount), 10, 120);
    drawBitmapString("  Balls: " + std::to_string(balls.size()), 10, 140);
}

void tcApp::mousePressed(Vec2 pos, int button) {
    if (button == MOUSE_BUTTON_LEFT) {
        box2d::Body* body = world.getBodyAtPoint(pos);
        if (body) {
            world.startDrag(body, pos);
        } else {
            addBall(pos.x, pos.y);
        }
    }
}

void tcApp::mouseDragged(Vec2 pos, int button) {
    if (button == MOUSE_BUTTON_LEFT) {
        world.updateDrag(pos);
    }
}

void tcApp::mouseReleased(Vec2 pos, int button) {
    if (button == MOUSE_BUTTON_LEFT) {
        world.endDrag();
    }
}

void tcApp::keyPressed(int key) {
    if (key == 'c' || key == 'C') {
        // Clear balls (keep floor)
        for (auto& ball : balls) {
            removeChild(ball);
        }
        balls.clear();
        ballListeners.clear();
        enterCount = 0;
        exitCount = 0;
    }
    else if (key == 'r' || key == 'R') {
        // Reset counters
        enterCount = 0;
        exitCount = 0;
    }
}
