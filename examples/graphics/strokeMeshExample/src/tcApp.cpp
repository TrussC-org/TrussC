#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("strokeMeshExample");

    // Display all combinations in 3x3 grid
    // Row: Cap (BUTT, ROUND, SQUARE)
    // Column: Join (MITER, ROUND, BEVEL)

    float gridLeft = 80;
    float gridTop = 60;
    float colWidth = 200;
    float rowHeight = 170;
    float headerHeight = 25;
    float labelWidth = 70;

    for (int cap = 0; cap < 3; cap++) {
        for (int join = 0; join < 3; join++) {
            StrokeMesh stroke;

            // Create polyline (to show corners)
            float cx = gridLeft + labelWidth + join * colWidth + colWidth / 2;
            float cy = gridTop + headerHeight + cap * rowHeight + rowHeight / 2;

            stroke.addVertex(cx - 80, cy);
            stroke.addVertex(cx - 20, cy - 50);
            stroke.addVertex(cx + 20, cy + 50);
            stroke.addVertex(cx + 80, cy);

            stroke.setWidth(strokeWidth);
            stroke.setCapType((StrokeMesh::CapType)cap);
            stroke.setJoinType((StrokeMesh::JoinType)join);

            // Color variation (change hue with HSB)
            float hue = (cap * 3 + join) * 0.07f;
            stroke.setColor(colorFromHSB(hue, 0.78f, 1.0f));

            stroke.update();
            strokes.push_back(stroke);
        }
    }

    // Add closed shape (star)
    float starX = gridLeft + labelWidth + 3 * colWidth + colWidth / 2;
    for (int cap = 0; cap < 3; cap++) {
        StrokeMesh stroke;

        Path star;
        float cy = gridTop + headerHeight + cap * rowHeight + rowHeight / 2;
        float outerR = 60, innerR = 25;
        int points = 5;
        for (int i = 0; i < points * 2; i++) {
            float angle = i * PI / points - QUARTER_TAU;
            float r = (i % 2 == 0) ? outerR : innerR;
            star.addVertex(starX + cos(angle) * r, cy + sin(angle) * r);
        }
        star.close();

        stroke.setShape(star);
        stroke.setWidth(strokeWidth);
        stroke.setJoinType((StrokeMesh::JoinType)cap);
        stroke.setColor(colorFromHSB(0.55f + cap * 0.05f, 0.78f, 1.0f));
        stroke.update();

        closedStrokes.push_back(stroke);
    }

    // Variable width stroke demo (bottom of screen)
    variableStroke.addVertexWithWidth(100, 680, 5.0f);   // Thin
    variableStroke.addVertexWithWidth(250, 640, 30.0f);  // Thick
    variableStroke.addVertexWithWidth(400, 680, 10.0f);  // Medium
    variableStroke.addVertexWithWidth(550, 640, 40.0f);  // Thicker
    variableStroke.addVertexWithWidth(700, 680, 5.0f);   // Thin again
    variableStroke.addVertexWithWidth(850, 640, 25.0f);  // Thick
    variableStroke.setColor(colors::white);
    variableStroke.setCapType(StrokeMesh::CAP_ROUND);
    variableStroke.setJoinType(StrokeMesh::JOIN_ROUND);
    variableStroke.update();
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0.0f);

    float gridLeft = 80;
    float gridTop = 60;
    float colWidth = 200;
    float rowHeight = 170;
    float headerHeight = 25;
    float labelWidth = 70;

    // Draw grid lines
    setColor(0.2f);
    // Vertical lines
    drawLine(gridLeft, gridTop, gridLeft, gridTop + headerHeight + rowHeight * 3);
    drawLine(gridLeft + labelWidth, gridTop, gridLeft + labelWidth, gridTop + headerHeight + rowHeight * 3);
    for (int j = 1; j <= 4; j++) {
        float x = gridLeft + labelWidth + j * colWidth;
        drawLine(x, gridTop, x, gridTop + headerHeight + rowHeight * 3);
    }
    // Horizontal lines
    drawLine(gridLeft, gridTop, gridLeft + labelWidth + colWidth * 4, gridTop);
    drawLine(gridLeft, gridTop + headerHeight, gridLeft + labelWidth + colWidth * 4, gridTop + headerHeight);
    for (int c = 1; c <= 3; c++) {
        float y = gridTop + headerHeight + c * rowHeight;
        drawLine(gridLeft, y, gridLeft + labelWidth + colWidth * 4, y);
    }

    // Header background
    setColor(0.16f);
    drawRect(gridLeft + 1, gridTop + 1, labelWidth + colWidth * 4 - 2, headerHeight - 1);
    drawRect(gridLeft + 1, gridTop + headerHeight + 1, labelWidth - 1, rowHeight * 3 - 2);

    // Column labels (Join Type)
    // y is baseline position for drawBitmapString, add character height for visual position
    const float charH = bitmapfont::CHAR_TEX_HEIGHT;
    setColor(1.0f);
    const char* joinNames[] = {"MITER", "ROUND", "BEVEL", "CLOSED"};
    for (int j = 0; j < 4; j++) {
        float x = gridLeft + labelWidth + j * colWidth + colWidth / 2 - 20;
        drawBitmapString(joinNames[j], x, gridTop + 6 + charH);
    }

    // Row labels (Cap Type)
    const char* capNames[] = {"BUTT", "ROUND", "SQUARE"};
    for (int c = 0; c < 3; c++) {
        float y = gridTop + headerHeight + c * rowHeight + rowHeight / 2 - 5 + charH;
        drawBitmapString(capNames[c], gridLeft + 8, y);
    }

    // Top-left corner label
    setColor(0.47f);
    drawBitmapString("Cap", gridLeft + 15, gridTop + 10 + charH);
    drawBitmapString("Join", gridLeft + 30, gridTop + charH);

    // Draw strokes
    for (auto& s : strokes) {
        s.draw();
    }

    // Draw closed shapes
    for (auto& s : closedStrokes) {
        s.draw();
    }

    // Draw variable width stroke
    variableStroke.draw();
    setColor(0.6f);
    drawBitmapString("Variable Width Stroke", 400, 702 + charH);

    // Info
    setColor(1.0f);
    drawBitmapString("Width: " + std::to_string((int)strokeWidth), 10, 20 + charH);
}
