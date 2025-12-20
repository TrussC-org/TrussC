// =============================================================================
// box2dNodeExample - tcxBox2d + Node 統合サンプル
// =============================================================================
// Body は tc::Node を継承しており、Node ツリーに統合できる。
// shared_ptr で管理し、root.addChild() で追加すると、
// root.updateTree() / root.drawTree() で一括処理できる。
//
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

    // ルートノード（shared_ptr で作成）
    std::shared_ptr<Node> root = std::make_shared<Node>();

    // Body たちを shared_ptr で保持
    std::vector<std::shared_ptr<Circle>> circles;
    std::vector<std::shared_ptr<Rect>> rects;
    std::vector<std::shared_ptr<PolyShape>> polygons;

    void setup() override {
        // 物理ワールドを初期化（重力: 下向き300px/sec^2）
        world.setup(Vec2(0, 300));

        // 画面端に壁を作成
        world.createBounds();

        // 初期オブジェクトを配置
        for (int i = 0; i < 3; ++i) {
            addCircle(200 + i * 100, 100, 25 + i * 10);
        }

        for (int i = 0; i < 2; ++i) {
            addRect(250 + i * 150, 200, 50, 30);
        }

        // 正六角形を追加
        addPolygon(400, 50, 30, 6);
    }

    void addCircle(float x, float y, float radius) {
        auto circle = std::make_shared<Circle>();
        circle->setup(world, x, y, radius);
        circle->setRestitution(0.7f);

        root->addChild(circle);  // Node ツリーに追加
        circles.push_back(circle);
    }

    void addRect(float x, float y, float w, float h) {
        auto rect = std::make_shared<Rect>();
        rect->setup(world, x, y, w, h);
        rect->setRestitution(0.3f);

        root->addChild(rect);
        rects.push_back(rect);
    }

    void addPolygon(float x, float y, float radius, int sides) {
        auto poly = std::make_shared<PolyShape>();
        poly->setupRegular(world, x, y, radius, sides);
        poly->setRestitution(0.5f);

        root->addChild(poly);
        polygons.push_back(poly);
    }

    void update() override {
        // 物理シミュレーションを進める
        world.update();

        // root.updateTree() で全ての子ノードの update() が呼ばれる
        // Body::update() が Box2D の位置を Node の x, y, rotation に同期
        root->updateTree();
    }

    void draw() override {
        clear(30);

        // root.drawTree() で全ての子ノードを一括描画
        // 各 Body の drawTree() が自動的に x, y, rotation を適用
        // 色は draw() 内で設定するか、描画前に設定

        // 色を種類ごとに設定するため、個別に drawTree
        setColor(1.0f, 0.78f, 0.4f);
        for (auto& circle : circles) {
            circle->drawTree();
        }

        setColor(0.4f, 0.78f, 1.0f);
        for (auto& rect : rects) {
            rect->drawTree();
        }

        setColor(0.78f, 0.4f, 1.0f);
        for (auto& poly : polygons) {
            poly->drawTree();
        }

        // 使い方の表示
        setColor(1.0f);
        drawBitmapString("Left click: Add circle", 10, 20);
        drawBitmapString("Right click: Add rectangle", 10, 36);
        drawBitmapString("Middle click: Add hexagon", 10, 52);
        drawBitmapString("C: Clear all", 10, 68);
        drawBitmapString("Bodies: " + std::to_string(world.getBodyCount()), 10, 84);
        drawBitmapString("Node children: " + std::to_string(root->getChildCount()), 10, 100);

        // Node 座標の表示（デバッグ用）
        if (!circles.empty()) {
            auto& c = circles[0];
            drawBitmapString("Circle[0] Node x,y: " +
                std::to_string((int)c->x) + ", " + std::to_string((int)c->y), 10, 126);
        }
    }

    void mousePressed(int x, int y, int button) override {
        if (button == MOUSE_BUTTON_LEFT) {
            addCircle((float)x, (float)y, randomFloat(15, 40));
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            addRect((float)x, (float)y, randomFloat(30, 60), randomFloat(20, 40));
        }
        else if (button == MOUSE_BUTTON_MIDDLE) {
            // 中クリック: 正多角形を追加（3〜8角形ランダム）
            int sides = 3 + rand() % 6;
            addPolygon((float)x, (float)y, randomFloat(20, 40), sides);
        }
    }

    void keyPressed(int key) override {
        if (key == 'c' || key == 'C') {
            // Cキー: 全てクリア
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
