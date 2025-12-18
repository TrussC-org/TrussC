#pragma once

// このファイルは TrussC.h からインクルードされる

#include <vector>

namespace trussc {

// プリミティブモード
enum class PrimitiveMode {
    Triangles,
    TriangleStrip,
    TriangleFan,
    Lines,
    LineStrip,
    LineLoop,
    Points
};

// Mesh - 頂点、カラー、インデックスを持つクラス
class Mesh {
public:
    Mesh() : mode_(PrimitiveMode::Triangles) {}

    // モード設定
    void setMode(PrimitiveMode mode) {
        mode_ = mode;
    }

    PrimitiveMode getMode() const {
        return mode_;
    }

    // ---------------------------------------------------------------------------
    // 頂点
    // ---------------------------------------------------------------------------
    void addVertex(float x, float y, float z = 0.0f) {
        vertices_.push_back(Vec3{x, y, z});
    }

    void addVertex(const Vec2& v) {
        vertices_.push_back(Vec3{v.x, v.y, 0.0f});
    }

    void addVertex(const Vec3& v) {
        vertices_.push_back(v);
    }

    void addVertices(const std::vector<Vec3>& verts) {
        for (const auto& v : verts) {
            vertices_.push_back(v);
        }
    }

    std::vector<Vec3>& getVertices() { return vertices_; }
    const std::vector<Vec3>& getVertices() const { return vertices_; }
    int getNumVertices() const { return static_cast<int>(vertices_.size()); }

    // ---------------------------------------------------------------------------
    // カラー（頂点カラー）
    // ---------------------------------------------------------------------------
    void addColor(const Color& c) {
        colors_.push_back(c);
    }

    void addColor(float r, float g, float b, float a = 1.0f) {
        colors_.push_back(Color{r, g, b, a});
    }

    void addColors(const std::vector<Color>& cols) {
        for (const auto& c : cols) {
            colors_.push_back(c);
        }
    }

    std::vector<Color>& getColors() { return colors_; }
    const std::vector<Color>& getColors() const { return colors_; }
    int getNumColors() const { return static_cast<int>(colors_.size()); }
    bool hasColors() const { return !colors_.empty(); }

    // ---------------------------------------------------------------------------
    // インデックス
    // ---------------------------------------------------------------------------
    void addIndex(unsigned int index) {
        indices_.push_back(index);
    }

    void addIndices(const std::vector<unsigned int>& inds) {
        for (auto i : inds) {
            indices_.push_back(i);
        }
    }

    // 三角形を追加（3つのインデックス）
    void addTriangle(unsigned int i0, unsigned int i1, unsigned int i2) {
        indices_.push_back(i0);
        indices_.push_back(i1);
        indices_.push_back(i2);
    }

    std::vector<unsigned int>& getIndices() { return indices_; }
    const std::vector<unsigned int>& getIndices() const { return indices_; }
    int getNumIndices() const { return static_cast<int>(indices_.size()); }
    bool hasIndices() const { return !indices_.empty(); }

    // ---------------------------------------------------------------------------
    // 法線（ライティング用）
    // ---------------------------------------------------------------------------
    void addNormal(float nx, float ny, float nz) {
        normals_.push_back(Vec3{nx, ny, nz});
    }

    void addNormal(const Vec3& n) {
        normals_.push_back(n);
    }

    void addNormals(const std::vector<Vec3>& norms) {
        for (const auto& n : norms) {
            normals_.push_back(n);
        }
    }

    void setNormal(size_t index, const Vec3& n) {
        if (index < normals_.size()) {
            normals_[index] = n;
        }
    }

    Vec3 getNormal(size_t index) const {
        if (index < normals_.size()) {
            return normals_[index];
        }
        return Vec3{0, 0, 1};  // デフォルト: Z方向
    }

    std::vector<Vec3>& getNormals() { return normals_; }
    const std::vector<Vec3>& getNormals() const { return normals_; }
    int getNumNormals() const { return static_cast<int>(normals_.size()); }
    bool hasNormals() const { return !normals_.empty(); }

