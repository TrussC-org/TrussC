#pragma once

// This file is included from TrussC.h

#include <vector>

namespace trussc {

// Primitive mode
enum class PrimitiveMode {
    Triangles,
    TriangleStrip,
    TriangleFan,
    Lines,
    LineStrip,
    LineLoop,
    Points
};

// Mesh - Class with vertices, colors, and indices
class Mesh {
public:
    Mesh() : mode_(PrimitiveMode::Triangles) {}

    // Mode settings
    void setMode(PrimitiveMode mode) {
        mode_ = mode;
    }

    PrimitiveMode getMode() const {
        return mode_;
    }

    // ---------------------------------------------------------------------------
    // Vertices
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
    // Colors (vertex colors)
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
    // Indices
    // ---------------------------------------------------------------------------
    void addIndex(unsigned int index) {
        indices_.push_back(index);
    }

    void addIndices(const std::vector<unsigned int>& inds) {
        for (auto i : inds) {
            indices_.push_back(i);
        }
    }

    // Add triangle (3 indices)
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
    // Normals (for lighting)
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
        return Vec3{0, 0, 1};  // Default: Z direction
    }

    std::vector<Vec3>& getNormals() { return normals_; }
    const std::vector<Vec3>& getNormals() const { return normals_; }
    int getNumNormals() const { return static_cast<int>(normals_.size()); }
    bool hasNormals() const { return !normals_.empty(); }

    // ---------------------------------------------------------------------------
    // Texture coordinates
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
    // Clear
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
    // Drawing
    // ---------------------------------------------------------------------------
    void draw() const {
        if (vertices_.empty()) return;

        // If lighting enabled and normals present, draw with lighting
        if (internal::lightingEnabled && hasNormals() &&
            normals_.size() >= vertices_.size() && internal::currentMaterial) {
            drawWithLighting();
            return;
        }

        // Normal drawing
        drawNoLighting();
    }

    // Normal drawing without lighting
    void drawNoLighting() const {
        if (vertices_.empty()) return;

        bool useColors = hasColors() && colors_.size() >= vertices_.size();
        bool useIndices = hasIndices();
        Color defColor = getDefaultContext().getColor();

        // Start sokol_gl draw mode
        switch (mode_) {
            case PrimitiveMode::Triangles:
                sgl_begin_triangles();
                break;
            case PrimitiveMode::TriangleStrip:
                sgl_begin_triangle_strip();
                break;
            case PrimitiveMode::TriangleFan:
                // sokol_gl doesn't have triangle_fan, use triangles instead
                drawTriangleFan(useColors, useIndices);
                return;
            case PrimitiveMode::Lines:
                sgl_begin_lines();
                break;
            case PrimitiveMode::LineStrip:
                sgl_begin_line_strip();
                break;
            case PrimitiveMode::LineLoop:
                // sokol_gl doesn't have line_loop, use line_strip + close
                drawLineLoop(useColors, useIndices);
                return;
            case PrimitiveMode::Points:
                sgl_begin_points();
                break;
        }

        // Add vertices
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

    // Draw with lighting (CPU-side lighting calculation)
    void drawWithLighting() const {
        if (mode_ != PrimitiveMode::Triangles) {
            // Currently only triangle mode supported
            drawNoLighting();
            return;
        }

        // Get current transformation matrix
        Mat4 modelMatrix = getDefaultContext().getCurrentMatrix();

        // Matrix for normal transformation (inverse transpose)
        // Simple implementation: if scale is uniform, modelMatrix can be used as-is
        // TODO: Calculate inverse transpose for non-uniform scale

        const Material& material = *internal::currentMaterial;

        sgl_begin_triangles();

        if (hasIndices()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size() && idx < normals_.size()) {
                    const Vec3& localPos = vertices_[idx];
                    const Vec3& localNormal = normals_[idx];

                    // Transform to world coordinates
                    Vec3 worldPos = modelMatrix * localPos;

                    // Transform normal (apply rotation only, ignore translation)
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

                    // Normalize normal
                    float len = std::sqrt(worldNormal.x * worldNormal.x +
                                          worldNormal.y * worldNormal.y +
                                          worldNormal.z * worldNormal.z);
                    if (len > 0.0001f) {
                        worldNormal.x /= len;
                        worldNormal.y /= len;
                        worldNormal.z /= len;
                    }

                    // Lighting calculation
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

                    // Transform to world coordinates
                    Vec3 worldPos = modelMatrix * localPos;

                    // Transform normal
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

                    // Normalize normal
                    float len = std::sqrt(worldNormal.x * worldNormal.x +
                                          worldNormal.y * worldNormal.y +
                                          worldNormal.z * worldNormal.z);
                    if (len > 0.0001f) {
                        worldNormal.x /= len;
                        worldNormal.y /= len;
                        worldNormal.z /= len;
                    }

                    // Lighting calculation
                    Color litColor = calculateLighting(worldPos, worldNormal, material);

                    sgl_c4f(litColor.r, litColor.g, litColor.b, litColor.a);
                    sgl_v3f(localPos.x, localPos.y, localPos.z);
                }
            }
        }

