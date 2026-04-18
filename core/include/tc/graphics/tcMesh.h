#pragma once

// This file is included from TrussC.h
// Note: tcTexture.h and tcImage.h must be included before this file

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

    ~Mesh() {
        releaseGpuBuffers();
    }

    // Copy does not transfer GPU buffers; the copy starts out dirty and will
    // re-upload on next GpuPbr draw. This matches VBO ownership semantics.
    Mesh(const Mesh& other)
        : mode_(other.mode_),
          vertices_(other.vertices_),
          normals_(other.normals_),
          colors_(other.colors_),
          indices_(other.indices_),
          texCoords_(other.texCoords_),
          tangents_(other.tangents_) {}

    Mesh& operator=(const Mesh& other) {
        if (this == &other) return *this;
        releaseGpuBuffers();
        mode_ = other.mode_;
        vertices_ = other.vertices_;
        normals_ = other.normals_;
        colors_ = other.colors_;
        indices_ = other.indices_;
        texCoords_ = other.texCoords_;
        tangents_ = other.tangents_;
        gpuDirty_ = true;
        return *this;
    }

    Mesh(Mesh&& other) noexcept
        : mode_(other.mode_),
          vertices_(std::move(other.vertices_)),
          normals_(std::move(other.normals_)),
          colors_(std::move(other.colors_)),
          indices_(std::move(other.indices_)),
          texCoords_(std::move(other.texCoords_)),
          tangents_(std::move(other.tangents_)),
          vbuf_(other.vbuf_),
          ibuf_(other.ibuf_),
          gpuVertexCount_(other.gpuVertexCount_),
          gpuIndexCount_(other.gpuIndexCount_),
          gpuDirty_(other.gpuDirty_) {
        other.vbuf_ = {};
        other.ibuf_ = {};
        other.gpuVertexCount_ = 0;
        other.gpuIndexCount_ = 0;
        other.gpuDirty_ = true;
    }

    Mesh& operator=(Mesh&& other) noexcept {
        if (this == &other) return *this;
        releaseGpuBuffers();
        mode_ = other.mode_;
        vertices_ = std::move(other.vertices_);
        normals_ = std::move(other.normals_);
        colors_ = std::move(other.colors_);
        indices_ = std::move(other.indices_);
        texCoords_ = std::move(other.texCoords_);
        tangents_ = std::move(other.tangents_);
        vbuf_ = other.vbuf_;
        ibuf_ = other.ibuf_;
        gpuVertexCount_ = other.gpuVertexCount_;
        gpuIndexCount_ = other.gpuIndexCount_;
        gpuDirty_ = other.gpuDirty_;
        other.vbuf_ = {};
        other.ibuf_ = {};
        other.gpuVertexCount_ = 0;
        other.gpuIndexCount_ = 0;
        other.gpuDirty_ = true;
        return *this;
    }

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
    bool hasValidTexCoords() const {
        return hasTexCoords() && texCoords_.size() >= vertices_.size();
    }

    // ---------------------------------------------------------------------------
    // Tangents (for normal mapping)
    // ---------------------------------------------------------------------------
    // Vec4: xyz = tangent direction along texture U axis,
    //       w   = bitangent sign (+1 or -1) for handedness.
    // Bitangent is reconstructed in the shader: B = cross(N, T.xyz) * T.w
    void addTangent(float tx, float ty, float tz, float tw = 1.0f) {
        tangents_.push_back(Vec4{tx, ty, tz, tw});
    }

    void addTangent(const Vec4& t) {
        tangents_.push_back(t);
    }

    void addTangent(const Vec3& t, float w = 1.0f) {
        tangents_.push_back(Vec4{t.x, t.y, t.z, w});
    }

    std::vector<Vec4>& getTangents() { return tangents_; }
    const std::vector<Vec4>& getTangents() const { return tangents_; }
    int getNumTangents() const { return static_cast<int>(tangents_.size()); }
    bool hasTangents() const { return !tangents_.empty(); }

    // ---------------------------------------------------------------------------
    // Clear
    // ---------------------------------------------------------------------------
    void clear() {
        vertices_.clear();
        normals_.clear();
        colors_.clear();
        indices_.clear();
        texCoords_.clear();
        tangents_.clear();
    }

    void clearVertices() { vertices_.clear(); }
    void clearNormals() { normals_.clear(); }
    void clearColors() { colors_.clear(); }
    void clearIndices() { indices_.clear(); }
    void clearTexCoords() { texCoords_.clear(); }
    void clearTangents() { tangents_.clear(); }

    // ---------------------------------------------------------------------------
    // Transform
    // ---------------------------------------------------------------------------

    /// Translate all vertices
    void translate(float x, float y, float z) {
        for (auto& v : vertices_) {
            v.x += x;
            v.y += y;
            v.z += z;
        }
    }

    void translate(const Vec3& offset) {
        translate(offset.x, offset.y, offset.z);
    }

    /// Rotate around X axis (radians)
    void rotateX(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        for (auto& v : vertices_) {
            float y = v.y * c - v.z * s;
            float z = v.y * s + v.z * c;
            v.y = y;
            v.z = z;
        }
        for (auto& n : normals_) {
            float y = n.y * c - n.z * s;
            float z = n.y * s + n.z * c;
            n.y = y;
            n.z = z;
        }
    }

    /// Rotate around Y axis (radians)
    void rotateY(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        for (auto& v : vertices_) {
            float x = v.x * c + v.z * s;
            float z = -v.x * s + v.z * c;
            v.x = x;
            v.z = z;
        }
        for (auto& n : normals_) {
            float x = n.x * c + n.z * s;
            float z = -n.x * s + n.z * c;
            n.x = x;
            n.z = z;
        }
    }

    /// Rotate around Z axis (radians)
    void rotateZ(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        for (auto& v : vertices_) {
            float x = v.x * c - v.y * s;
            float y = v.x * s + v.y * c;
            v.x = x;
            v.y = y;
        }
        for (auto& n : normals_) {
            float x = n.x * c - n.y * s;
            float y = n.x * s + n.y * c;
            n.x = x;
            n.y = y;
        }
    }

    /// Scale all vertices
    void scale(float x, float y, float z) {
        for (auto& v : vertices_) {
            v.x *= x;
            v.y *= y;
            v.z *= z;
        }
        // Normals need to be renormalized if non-uniform scale
        if (x != y || y != z) {
            for (auto& n : normals_) {
                n.x /= x;
                n.y /= y;
                n.z /= z;
                float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (len > 0.0001f) {
                    n.x /= len;
                    n.y /= len;
                    n.z /= len;
                }
            }
        }
    }

    void scale(float s) {
        scale(s, s, s);
    }

    /// Apply transformation matrix to all vertices and normals
    void transform(const Mat4& m) {
        for (auto& v : vertices_) {
            v = m * v;
        }
        // Transform normals (rotation only, no translation)
        for (auto& n : normals_) {
            Vec3 transformed;
            transformed.x = m.m[0] * n.x + m.m[1] * n.y + m.m[2] * n.z;
            transformed.y = m.m[4] * n.x + m.m[5] * n.y + m.m[6] * n.z;
            transformed.z = m.m[8] * n.x + m.m[9] * n.y + m.m[10] * n.z;
            // Normalize
            float len = std::sqrt(transformed.x * transformed.x +
                                  transformed.y * transformed.y +
                                  transformed.z * transformed.z);
            if (len > 0.0001f) {
                n.x = transformed.x / len;
                n.y = transformed.y / len;
                n.z = transformed.z / len;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Append
    // ---------------------------------------------------------------------------

    /// Append another mesh to this mesh
    void append(const Mesh& other) {
        if (other.vertices_.empty()) return;

        unsigned int baseIndex = static_cast<unsigned int>(vertices_.size());

        // Append vertices
        for (const auto& v : other.vertices_) {
            vertices_.push_back(v);
        }

        // Append normals
        for (const auto& n : other.normals_) {
            normals_.push_back(n);
        }

        // Append colors
        for (const auto& c : other.colors_) {
            colors_.push_back(c);
        }

        // Append texcoords
        for (const auto& t : other.texCoords_) {
            texCoords_.push_back(t);
        }

        // Append tangents
        for (const auto& t : other.tangents_) {
            tangents_.push_back(t);
        }

        // Append indices with offset
        for (auto idx : other.indices_) {
            indices_.push_back(idx + baseIndex);
        }
    }

    // ---------------------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------------------
    void draw() const {
        if (vertices_.empty()) return;

        // GPU PBR path: requires normals and a Material.
        // Evaluated per-pixel on the GPU via the meshPbr shader.
        if (hasNormals() && normals_.size() >= vertices_.size() &&
            internal::currentMaterial) {
            drawGpuPbr();
            return;
        }

        // Normal drawing (no material set or no normals)
        drawNoLighting();
    }

    // Draw with texture
    void draw(const Texture& texture) const {
        if (vertices_.empty()) return;
        drawNoLightingWithTexture(texture);
    }

    // Draw with image (convenience method)
    void draw(const Image& image) const {
        draw(image.getTexture());
    }

    // Normal drawing without lighting
    void drawNoLighting() const {
        if (vertices_.empty()) return;

        bool useColors = hasColors() && colors_.size() >= vertices_.size();
        bool useIndices = hasIndices();
        Color defColor = getDefaultContext().getColor();
        auto& writer = internal::getActiveWriter();

        // Convert PrimitiveMode to PrimitiveType and begin
        PrimitiveType primType;
        switch (mode_) {
            case PrimitiveMode::Triangles:
                primType = PrimitiveType::Triangles;
                break;
            case PrimitiveMode::TriangleStrip:
                primType = PrimitiveType::TriangleStrip;
                break;
            case PrimitiveMode::TriangleFan:
                // No direct support, expand to triangles
                drawTriangleFan(useColors, useIndices);
                return;
            case PrimitiveMode::Lines:
                primType = PrimitiveType::Lines;
                break;
            case PrimitiveMode::LineStrip:
                primType = PrimitiveType::LineStrip;
                break;
            case PrimitiveMode::LineLoop:
                // No direct support, use line_strip + close
                drawLineLoop(useColors, useIndices);
                return;
            case PrimitiveMode::Points:
                primType = PrimitiveType::Points;
                break;
        }

        writer.begin(primType);

        // Add vertices
        if (useIndices) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    if (useColors) {
                        writer.color(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                    } else {
                        writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    writer.vertex(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                }
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (useColors) {
                    writer.color(colors_[i].r, colors_[i].g, colors_[i].b, colors_[i].a);
                } else {
                    writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                writer.vertex(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
        }

        writer.end();
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

        const Material& baseMaterial = *internal::currentMaterial;
        bool useVertexColors = hasColors() && colors_.size() >= vertices_.size();

        sgl_begin_triangles();

        auto processVertex = [&](size_t idx) {
            const Vec3& localPos = vertices_[idx];
            const Vec3& localNormal = normals_[idx];

            // Transform to world coordinates
            Vec3 worldPos = modelMatrix * localPos;

            // Transform normal (rotation only, ignore translation)
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

            // Normalize
            float len = std::sqrt(worldNormal.x * worldNormal.x +
                                  worldNormal.y * worldNormal.y +
                                  worldNormal.z * worldNormal.z);
            if (len > 0.0001f) {
                worldNormal.x /= len;
                worldNormal.y /= len;
                worldNormal.z /= len;
            }

            // Lighting calculation
            Color litColor;
            if (useVertexColors) {
                // Use vertex color as base color
                Material mat = baseMaterial;
                mat.setBaseColor(colors_[idx]);
                litColor = calculateLighting(worldPos, worldNormal, mat);
                litColor.a = colors_[idx].a;
            } else {
                litColor = calculateLighting(worldPos, worldNormal, baseMaterial);
            }

            sgl_c4f(litColor.r, litColor.g, litColor.b, litColor.a);
            sgl_v3f(localPos.x, localPos.y, localPos.z);
        };

        if (hasIndices()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size() && idx < normals_.size()) {
                    processVertex(idx);
                }
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (i < normals_.size()) {
                    processVertex(i);
                }
            }
        }

        sgl_end();
    }

    // Draw with texture (no lighting)
    void drawNoLightingWithTexture(const Texture& texture) const {
        if (vertices_.empty()) return;

        bool useColors = hasColors() && colors_.size() >= vertices_.size();
        bool useIndices = hasIndices();
        bool useTexCoords = hasValidTexCoords();
        Color defColor = getDefaultContext().getColor();

        // Enable texture
        texture.bind();

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
                drawTriangleFanWithTexture(useColors, useIndices, useTexCoords, defColor, texture);
                texture.unbind();
                return;
            case PrimitiveMode::Lines:
                sgl_begin_lines();
                break;
            case PrimitiveMode::LineStrip:
                sgl_begin_line_strip();
                break;
            case PrimitiveMode::LineLoop:
                // sokol_gl doesn't have line_loop, use line_strip + close
                drawLineLoopWithTexture(useColors, useIndices, useTexCoords, defColor, texture);
                texture.unbind();
                return;
            case PrimitiveMode::Points:
                sgl_begin_points();
                break;
        }

        // Add vertices with texture coordinates (using combined API)
        if (useIndices) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    const Color& col = useColors ? colors_[idx] : defColor;
                    const Vec2& tex = useTexCoords && idx < texCoords_.size() 
                                      ? texCoords_[idx] : Vec2(0.0f, 0.0f);
                    
                    // Use combined API: position + texture + color in one call
                    sgl_v3f_t2f_c4f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z,
                                    tex.x, tex.y,
                                    col.r, col.g, col.b, col.a);
                }
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                const Color& col = useColors ? colors_[i] : defColor;
                const Vec2& tex = useTexCoords && i < texCoords_.size() 
                                  ? texCoords_[i] : Vec2(0.0f, 0.0f);
                
                // Use combined API: position + texture + color in one call
                sgl_v3f_t2f_c4f(vertices_[i].x, vertices_[i].y, vertices_[i].z,
                                tex.x, tex.y,
                                col.r, col.g, col.b, col.a);
            }
        }

        sgl_end();
        texture.unbind();
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
        auto& writer = internal::getActiveWriter();
        writer.begin(PrimitiveType::Triangles);

        if (useIndices && indices_.size() >= 3) {
            // Using indices
            for (size_t i = 1; i < indices_.size() - 1; i++) {
                unsigned int i0 = indices_[0];
                unsigned int i1 = indices_[i];
                unsigned int i2 = indices_[i + 1];

                for (auto idx : {i0, i1, i2}) {
                    if (idx < vertices_.size()) {
                        if (useColors && idx < colors_.size()) {
                            writer.color(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                        } else {
                            writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                        }
                        writer.vertex(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                    }
                }
            }
        } else {
            // Vertices only
            for (size_t i = 1; i < vertices_.size() - 1; i++) {
                for (size_t j : {(size_t)0, i, i + 1}) {
                    if (useColors) {
                        writer.color(colors_[j].r, colors_[j].g, colors_[j].b, colors_[j].a);
                    } else {
                        writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    writer.vertex(vertices_[j].x, vertices_[j].y, vertices_[j].z);
                }
            }
        }

        writer.end();
    }

    // Draw Line Loop as line_strip (close at end)
    void drawLineLoop(bool useColors, bool useIndices) const {
        if (vertices_.size() < 2) return;

        Color defColor = getDefaultContext().getColor();
        auto& writer = internal::getActiveWriter();
        writer.begin(PrimitiveType::LineStrip);

        if (useIndices && !indices_.empty()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    if (useColors && idx < colors_.size()) {
                        writer.color(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                    } else {
                        writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                    }
                    writer.vertex(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
                }
            }
            // Close
            if (!indices_.empty() && indices_[0] < vertices_.size()) {
                auto idx = indices_[0];
                if (useColors && idx < colors_.size()) {
                    writer.color(colors_[idx].r, colors_[idx].g, colors_[idx].b, colors_[idx].a);
                } else {
                    writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                writer.vertex(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                if (useColors) {
                    writer.color(colors_[i].r, colors_[i].g, colors_[i].b, colors_[i].a);
                } else {
                    writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
                }
                writer.vertex(vertices_[i].x, vertices_[i].y, vertices_[i].z);
            }
            // Close
            if (useColors) {
                writer.color(colors_[0].r, colors_[0].g, colors_[0].b, colors_[0].a);
            } else {
                writer.color(defColor.r, defColor.g, defColor.b, defColor.a);
            }
            writer.vertex(vertices_[0].x, vertices_[0].y, vertices_[0].z);
        }

        writer.end();
    }

    // Draw Triangle Fan with texture
    void drawTriangleFanWithTexture(bool useColors, bool useIndices, bool useTexCoords,
                                    const Color& defColor, const Texture& texture) const {
        if (vertices_.size() < 3) return;

        sgl_begin_triangles();

        if (useIndices && indices_.size() >= 3) {
            // Using indices
            for (size_t i = 1; i < indices_.size() - 1; i++) {
                unsigned int i0 = indices_[0];
                unsigned int i1 = indices_[i];
                unsigned int i2 = indices_[i + 1];

                for (auto idx : {i0, i1, i2}) {
                    if (idx < vertices_.size()) {
                        const Color& col = (useColors && idx < colors_.size()) 
                                          ? colors_[idx] : defColor;
                        const Vec2& tex = useTexCoords && idx < texCoords_.size() 
                                          ? texCoords_[idx] : Vec2(0.0f, 0.0f);
                        
                        sgl_v3f_t2f_c4f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z,
                                        tex.x, tex.y,
                                        col.r, col.g, col.b, col.a);
                    }
                }
            }
        } else {
            // Vertices only
            for (size_t i = 1; i < vertices_.size() - 1; i++) {
                for (size_t j : {(size_t)0, i, i + 1}) {
                    const Color& col = useColors ? colors_[j] : defColor;
                    const Vec2& tex = useTexCoords && j < texCoords_.size() 
                                      ? texCoords_[j] : Vec2(0.0f, 0.0f);
                    
                    sgl_v3f_t2f_c4f(vertices_[j].x, vertices_[j].y, vertices_[j].z,
                                    tex.x, tex.y,
                                    col.r, col.g, col.b, col.a);
                }
            }
        }

        sgl_end();
    }

    // Draw Line Loop with texture
    void drawLineLoopWithTexture(bool useColors, bool useIndices, bool useTexCoords,
                                  const Color& defColor, const Texture& texture) const {
        if (vertices_.size() < 2) return;

        sgl_begin_line_strip();

        if (useIndices && !indices_.empty()) {
            for (auto idx : indices_) {
                if (idx < vertices_.size()) {
                    const Color& col = (useColors && idx < colors_.size()) 
                                      ? colors_[idx] : defColor;
                    const Vec2& tex = useTexCoords && idx < texCoords_.size() 
                                      ? texCoords_[idx] : Vec2(0.0f, 0.0f);
                    
                    sgl_v3f_t2f_c4f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z,
                                    tex.x, tex.y,
                                    col.r, col.g, col.b, col.a);
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
                
                if (useTexCoords && idx < texCoords_.size()) {
                    sgl_t2f(texCoords_[idx].x, texCoords_[idx].y);
                } else {
                    sgl_t2f(0.0f, 0.0f);
                }
                
                sgl_v3f(vertices_[idx].x, vertices_[idx].y, vertices_[idx].z);
            }
        } else {
            for (size_t i = 0; i < vertices_.size(); i++) {
                const Color& col = useColors ? colors_[i] : defColor;
                const Vec2& tex = useTexCoords && i < texCoords_.size() 
                                  ? texCoords_[i] : Vec2(0.0f, 0.0f);
                
                sgl_v3f_t2f_c4f(vertices_[i].x, vertices_[i].y, vertices_[i].z,
                                tex.x, tex.y,
                                col.r, col.g, col.b, col.a);
            }
            // Close
            const Color& col0 = useColors ? colors_[0] : defColor;
            const Vec2& tex0 = useTexCoords && !texCoords_.empty() 
                               ? texCoords_[0] : Vec2(0.0f, 0.0f);
            
            sgl_v3f_t2f_c4f(vertices_[0].x, vertices_[0].y, vertices_[0].z,
                            tex0.x, tex0.y,
                            col0.r, col0.g, col0.b, col0.a);
        }

        sgl_end();
    }

public:
    // ---------------------------------------------------------------------------
    // GPU buffer management (for LightingMode::GpuPbr path)
    // ---------------------------------------------------------------------------
    //
    // Mesh owns optional sg_buffer handles which are created lazily on the
    // first GpuPbr draw and kept alive until the Mesh is destroyed.
    //
    // The buffers are marked dirty automatically when the vertex or index
    // vector size changes. For in-place modifications that don't change size
    // (e.g. writing directly into getVertices()[i].x), call markGpuDirty()
    // explicitly before the next draw.

    // Force a re-upload on the next GpuPbr draw.
    void markGpuDirty() const { gpuDirty_ = true; }

    // Upload interleaved (pos, normal, uv) data to a sg_buffer. Lazy; no-op if
    // already clean and sizes match. Called automatically from drawGpuPbr().
    void uploadToGpu() const {
        // Detect external mutation via size change (best effort)
        if (static_cast<int>(vertices_.size()) != gpuVertexCount_ ||
            static_cast<int>(indices_.size()) != gpuIndexCount_) {
            gpuDirty_ = true;
        }
        if (!gpuDirty_) return;
        if (vertices_.empty()) return;

        // Pack interleaved: pos(3) + normal(3) + uv(2) + tangent(4) = 48 bytes
        struct PbrVertex {
            float x, y, z;
            float nx, ny, nz;
            float u, v;
            float tx, ty, tz, tw;
        };
        std::vector<PbrVertex> packed;
        packed.resize(vertices_.size());
        for (size_t i = 0; i < vertices_.size(); ++i) {
            PbrVertex& pv = packed[i];
            pv.x = vertices_[i].x;
            pv.y = vertices_[i].y;
            pv.z = vertices_[i].z;
            if (i < normals_.size()) {
                pv.nx = normals_[i].x;
                pv.ny = normals_[i].y;
                pv.nz = normals_[i].z;
            } else {
                pv.nx = 0.0f; pv.ny = 1.0f; pv.nz = 0.0f;
            }
            if (i < texCoords_.size()) {
                pv.u = texCoords_[i].x;
                pv.v = texCoords_[i].y;
            } else {
                pv.u = 0.0f; pv.v = 0.0f;
            }
            if (i < tangents_.size()) {
                pv.tx = tangents_[i].x;
                pv.ty = tangents_[i].y;
                pv.tz = tangents_[i].z;
                pv.tw = tangents_[i].w;
            } else {
                pv.tx = 0.0f; pv.ty = 0.0f; pv.tz = 0.0f; pv.tw = 0.0f;
            }
        }

        releaseGpuBuffers();

        sg_buffer_desc vbd = {};
        vbd.data.ptr = packed.data();
        vbd.data.size = packed.size() * sizeof(PbrVertex);
        vbd.label = "tc_mesh_pbr_vbuf";
        vbuf_ = sg_make_buffer(&vbd);
        gpuVertexCount_ = static_cast<int>(vertices_.size());

        if (!indices_.empty()) {
            sg_buffer_desc ibd = {};
            ibd.usage.index_buffer = true;
            ibd.data.ptr = indices_.data();
            ibd.data.size = indices_.size() * sizeof(unsigned int);
            ibd.label = "tc_mesh_pbr_ibuf";
            ibuf_ = sg_make_buffer(&ibd);
            gpuIndexCount_ = static_cast<int>(indices_.size());
        } else {
            gpuIndexCount_ = 0;
        }

        gpuDirty_ = false;
    }

    // Draw via the GPU PBR pipeline. Defined in tcMeshPbrPipeline.h which is
    // included after this file by TrussC.h.
    void drawGpuPbr() const;

    // Accessors used by PbrPipeline
    sg_buffer getGpuVertexBuffer() const { return vbuf_; }
    sg_buffer getGpuIndexBuffer() const { return ibuf_; }
    int getGpuVertexCount() const { return gpuVertexCount_; }
    int getGpuIndexCount() const { return gpuIndexCount_; }

private:
    void releaseGpuBuffers() const {
        if (vbuf_.id != 0) {
            sg_destroy_buffer(vbuf_);
            vbuf_ = {};
        }
        if (ibuf_.id != 0) {
            sg_destroy_buffer(ibuf_);
            ibuf_ = {};
        }
        gpuVertexCount_ = 0;
        gpuIndexCount_ = 0;
        gpuDirty_ = true;
    }

    PrimitiveMode mode_;
    std::vector<Vec3> vertices_;
    std::vector<Vec3> normals_;
    std::vector<Color> colors_;
    std::vector<unsigned int> indices_;
    std::vector<Vec2> texCoords_;
    std::vector<Vec4> tangents_;

    // GPU buffers for LightingMode::GpuPbr. mutable so that draw() (const) can
    // lazily upload.
    mutable sg_buffer vbuf_{};
    mutable sg_buffer ibuf_{};
    mutable int gpuVertexCount_{0};
    mutable int gpuIndexCount_{0};
    mutable bool gpuDirty_{true};
};

} // namespace trussc
