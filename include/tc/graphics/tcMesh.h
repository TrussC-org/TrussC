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
    size_t getNumVertices() const { return vertices_.size(); }

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
    size_t getNumColors() const { return colors_.size(); }
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
    size_t getNumIndices() const { return indices_.size(); }
    bool hasIndices() const { return !indices_.empty(); }

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
        colors_.clear();
        indices_.clear();
        texCoords_.clear();
    }

    void clearVertices() { vertices_.clear(); }
    void clearColors() { colors_.clear(); }
    void clearIndices() { indices_.clear(); }
    void clearTexCoords() { texCoords_.clear(); }

    // ---------------------------------------------------------------------------
    // 描画
    // ---------------------------------------------------------------------------
    void draw() const {
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
    std::vector<Color> colors_;
    std::vector<unsigned int> indices_;
    std::vector<Vec2> texCoords_;
};

} // namespace trussc
