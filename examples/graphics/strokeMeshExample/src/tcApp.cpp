#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("strokeMeshExample");

    // 3x3のグリッドで全組み合わせを表示
    // 行: Cap (BUTT, ROUND, SQUARE)
    // 列: Join (MITER, ROUND, BEVEL)

    float gridLeft = 80;
    float gridTop = 60;
    float colWidth = 200;
    float rowHeight = 170;
    float headerHeight = 25;
    float labelWidth = 70;

    for (int cap = 0; cap < 3; cap++) {
        for (int join = 0; join < 3; join++) {
            StrokeMesh stroke;

            // 折れ線を作成（角が見えるように）
            float cx = gridLeft + labelWidth + join * colWidth + colWidth / 2;
            float cy = gridTop + headerHeight + cap * rowHeight + rowHeight / 2;

            stroke.addVertex(cx - 80, cy);
            stroke.addVertex(cx - 20, cy - 50);
            stroke.addVertex(cx + 20, cy + 50);
            stroke.addVertex(cx + 80, cy);

            stroke.setWidth(strokeWidth);
            stroke.setCapType((StrokeMesh::CapType)cap);
            stroke.setJoinType((StrokeMesh::JoinType)join);

            // 色分け（HSBで色相を変える）
            float hue = (cap * 3 + join) * 0.07f;
            stroke.setColor(colorFromHSB(hue, 0.78f, 1.0f));

            stroke.update();
            strokes.push_back(stroke);
        }
    }

    // 閉じた形状も追加（星型）
    float starX = gridLeft + labelWidth + 3 * colWidth + colWidth / 2;
    for (int cap = 0; cap < 3; cap++) {
        StrokeMesh stroke;

        Polyline star;
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

    // 可変幅ストロークのデモ（画面下部）
    variableStroke.addVertexWithWidth(100, 680, 5.0f);   // 細い
    variableStroke.addVertexWithWidth(250, 640, 30.0f);  // 太い
    variableStroke.addVertexWithWidth(400, 680, 10.0f);  // 中くらい
    variableStroke.addVertexWithWidth(550, 640, 40.0f);  // もっと太い
    variableStroke.addVertexWithWidth(700, 680, 5.0f);   // また細い
    variableStroke.addVertexWithWidth(850, 640, 25.0f);  // 太い
    variableStroke.setColor(colors::white);
    variableStroke.setCapType(StrokeMesh::CAP_ROUND);
    variableStroke.setJoinType(StrokeMesh::JOIN_ROUND);
    variableStroke.update();
}

void tcApp::update() {
}

void tcApp::draw() {
    clear(0);

    float gridLeft = 80;
    float gridTop = 60;
    float colWidth = 200;
    float rowHeight = 170;
    float headerHeight = 25;
    float labelWidth = 70;

    // グリッド線を描画
    setColor(50);
    // 縦線
    drawLine(gridLeft, gridTop, gridLeft, gridTop + headerHeight + rowHeight * 3);
    drawLine(gridLeft + labelWidth, gridTop, gridLeft + labelWidth, gridTop + headerHeight + rowHeight * 3);
    for (int j = 1; j <= 4; j++) {
        float x = gridLeft + labelWidth + j * colWidth;
        drawLine(x, gridTop, x, gridTop + headerHeight + rowHeight * 3);
    }
    // 横線
    drawLine(gridLeft, gridTop, gridLeft + labelWidth + colWidth * 4, gridTop);
    drawLine(gridLeft, gridTop + headerHeight, gridLeft + labelWidth + colWidth * 4, gridTop + headerHeight);
    for (int c = 1; c <= 3; c++) {
        float y = gridTop + headerHeight + c * rowHeight;
        drawLine(gridLeft, y, gridLeft + labelWidth + colWidth * 4, y);
    }

    // ヘッダー背景
    setColor(40);
    drawRect(gridLeft + 1, gridTop + 1, labelWidth + colWidth * 4 - 2, headerHeight - 1);
    drawRect(gridLeft + 1, gridTop + headerHeight + 1, labelWidth - 1, rowHeight * 3 - 2);

    // 列ラベル (Join Type)
    setColor(255);
    const char* joinNames[] = {"MITER", "ROUND", "BEVEL", "CLOSED"};
    for (int j = 0; j < 4; j++) {
        float x = gridLeft + labelWidth + j * colWidth + colWidth / 2 - 20;
        drawBitmapString(joinNames[j], x, gridTop + 6);
    }

    // 行ラベル (Cap Type)
    const char* capNames[] = {"BUTT", "ROUND", "SQUARE"};
    for (int c = 0; c < 3; c++) {
        float y = gridTop + headerHeight + c * rowHeight + rowHeight / 2 - 5;
        drawBitmapString(capNames[c], gridLeft + 8, y);
    }

    // 左上コーナーのラベル
    setColor(120);
    drawBitmapString("Cap", gridLeft + 15, gridTop + 10);
    drawBitmapString("Join", gridLeft + 30, gridTop);

    // ストロークを描画
    for (auto& s : strokes) {
        s.draw();
    }

    // 閉じた形状を描画
    for (auto& s : closedStrokes) {
        s.draw();
    }

    // 可変幅ストロークを描画
    variableStroke.draw();
    setColor(150);
    drawBitmapString("Variable Width Stroke", 400, 710);

    // 情報
    setColor(255);
    drawBitmapString("Width: " + std::to_string((int)strokeWidth), 10, 20);
}
