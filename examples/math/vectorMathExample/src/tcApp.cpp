#include "tcApp.h"
#include <iostream>
#include <cstdlib>

using namespace std;

// ---------------------------------------------------------------------------
// HSB → RGB 変換
// ---------------------------------------------------------------------------
void tcApp::setColorHSB(float h, float s, float b, float a) {
    h = h - floor(h);  // 0-1 にクランプ
    float r, g, bl;

    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = b * (1 - s);
    float q = b * (1 - f * s);
    float t = b * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: r = b; g = t; bl = p; break;
        case 1: r = q; g = b; bl = p; break;
        case 2: r = p; g = b; bl = t; break;
        case 3: r = p; g = q; bl = b; break;
        case 4: r = t; g = p; bl = b; break;
        case 5: r = b; g = p; bl = q; break;
        default: r = g = bl = 0; break;
    }
    setColor(r, g, bl, a);
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void tcApp::setup() {
    cout << "03_math: Vector & Matrix Demo" << endl;
    cout << "  - Space: モード切り替え" << endl;
    cout << "  - Click: パーティクル生成" << endl;
    cout << "  - ESC: 終了" << endl;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void tcApp::update() {
    if (mode_ == 3) {
        updateParticles();
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void tcApp::draw() {
    clear(0.1f, 0.1f, 0.15f);

    switch (mode_) {
        case 0: drawVec2Demo(); break;
        case 1: drawRotationDemo(); break;
        case 2: drawLerpDemo(); break;
        case 3: drawParticleDemo(); break;
    }

    // 画面下部に操作説明
    setColor(0.5f, 0.5f, 0.5f);
    drawBitmapString("[SPACE] next mode  [Click] spawn particles (mode 3)  [ESC] quit", 20, getWindowHeight() - 20);
}

// ---------------------------------------------------------------------------
// Vec2 基本演算デモ
// ---------------------------------------------------------------------------
void tcApp::drawVec2Demo() {
    Vec2 center(640, 360);
    Vec2 mouse(getGlobalMouseX(), getGlobalMouseY());

    // マウスへのベクトル
    Vec2 toMouse = mouse - center;

    // 正規化
    Vec2 dir = toMouse.normalized();

    // 長さを表示
    float len = toMouse.length();

    // 角度
    float angle = toMouse.angle();

    // 中心から矢印を描画
    setColor(0.3f, 0.8f, 0.3f);
    drawLine(center.x, center.y, mouse.x, mouse.y);

    // 正規化ベクトル（固定長）を表示
    Vec2 normEnd = center + dir * 100;
    setColor(1.0f, 0.5f, 0.2f);
    drawLine(center.x, center.y, normEnd.x, normEnd.y);

    // 垂直ベクトル
    Vec2 perp = dir.perpendicular() * 50;
    setColor(0.2f, 0.5f, 1.0f);
    drawLine(center.x, center.y, center.x + perp.x, center.y + perp.y);
    drawLine(center.x, center.y, center.x - perp.x, center.y - perp.y);

    // 中心点
    setColor(1.0f, 1.0f, 1.0f);
    drawCircle(center.x, center.y, 8);

    // マウス点
    setColor(1.0f, 0.3f, 0.3f);
    drawCircle(mouse.x, mouse.y, 8);

    // 角度を円弧で表示
    setColor(1.0f, 1.0f, 0.3f, 0.5f);
    int segments = 32;
    float arcLen = fmin(len, 80.0f);
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * angle;
        float a2 = (float)(i + 1) / segments * angle;
        Vec2 p1 = center + Vec2::fromAngle(a1, arcLen);
        Vec2 p2 = center + Vec2::fromAngle(a2, arcLen);
        drawLine(p1.x, p1.y, p2.x, p2.y);
    }

    // タイトルと説明
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Mode 0: Vec2 Basic Operations", 20, 25);
    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Move mouse to see vector operations", 20, 45);

    // ベクトル情報
    char buf[128];
    setColor(0.3f, 0.8f, 0.3f);
    snprintf(buf, sizeof(buf), "toMouse: (%.1f, %.1f)", toMouse.x, toMouse.y);
    drawBitmapString(buf, 20, 80);
    snprintf(buf, sizeof(buf), "length: %.1f", len);
    drawBitmapString(buf, 20, 95);

    setColor(1.0f, 1.0f, 0.3f);
    snprintf(buf, sizeof(buf), "angle: %.2f rad (%.1f deg)", angle, angle * 180.0f / PI);
    drawBitmapString(buf, 20, 115);

    setColor(1.0f, 0.5f, 0.2f);
    snprintf(buf, sizeof(buf), "normalized: (%.2f, %.2f)", dir.x, dir.y);
    drawBitmapString(buf, 20, 135);

    setColor(0.2f, 0.5f, 1.0f);
    snprintf(buf, sizeof(buf), "perpendicular: (%.2f, %.2f)", perp.x / 50, perp.y / 50);
    drawBitmapString(buf, 20, 155);
}

// ---------------------------------------------------------------------------
// 回転デモ
// ---------------------------------------------------------------------------
void tcApp::drawRotationDemo() {
    double t = getElapsedTime();
    Vec2 center(640, 360);

    // 回転する点群
    int numPoints = 12;
    float baseRadius = 150;

    for (int i = 0; i < numPoints; i++) {
        float baseAngle = (float)i / numPoints * TAU;
        Vec2 point = Vec2::fromAngle(baseAngle, baseRadius);

        // 時間で回転
        point = point.rotated((float)t * 0.5f);

        // 各点を中心にさらに小さい円を回転
        int numSub = 6;
        float subRadius = 30;
        for (int j = 0; j < numSub; j++) {
            float subAngle = (float)j / numSub * TAU;
            Vec2 subPoint = Vec2::fromAngle(subAngle, subRadius);
            subPoint = subPoint.rotated((float)t * 2.0f + baseAngle);

            Vec2 finalPos = center + point + subPoint;

            setColorHSB((float)i / numPoints, 0.7f, 1.0f, 0.8f);
            drawCircle(finalPos.x, finalPos.y, 5);
        }

        // メインの点
        Vec2 mainPos = center + point;
        setColorHSB((float)i / numPoints, 0.5f, 1.0f);
        drawCircle(mainPos.x, mainPos.y, 10);
    }

    // 中心
    setColor(1.0f, 1.0f, 1.0f);
    drawCircle(center.x, center.y, 5);

    // タイトルと説明
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Mode 1: Vec2 Rotation", 20, 25);
    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Vec2::fromAngle() creates vectors from angle", 20, 45);
    drawBitmapString("Vec2::rotated() rotates vectors around origin", 20, 60);

    char buf[64];
    setColor(0.8f, 0.8f, 0.8f);
    snprintf(buf, sizeof(buf), "time: %.1f sec", t);
    drawBitmapString(buf, 20, 90);
}

// ---------------------------------------------------------------------------
// 線形補間デモ
// ---------------------------------------------------------------------------
void tcApp::drawLerpDemo() {
    double t = getElapsedTime();
    Vec2 mouse(getGlobalMouseX(), getGlobalMouseY());

    // 複数の点が lerp でマウスを追いかける
    static vector<Vec2> followers;
    if (followers.empty()) {
        for (int i = 0; i < 20; i++) {
            followers.push_back(Vec2(640, 360));
        }
    }

    // 最初の点はマウスを直接追いかける
    followers[0] = followers[0].lerp(mouse, 0.1f);

    // 後続の点は前の点を追いかける
    for (size_t i = 1; i < followers.size(); i++) {
        float lerpAmount = 0.15f - (float)i * 0.005f;
        followers[i] = followers[i].lerp(followers[i - 1], lerpAmount);
    }

    // 描画
    for (size_t i = 0; i < followers.size(); i++) {
        float ratio = (float)i / followers.size();
        setColorHSB(ratio * 0.3f + (float)t * 0.1f, 0.8f, 1.0f, 1.0f - ratio * 0.5f);

        float radius = 20.0f - (float)i * 0.8f;
        drawCircle(followers[i].x, followers[i].y, radius);
    }

    // 線で結ぶ
    setColor(1.0f, 1.0f, 1.0f, 0.3f);
    for (size_t i = 1; i < followers.size(); i++) {
        drawLine(followers[i - 1].x, followers[i - 1].y,
                    followers[i].x, followers[i].y);
    }

    // タイトルと説明
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Mode 2: Vec2 Lerp (Linear Interpolation)", 20, 25);
    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Move mouse - circles follow with easing", 20, 45);
    drawBitmapString("Vec2::lerp(target, amount) blends positions", 20, 60);

    char buf[64];
    setColor(0.8f, 0.8f, 0.8f);
    snprintf(buf, sizeof(buf), "followers: %zu", followers.size());
    drawBitmapString(buf, 20, 90);
}

// ---------------------------------------------------------------------------
// パーティクルデモ
// ---------------------------------------------------------------------------
void tcApp::drawParticleDemo() {
    // 自動でパーティクル生成
    double t = getElapsedTime();
    if (fmod(t, 0.05) < getDeltaTime()) {
        float x = 640 + cos(t * 2) * 200;
        float y = 360 + sin(t * 3) * 150;
        spawnParticle(x, y);
    }

    drawParticles();

    // タイトルと説明
    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("Mode 3: Particle System with Vec2", 20, 25);
    setColor(0.7f, 0.7f, 0.7f);
    drawBitmapString("Click to spawn particles", 20, 45);
    drawBitmapString("Uses Vec2 for pos, vel, acc (physics)", 20, 60);

    char buf[64];
    setColor(0.8f, 0.8f, 0.8f);
    snprintf(buf, sizeof(buf), "particles: %zu", particles_.size());
    drawBitmapString(buf, 20, 90);
}

void tcApp::spawnParticle(float x, float y) {
    Particle p;
    p.pos = Vec2(x, y);

    // ランダムな方向
    float angle = (float)rand() / RAND_MAX * TAU;
    float speed = 50 + (float)rand() / RAND_MAX * 100;
    p.vel = Vec2::fromAngle(angle, speed);

    p.acc = Vec2(0, 50);  // 重力
    p.radius = 5 + (float)rand() / RAND_MAX * 10;
    p.hue = (float)rand() / RAND_MAX;
    p.life = 2.0f + (float)rand() / RAND_MAX * 2.0f;
    p.maxLife = p.life;

    particles_.push_back(p);
}

void tcApp::updateParticles() {
    float dt = (float)getDeltaTime();

    for (auto& p : particles_) {
        // 速度に加速度を加算
        p.vel += p.acc * dt;

        // 位置に速度を加算
        p.pos += p.vel * dt;

        // 寿命を減らす
        p.life -= dt;

        // 速度を制限
        p.vel.limit(300);
    }

    // 死んだパーティクルを削除
    particles_.erase(
        remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return p.life <= 0; }),
        particles_.end()
    );
}

void tcApp::drawParticles() {
    for (const auto& p : particles_) {
        float lifeRatio = p.life / p.maxLife;
        setColorHSB(p.hue, 0.8f, 1.0f, lifeRatio);
        drawCircle(p.pos.x, p.pos.y, p.radius * lifeRatio);
    }
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------
void tcApp::keyPressed(int key) {
    if (key == KEY_ESCAPE) {
        sapp_request_quit();
    }
    else if (key == KEY_SPACE) {
        mode_ = (mode_ + 1) % NUM_MODES;
        cout << "Mode: " << mode_ << endl;
    }
}

void tcApp::mousePressed(int x, int y, int button) {
    if (mode_ == 3) {
        // パーティクルを生成
        for (int i = 0; i < 20; i++) {
            spawnParticle((float)x, (float)y);
        }
    }
}
