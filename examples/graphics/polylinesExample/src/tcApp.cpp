#include "tcApp.h"
#include <sstream>

using std::stringstream;

void tcApp::setup() {
    setWindowTitle("polylinesExample");
    setupPolylines();
}

void tcApp::setupPolylines() {
    // 3x2 grid; col centers 160 / 480 / 800, row centers 230 / 430.

    // Line Polyline (col 1, row 1)
    linePolyline.clear();
    linePolyline.addVertex(60,  170);
    linePolyline.lineTo   (180, 230);
    linePolyline.lineTo   (120, 290);
    linePolyline.lineTo   (260, 290);

    // Cubic Bezier curve (col 2, row 1) — endpoints + peaks lowered to clear label
    bezierPolyline.clear();
    bezierPolyline.addVertex(360, 200);
    bezierPolyline.bezierTo(430, 140, 530, 350, 600, 200);

    // Quadratic Bezier curve (col 3, row 1) — same lowering
    quadPolyline.clear();
    quadPolyline.addVertex(680, 200);
    quadPolyline.quadBezierTo(800, 350, 920, 200);

    // Catmull-Rom spline (col 1, row 2) — shapes pushed down +50 from row labels
    curvePolyline.clear();
    curvePolyline.curveTo(40.0f,  470.0f);   // tangent only
    curvePolyline.curveTo(80.0f,  430.0f);   // curve begins here
    curvePolyline.curveTo(130.0f, 520.0f);
    curvePolyline.curveTo(180.0f, 430.0f);
    curvePolyline.curveTo(230.0f, 520.0f);
    curvePolyline.curveTo(280.0f, 470.0f);   // curve ends here (tangent only)

    // Star — closed shape (col 2, row 2)
    starPolyline.clear();
    {
        const int points = 5;
        const float outerR = 75;
        const float innerR = 32;
        const float scx = 480;
        const float scy = 480;
        for (int i = 0; i < points * 2; i++) {
            float angle = i * TAU / (points * 2) - QUARTER_TAU;
            float r = (i % 2 == 0) ? outerR : innerR;
            starPolyline.addVertex(scx + cos(angle) * r,
                                   scy + sin(angle) * r);
        }
        starPolyline.close();
    }

    // Arc (col 3, row 2)
    arcPolyline.clear();
    arcPolyline.arc(800.0f, 480.0f, 80.0f, 80.0f, 0.0f, 270.0f, 32);
}

void tcApp::update() {
    time += getDeltaTime();
}

void tcApp::draw() {
    clear(0.12f);

    switch (mode) {
        case 0: drawCurveDemo(); break;
        case 1: drawMouseDrawing(); break;
        case 2: drawAnimatedCurve(); break;
    }

    // UI
    setColor(1.0f);
    stringstream ss;
    ss << "Mode " << (mode + 1) << "/" << NUM_MODES << ": ";
    switch (mode) {
        case 0: ss << "Curve Types Demo"; break;
        case 1: ss << "Mouse Drawing"; break;
        case 2: ss << "Animated Curves"; break;
    }
    ss << "\n\nControls:\n";
    ss << "  1-3: Switch mode\n";
    ss << "  c: Clear mouse drawing";

    drawBitmapString(ss.str(), 20, 20);
}

void tcApp::drawCurveDemo() {
    // Draw with stroke only (Polyline fill supports only convex shapes)
    noFill();

    // Row 1
    setColor(colors::red);
    linePolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("lineTo()", 130, 150);

    setColor(colors::green);
    bezierPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("bezierTo()", 440, 150);

    setColor(colors::blue);
    quadPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("quadBezierTo()", 740, 150);

    // Row 2 (labels at +20, shapes at +50 vs row 1)
    setColor(colors::orange);
    curvePolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("curveTo()", 130, 370);

    setColor(colors::magenta);
    starPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("closed star", 440, 370);

    setColor(colors::blue);
    arcPolyline.draw();
    setColor(colors::darkGray);
    drawBitmapString("arc()", 780, 370);

    // Reset to default
    fill();
}

void tcApp::drawMouseDrawing() {
    setColor(0.78f);
    drawBitmapString("Click and drag to draw a polyline", 20, 120);
    drawBitmapString("Press 'c' to clear", 20, 140);

    // Mouse tracking
    if (isMousePressed()) {
        if (!isDrawing) {
            isDrawing = true;
        }
        mousePolyline.addVertex(getMouseX(), getMouseY());
    } else {
        isDrawing = false;
    }

    // Draw
    setColor(colors::lime);
    mousePolyline.draw();

    // Show vertices
    setColor(colors::red);
    for (size_t i = 0; i < mousePolyline.size(); i++) {
        drawCircle(mousePolyline[i].x, mousePolyline[i].y, 2);
    }

    // Info display
    setColor(0.78f);
    stringstream info;
    info << "Vertices: " << mousePolyline.size();
    info << "\nPerimeter: " << (int)mousePolyline.getPerimeter() << " px";
    drawBitmapString(info.str(), 20, 160);
}

void tcApp::drawAnimatedCurve() {
    int w = getWidth();
    int h = getHeight();
    float cy = h / 2.0f + 30;

    // Animated flower-like shape (left side, 1.5x size)
    float flowerCx = w * 0.3f;
    Path flower;
    int petals = 6;
    int segments = 60;

    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * TAU;
        float r = 150 + 75 * sin(petals * angle + time * 2);  // 1.5x
        float x = flowerCx + cos(angle) * r;
        float y = cy + sin(angle) * r;
        flower.addVertex(x, y);
    }
    flower.close();

    // Gradient-like drawing
    noFill();
    for (int i = 0; i < 5; i++) {
        float scale = 1.0f - i * 0.15f;
        setColorHSB(time + i * 0.1f, 0.8f, 0.9f);

        Path scaled;
        for (size_t j = 0; j < flower.size(); j++) {
            float x = flowerCx + (flower[j].x - flowerCx) * scale;
            float y = cy + (flower[j].y - cy) * scale;
            scaled.addVertex(x, y);
        }
        scaled.close();
        scaled.draw();
    }

    // Lissajous curve (right side, 1.5x size)
    float lissaCx = w * 0.75f;
    Path lissajous;
    int a = 3, b = 4;
    for (int i = 0; i <= 100; i++) {
        float t = (float)i / 100 * TAU;
        float x = lissaCx + 120 * sin(a * t + time);  // 1.5x
        float y = cy + 120 * sin(b * t);              // 1.5x
        lissajous.addVertex(x, y);
    }
    lissajous.close();

    setColor(colors::cyan);
    lissajous.draw();

    setColor(0.78f);
    drawBitmapString("Animated flower & Lissajous curve", 20, 120);
}

void tcApp::keyPressed(int key) {
    switch (key) {
        case '1': mode = 0; break;
        case '2': mode = 1; break;
        case '3': mode = 2; break;
        case 'c':
        case 'C':
            mousePolyline.clear();
            break;
    }
}