    // ---------------------------------------------------------------------------
    // テクスチャ座標
    // ---------------------------------------------------------------------------
    void addTexCoord(float u, float v) {
        texCoords_.push_back(Vec2{u, v});
    }

    void addTexCoord(const Vec2& t) {
        texCoords_.push_back(t);
    }

    std::vector<Vec2>& getTexCoords() { return texCoords_; }
    const std::vector<Vec2>& getTexCoords() const { return texCoords_; }
    bool hasTexCoords() const { return !texCoords_.empty(); }

    // ---------------------------------------------------------------------------
    // クリア
    // ---------------------------------------------------------------------------
    void clear() {
        vertices_.clear();
        normals_.clear();
        colors_.clear();
        indices_.clear();
        texCoords_.clear();
    }

    void clearVertices() { vertices_.clear(); }
    void clearNormals() { normals_.clear(); }
    void clearColors() { colors_.clear(); }
    void clearIndices() { indices_.clear(); }
    void clearTexCoords() { texCoords_.clear(); }

    // ---------------------------------------------------------------------------
    // 描画
    // ---------------------------------------------------------------------------
    void draw() const {
        if (vertices_.empty()) return;

        // ライティングが有効で法線がある場合はライティング付き描画
        if (internal::lightingEnabled && hasNormals() &&
            normals_.size() >= vertices_.size() && internal::currentMaterial) {
            drawWithLighting();
            return;
        }

        // 通常描画
        drawNoLighting();
    }

