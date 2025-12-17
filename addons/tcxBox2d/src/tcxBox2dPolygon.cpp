// =============================================================================
// tcxBox2dPolygon.cpp - Box2D ポリゴンボディ
// =============================================================================

#include "tcxBox2d/tcxBox2dPolygon.h"
#include <cmath>

namespace tcx::box2d {

Polygon::Polygon(Polygon&& other) noexcept
    : Body(std::move(other))
    , vertices_(std::move(other.vertices_))
{
}

Polygon& Polygon::operator=(Polygon&& other) noexcept {
    if (this != &other) {
        Body::operator=(std::move(other));
        vertices_ = std::move(other.vertices_);
    }
    return *this;
}

void Polygon::setup(World& world, const std::vector<tc::Vec2>& vertices, float cx, float cy) {
    if (vertices.size() < 3 || vertices.size() > 8) {
        // Box2Dは3〜8頂点のみサポート
        return;
    }

    world_ = &world;
    vertices_ = vertices;

    // ボディ定義
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = World::toBox2d(cx, cy);

    body_ = world.getWorld()->CreateBody(&bodyDef);

    // 頂点をBox2D形式に変換
    std::vector<b2Vec2> b2Vertices(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        b2Vertices[i] = World::toBox2d(vertices[i]);
    }

    // ポリゴン形状
    b2PolygonShape polygon;
    polygon.Set(b2Vertices.data(), static_cast<int32>(b2Vertices.size()));

    // フィクスチャ定義
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &polygon;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.restitution = 0.3f;

    body_->CreateFixture(&fixtureDef);

    // UserData に Body* を保存（World::getBodyAtPoint() で使用）
    body_->GetUserData().pointer = reinterpret_cast<uintptr_t>(this);

    // Node の初期位置を設定
    x = cx;
    y = cy;
}

void Polygon::setup(World& world, const tc::Polyline& polyline, float cx, float cy) {
    std::vector<tc::Vec2> vertices;
    for (int i = 0; i < polyline.size(); ++i) {
        vertices.push_back(tc::Vec2(polyline[i].x, polyline[i].y));
    }
    setup(world, vertices, cx, cy);
}

void Polygon::setupRegular(World& world, float cx, float cy, float radius, int sides) {
    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;

    std::vector<tc::Vec2> vertices(sides);
    float angleStep = 2.0f * M_PI / sides;

    for (int i = 0; i < sides; ++i) {
        float angle = i * angleStep - M_PI / 2;  // 上から開始
        vertices[i] = tc::Vec2(
            std::cos(angle) * radius,
            std::sin(angle) * radius
        );
    }

    setup(world, vertices, cx, cy);
}

// Node用: 原点(0,0)に描画（drawTree()が変換を適用する）
void Polygon::draw() {
    if (!body_ || vertices_.empty()) return;

    // ポリゴンを線で描画（原点中心）
    for (size_t i = 0; i < vertices_.size(); ++i) {
        size_t next = (i + 1) % vertices_.size();
        tc::drawLine(vertices_[i].x, vertices_[i].y,
                     vertices_[next].x, vertices_[next].y);
    }
}

void Polygon::drawFill() {
    if (!body_ || vertices_.empty()) return;

    // 三角形ファンで塗りつぶし（原点中心）
    tc::Mesh mesh;
    mesh.setMode(tc::PrimitiveMode::TriangleFan);

    // 中心点
    mesh.addVertex(tc::Vec3(0, 0, 0));

    // 頂点
    for (const auto& v : vertices_) {
        mesh.addVertex(tc::Vec3(v.x, v.y, 0));
    }
    // 最初の頂点に戻る
    mesh.addVertex(tc::Vec3(vertices_[0].x, vertices_[0].y, 0));

    mesh.draw();
}

void Polygon::draw(const tc::Color& color) {
    tc::setColor(color);
    draw();
}

} // namespace tcx::box2d
