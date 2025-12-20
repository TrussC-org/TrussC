// =============================================================================
// box2dBasicExample - tcxBox2d アドオンの基本サンプル
// =============================================================================
// クリックで円を生成、物理シミュレーションで落下する。
// Node ツリーは使わず、各 Body を個別に描画するシンプルな例。
// =============================================================================

#include "tcBaseApp.h"
#include <tcxBox2d.h>
#include <vector>
#include <memory>
#include <cstdlib>

using namespace tc;
using namespace tcx::box2d;

// ランダムな float を生成（min 〜 max）
float randomFloat(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

class tcApp : public tc::App {
public:
    World world;
    std::vector<std::shared_ptr<Circle>> circles;
    std::vector<std::shared_ptr<Rect>> rects;

    void setup() override {
        // 物理ワールドを初期化（重力: 下向き300px/sec^2）
        world.setup(Vec2(0, 300));

        // 画面端に壁を作成
        world.createBounds();

        // 初期オブジェクトを配置（ごちゃっと積む）
        auto c1 = std::make_shared<Circle>();
        c1->setup(world, 350, 80, 30);
        circles.push_back(c1);

        auto c2 = std::make_shared<Circle>();
        c2->setup(world, 380, 150, 25);
        circles.push_back(c2);

        auto c3 = std::make_shared<Circle>();
        c3->setup(world, 340, 200, 35);
        circles.push_back(c3);

        auto c4 = std::make_shared<Circle>();
        c4->setup(world, 400, 120, 20);
        circles.push_back(c4);

        auto c5 = std::make_shared<Circle>();
        c5->setup(world, 360, 250, 28);
        circles.push_back(c5);
    }

    void update() override {
        // 物理シミュレーションを進める
        world.update();

        // Box2D の位置を Node の x, y, rotation に同期
        for (auto& c : circles) c->updateTree();
        for (auto& r : rects) r->updateTree();
    }

    void draw() override {
        clear(30);

        // 全ての円を描画（drawTree で位置・回転を適用）
        setColor(1.0f, 0.78f, 0.4f);
        for (auto& circle : circles) {
            circle->drawTree();
        }

        // 全ての矩形を描画
        setColor(0.4f, 0.78f, 1.0f);
        for (auto& rect : rects) {
            rect->drawTree();
        }

        // ドラッグ中ならバネの線を描画
        if (world.isDragging()) {
            Vec2 anchor = world.getDragAnchor();
            setColor(1.0f, 0.4f, 0.4f);
            drawLine(anchor.x, anchor.y, getMouseX(), getMouseY());
        }

        // 使い方の表示
        setColor(1.0f);
        drawBitmapString("Left click: Add circle / Drag body", 10, 20);
        drawBitmapString("Right click: Add rectangle", 10, 36);
        drawBitmapString("C: Clear all", 10, 52);
        drawBitmapString("Bodies: " + std::to_string(world.getBodyCount()), 10, 68);
    }

    void mousePressed(int x, int y, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            // 既存のボディがあればドラッグ開始
            Body* body = world.getBodyAtPoint((float)x, (float)y);
            if (body) {
                world.startDrag(body, (float)x, (float)y);
            } else {
                // なければ円を追加
                auto circle = std::make_shared<Circle>();
                circle->setup(world, (float)x, (float)y, randomFloat(15, 40));
                circle->setRestitution(0.7f);  // よく跳ねる
                circles.push_back(circle);
            }
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            // 右クリック: 矩形を追加
            auto rect = std::make_shared<Rect>();
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
            // Cキー: 全てクリア
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