    // ライティングなしの通常描画
    void drawNoLighting() const {
        if (vertices_.empty()) return;

        bool useColors = hasColors() && colors_.size() >= vertices_.size();
        bool useIndices = hasIndices();
        Color defColor = getDefaultContext().getColor();

        // sokol_gl の描画モード開始
        switch (mode_) {
            case PrimitiveMode::Triangles:
                sgl_begin_triangles();
                break;
            case PrimitiveMode::TriangleStrip:
                sgl_begin_triangle_strip();
                break;
            case PrimitiveMode::TriangleFan:
                // sokol_gl には triangle_fan がないので triangles で代用
                drawTriangleFan(useColors, useIndices);
                return;
            case PrimitiveMode::Lines:
                sgl_begin_lines();
                break;
            case PrimitiveMode::LineStrip:
                sgl_begin_line_strip();
                break;
            case PrimitiveMode::LineLoop:
                // sokol_gl には line_loop がないので line_strip + 閉じる
                drawLineLoop(useColors, useIndices);
                return;
            case PrimitiveMode::Points:
                sgl_begin_points();
                break;
        }

        // 頂点を追加
        if (useIndices) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    if (useColors) {
                        sgl_c4f(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                    } else {
                        sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    sgl_v3f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                }
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (useColors) {
                    sgl_c4f(colors_[i].r, colors_[i].g, colors_[i].b, colors_[i].a);
                } else {
                    sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
        }

        sgl_end();
    }

    // ライティング付き描画（CPU側でライティング計算）
    void drawWithLighting() const {
        if (mode_ != PrimitiveMode::Triangles) {
            // 現在は三角形モードのみサポート
            drawNoLighting();
            return;
        }

        // 現在の変換行列を取得
        Mat4 modelMatrix = getDefaultContext().getCurrentMatrix();

        // 法線変換用行列（逆転置行列）
        // 簡易実装: スケールが均一なら modelMatrix をそのまま使える
        // TODO: 不均一スケールに対応する場合は逆転置行列を計算

        const Material& material = *internal::currentMaterial;

        sgl_begin_triangles();

        if (hasIndices()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size() && idx < normals_.size()) {
                    const Vec3& localPos = vertices_[idx];
                    const Vec3& localNormal = normals_[idx];

                    // ワールド座標に変換
                    Vec3 worldPos = modelMatrix * localPos;

                    // 法線を変換（回転のみ適用、平行移動は無視）
                    Vec3 worldNormal;
                    worldNormal.x = modelMatrix.m[0] * localNormal.x +
                                    modelMatrix.m[1] * localNormal.y +
                                    modelMatrix.m[2] * localNormal.z;
                    worldNormal.y = modelMatrix.m[4] * localNormal.x +
                                    modelMatrix.m[5] * localNormal.y +
                                    modelMatrix.m[6] * localNormal.z;
                    worldNormal.z = modelMatrix.m[8] * localNormal.x +
                                    modelMatrix.m[9] * localNormal.y +
                                    modelMatrix.m[10] * localNormal.z;

                    // 法線を正規化
                    float len = std::sqrt(worldNormal.x * worldNormal.x +
                                          worldNormal.y * worldNormal.y +
                                          worldNormal.z * worldNormal.z);
                    if (len > 0.0001f) {
                        worldNormal.x /= len;
                        worldNormal.y /= len;
                        worldNormal.z /= len;
                    }

                    // ライティング計算
                    Color litColor = calculateLighting(worldPos, worldNormal, material);

                    sgl_c4f(litColor.r, litColor.g, litColor.b, litColor.a);
                    sgl_v3f(localPos.x, localPos.y, localPos.z);
                }
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (i < normals_.size()) {
                    const Vec3& localPos = vertices_[i];
                    const Vec3& localNormal = normals_[i];

                    // ワールド座標に変換
                    Vec3 worldPos = modelMatrix * localPos;

                    // 法線を変換
                    Vec3 worldNormal;
                    worldNormal.x = modelMatrix.m[0] * localNormal.x +
                                    modelMatrix.m[1] * localNormal.y +
                                    modelMatrix.m[2] * localNormal.z;
                    worldNormal.y = modelMatrix.m[4] * localNormal.x +
                                    modelMatrix.m[5] * localNormal.y +
                                    modelMatrix.m[6] * localNormal.z;
                    worldNormal.z = modelMatrix.m[8] * localNormal.x +
                                    modelMatrix.m[9] * localNormal.y +
                                    modelMatrix.m[10] * localNormal.z;

                    // 法線を正規化
                    float len = std::sqrt(worldNormal.x * worldNormal.x +
                                          worldNormal.y * worldNormal.y +
                                          worldNormal.z * worldNormal.z);
                    if (len > 0.0001f) {
                        worldNormal.x /= len;
                        worldNormal.y /= len;
                        worldNormal.z /= len;
                    }

                    // ライティング計算
                    Color litColor = calculateLighting(worldPos, worldNormal, material);

                    sgl_c4f(litColor.r, litColor.g, litColor.b, litColor.a);
                    sgl_v3f(localPos.x, localPos.y, localPos.z);
                }
            }
        }

        sgl_end();
    }