        sgl_end();
    }

    // Wireframe drawing (draw triangle edges as lines)
    void drawWireframe() const {
        if (vertices_.empty()) return;

        // If current mode is not triangle-based, use normal draw
        if (mode_ != PrimitiveMode::Triangles &&
            mode_ != PrimitiveMode::TriangleStrip &&
            mode_ != PrimitiveMode::TriangleFan) {
            draw();
            return;
        }

        Color defColor = getDefaultContext().getColor();
        sgl_begin_lines();

        if (hasIndices()) {
            // When using indices, draw edges for each triangle
            for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
                unsigned int i0 = indices_[i];
                unsigned int i1 = indices_[i + 1];
                unsigned int i2 = indices_[i + 2];

                if (i0 < vertices_.size() && i1 < vertices_.size() && i2 < vertices_.size()) {
                    sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);

                    // Edge 0-1
                    sgl_v3f(vertices_[i0].x, vertices_[i0].y, vertices_[i0].z);
                    sgl_v3f(vertices_[i1].x, vertices_[i1].y, vertices_[i1].z);

                    // Edge 1-2
                    sgl_v3f(vertices_[i1].x, vertices_[i1].y, vertices_[i1].z);
                    sgl_v3f(vertices_[i2].x, vertices_[i2].y, vertices_[i2].z);

                    // Edge 2-0
                    sgl_v3f(vertices_[i2].x, vertices_[i2].y, vertices_[i2].z);
                    sgl_v3f(vertices_[i0].x, vertices_[i0].y, vertices_[i0].z);
                }
            }
        } else {
            // Without indices, process 3 vertices at a time as triangles
            for (size_t i = 0; i + 2 < vertices_.size(); i += 3) {
                sgl_c4f(defColor.r, defColor.g, defColor.b, defColor.a);

                // Edge 0-1
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
                sgl_v3f(vertices_[i+1].x, vertices_[i+1].y, vertices_[i+1].z);

                // Edge 1-2
                sgl_v3f(vertices_[i+1].x, vertices_[i+1].y, vertices_[i+1].z);
                sgl_v3f(vertices_[i+2].x, vertices_[i+2].y, vertices_[i+2].z);

                // Edge 2-0
                sgl_v3f(vertices_[i+2].x, vertices_[i+2].y, vertices_[i+2].z);
                sgl_v3f(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
        }

        sgl_end();
    }

private:
    // Draw Triangle Fan as triangles
    void drawTriangleFan(bool useColors, bool useIndices) const {
        if (vertices_.size() < 3) return;

        Color defColor = getDefaultContext().getColor();
        sgl_begin_triangles();

        if (useIndices && indices_.size() >= 3) {
            // Using indices
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
            // Vertices only
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

    // Draw Line Loop as line_strip (close at end)
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
            // Close
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
            // Close
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