    // ワイヤーフレーム描画（三角形のエッジをラインで描画）
    void drawWireframe() const {
        if (vertices_.empty()) return;

        // 現在のモードが三角形系でない場合は通常のdrawを使う
        if (mode_ != PrimitiveMode::Triangles &&
            mode_ != PrimitiveMode::TriangleStrip &&
            mode_ != PrimitiveMode::TriangleFan) {
            draw();
            return;
        }

        Color defColor = getDefaultContext().getColor();
        sgl_begin_lines();

        if (hasIndices()) {
            // インデックスを使う場合、三角形ごとにエッジを描画
            for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
                unsigned int i0 = indices_[i];
                unsigned int i1 = indices_[i + 1];
                unsigned int i2 = indices_[i + 2];

                if (i0 < vertices_.size() && i1 < vertices_.size() && i2 < vertices_.size()) {
                    sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);

                    // エッジ 0-1
                    sgl_v3f(vertices_[i0].x, vertices_[i0].y, vertices_[i0].z);
                    sgl_v3f(vertices_[i1].x, vertices_[i1].y, vertices_[i1].z);

                    // エッジ 1-2
                    sgl_v3f(vertices_[i1].x, vertices_[i1].y, vertices_[i1].z);
                    sgl_v3f(vertices_[i2].x, vertices_[i2].y, vertices_[i2].z);

                    // エッジ 2-0
                    sgl_v3f(vertices_[i2].x, vertices_[i2].y, vertices_[i2].z);
                    sgl_v3f(vertices_[i0].x, vertices_[i0].y, vertices_[i0].z);
                }
            }
        } else {
            // インデックスなしの場合、3頂点ずつ三角形として処理
            for (size_t i = 0; i + 2 < vertices_.size(); i += 3) {
                sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);

                // エッジ 0-1
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
                sgl_v3f(vertices_[i+1].x, vertices_[i+1].y, vertices_[i+1].z);

                // エッジ 1-2
                sgl_v3f(vertices_[i+1].x, vertices_[i+1].y, vertices_[i+1].z);
                sgl_v3f(vertices_[i+2].x, vertices_[i+2].y, vertices_[i+2].z);

                // エッジ 2-0
                sgl_v3f(vertices_[i+2].x, vertices_[i+2].y, vertices_[i+2].z);
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
        }

        sgl_end();
    }

private:
    // Triangle Fan を triangles として描画
    void drawTriangleFan(bool useColors, bool useIndices) const {
        if (vertices_.size() < 3) return;

        Color defColor = getDefaultContext().getColor();
        sgl_begin_triangles();

        if (useIndices && indices_.size() >= 3) {
            // インデックス使用
            for (size_t i = 1; i < indices_.size() - 1; i++) {
                unsigned int i0 = indices_[0];
                unsigned int i1 = indices_[i];
                unsigned int i2 = indices_[i + 1];

                for (auto idx : {i0, i1, i2}) {
                    if (idx < vertices_.size()) {
                        if (useColors && idx < colors_.size()) {
                            sgl_c4f(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                        } else {
                            sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                        }
                        sgl_v3f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                    }
                }
            }
        } else {
            // 頂点のみ
            for (size_t i = 1; i < vertices_.size() - 1; i++) {
                for (size_t j : {(size_t)0, i, i + 1}) {
                    if (useColors) {
                        sgl_c4f(colors_[j].r, colors_[j].g, colors_[j].b, colors_[j].a);
                    } else {
                        sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    sgl_v3f(vertices_[j].x, vertices_[j].y, vertices_[j].z);
                }
            }
        }

        sgl_end();
    }

    // Line Loop を line_strip として描画（最後に閉じる）
    void drawLineLoop(bool useColors, bool useIndices) const {
        if (vertices_.size() < 2) return;

        Color defColor = getDefaultContext().getColor();
        sgl_begin_line_strip();

        if (useIndices && !indices_.empty()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    if (useColors && idx < colors_.size()) {
                        sgl_c4f(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                    } else {
                        sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    sgl_v3f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                }
            }
            // 閉じる
            if (!indices_.empty() && indices_[0] < vertices_.size()) {
                auto idx = indices_[0];
                if (useColors && idx < colors_.size()) {
                    sgl_c4f(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                } else {
                    sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                sgl_v3f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (useColors) {
                    sgl_c4f(colors_[i].r, colors_[i].g, colors_[i].b, colors_[i].a);
                } else {
                    sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
            // 閉じる
            if (useColors) {
                sgl_c4f(colors_[0].r, colors_[0].g, colors_[0].b, colors_[0].a);
            } else {
                sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);
            }
            sgl_v3f(vertices_[0].x, vertices_[0].y, vertices_[0].z);
        }

        sgl_end();
    }

    PrimitiveMode mode_;
    std::vector<Vec3> vertices_;
    std::vector<Vec3> normals_;
    std::vector<Color> colors_;
    std::vector<unsigned int> indices_;
    std::vector<Vec2> texCoords_;
};

} // namespace trussc
