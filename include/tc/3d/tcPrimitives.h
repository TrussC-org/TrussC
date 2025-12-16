#pragma once

// 3Dプリミティブ生成関数
// Mesh を返すので、tc/graphics/tcMesh.h が先にインクルードされている必要がある

namespace trussc {

// ---------------------------------------------------------------------------
// Plane（平面）
// ---------------------------------------------------------------------------
inline Mesh createPlane(float width, float height, int cols = 2, int rows = 2) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    // 頂点と法線を生成
    for (int y = 0; y <= rows; y++) {
        for (int x = 0; x <= cols; x++) {
            float px = -halfW + (width * x / cols);
            float py = -halfH + (height * y / rows);
            mesh.addVertex(px, py, 0);
            mesh.addNormal(0, 0, 1);  // 全頂点がZ+方向
            mesh.addTexCoord((float)x / cols, (float)y / rows);
        }
    }

    // インデックスを生成（三角形）
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int i0 = y * (cols + 1) + x;
            int i1 = i0 + 1;
            int i2 = i0 + (cols + 1);
            int i3 = i2 + 1;
            mesh.addTriangle(i0, i2, i1);
            mesh.addTriangle(i1, i2, i3);
        }
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// Box（立方体）- フラットシェーディング用（24頂点）
// ---------------------------------------------------------------------------
inline Mesh createBox(float width, float height, float depth) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float w = width * 0.5f;
    float h = height * 0.5f;
    float d = depth * 0.5f;

    // 各面に4頂点（フラットシェーディング用）
    // 前面 (Z+) - 頂点 0-3
    mesh.addVertex(-w, -h,  d); mesh.addNormal(0, 0, 1);
    mesh.addVertex( w, -h,  d); mesh.addNormal(0, 0, 1);
    mesh.addVertex( w,  h,  d); mesh.addNormal(0, 0, 1);
    mesh.addVertex(-w,  h,  d); mesh.addNormal(0, 0, 1);

    // 後面 (Z-) - 頂点 4-7
    mesh.addVertex( w, -h, -d); mesh.addNormal(0, 0, -1);
    mesh.addVertex(-w, -h, -d); mesh.addNormal(0, 0, -1);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(0, 0, -1);
    mesh.addVertex( w,  h, -d); mesh.addNormal(0, 0, -1);

    // 上面 (Y+) - 頂点 8-11
    mesh.addVertex(-w,  h,  d); mesh.addNormal(0, 1, 0);
    mesh.addVertex( w,  h,  d); mesh.addNormal(0, 1, 0);
    mesh.addVertex( w,  h, -d); mesh.addNormal(0, 1, 0);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(0, 1, 0);

    // 下面 (Y-) - 頂点 12-15
    mesh.addVertex(-w, -h, -d); mesh.addNormal(0, -1, 0);
    mesh.addVertex( w, -h, -d); mesh.addNormal(0, -1, 0);
    mesh.addVertex( w, -h,  d); mesh.addNormal(0, -1, 0);
    mesh.addVertex(-w, -h,  d); mesh.addNormal(0, -1, 0);

    // 右面 (X+) - 頂点 16-19
    mesh.addVertex( w, -h,  d); mesh.addNormal(1, 0, 0);
    mesh.addVertex( w, -h, -d); mesh.addNormal(1, 0, 0);
    mesh.addVertex( w,  h, -d); mesh.addNormal(1, 0, 0);
    mesh.addVertex( w,  h,  d); mesh.addNormal(1, 0, 0);

    // 左面 (X-) - 頂点 20-23
    mesh.addVertex(-w, -h, -d); mesh.addNormal(-1, 0, 0);
    mesh.addVertex(-w, -h,  d); mesh.addNormal(-1, 0, 0);
    mesh.addVertex(-w,  h,  d); mesh.addNormal(-1, 0, 0);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(-1, 0, 0);

    // インデックス（各面2三角形）
    for (int face = 0; face < 6; face++) {
        int base = face * 4;
        mesh.addTriangle(base, base + 1, base + 2);
        mesh.addTriangle(base, base + 2, base + 3);
    }

    return mesh;
}

inline Mesh createBox(float size) {
    return createBox(size, size, size);
}

// ---------------------------------------------------------------------------
// Sphere（球）
// ---------------------------------------------------------------------------
inline Mesh createSphere(float radius, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    int rings = resolution;
    int sectors = resolution;

    // 頂点と法線を生成
    for (int r = 0; r <= rings; r++) {
        float v = (float)r / rings;
        float phi = v * PI;

        for (int s = 0; s <= sectors; s++) {
            float u = (float)s / sectors;
            float theta = u * TAU;

            // 単位球上の点（これがそのまま法線になる）
            float x = cos(theta) * sin(phi);
            float y = cos(phi);
            float z = sin(theta) * sin(phi);

            mesh.addVertex(x * radius, y * radius, z * radius);
            mesh.addNormal(x, y, z);  // 正規化済み
            mesh.addTexCoord(u, v);
        }
    }

    // インデックスを生成
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < sectors; s++) {
            int i0 = r * (sectors + 1) + s;
            int i1 = i0 + 1;
            int i2 = i0 + (sectors + 1);
            int i3 = i2 + 1;

            if (r != 0) {
                mesh.addTriangle(i0, i2, i1);
            }
            if (r != rings - 1) {
                mesh.addTriangle(i1, i2, i3);
            }
        }
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// Cylinder（円柱）
// ---------------------------------------------------------------------------
inline Mesh createCylinder(float radius, float height, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfH = height * 0.5f;

    // 側面の頂点（放射方向の法線）
    int baseIndex = 0;
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float nx = cos(angle);  // 法線（正規化済み）
        float nz = sin(angle);
        float x = nx * radius;
        float z = nz * radius;

        mesh.addVertex(x, -halfH, z);  // 下
        mesh.addNormal(nx, 0, nz);
        mesh.addVertex(x,  halfH, z);  // 上
        mesh.addNormal(nx, 0, nz);
    }

    // 側面のインデックス
    for (int i = 0; i < resolution; i++) {
        int i0 = baseIndex + i * 2;
        int i1 = i0 + 1;
        int i2 = i0 + 2;
        int i3 = i0 + 3;
        mesh.addTriangle(i0, i2, i1);
        mesh.addTriangle(i1, i2, i3);
    }

    // 上面の中心（Y+方向の法線）
    int topCenter = mesh.getNumVertices();
    mesh.addVertex(0, halfH, 0);
    mesh.addNormal(0, 1, 0);

    // 上面の頂点
    int topBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, halfH, z);
        mesh.addNormal(0, 1, 0);
    }

    // 上面のインデックス
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(topCenter, topBase + i, topBase + i + 1);
    }

    // 下面の中心（Y-方向の法線）
    int bottomCenter = mesh.getNumVertices();
    mesh.addVertex(0, -halfH, 0);
    mesh.addNormal(0, -1, 0);

    // 下面の頂点
    int bottomBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, -halfH, z);
        mesh.addNormal(0, -1, 0);
    }

    // 下面のインデックス（逆回り）
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(bottomCenter, bottomBase + i + 1, bottomBase + i);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// Cone（円錐）
// ---------------------------------------------------------------------------
inline Mesh createCone(float radius, float height, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfH = height * 0.5f;

    // 側面の法線計算用
    // 円錐の傾斜角に基づく法線の Y 成分と水平成分
    float slopeLen = sqrt(radius * radius + height * height);
    float ny = radius / slopeLen;      // 上向き成分
    float nHoriz = height / slopeLen;  // 水平方向成分

    // 側面の頂点を三角形ごとに独立して作成（フラットシェーディング用）
    for (int i = 0; i < resolution; i++) {
        float angle0 = TAU * i / resolution;
        float angle1 = TAU * (i + 1) / resolution;
        float angleMid = (angle0 + angle1) * 0.5f;

        // この三角形の法線（中間角度での法線）
        float nx = cos(angleMid) * nHoriz;
        float nz = sin(angleMid) * nHoriz;

        // 頂点（先端）
        mesh.addVertex(0, halfH, 0);
        mesh.addNormal(nx, ny, nz);

        // 底面の2頂点
        mesh.addVertex(cos(angle0) * radius, -halfH, sin(angle0) * radius);
        mesh.addNormal(nx, ny, nz);
        mesh.addVertex(cos(angle1) * radius, -halfH, sin(angle1) * radius);
        mesh.addNormal(nx, ny, nz);

        int base = i * 3;
        mesh.addTriangle(base, base + 1, base + 2);
    }

    // 底面の中心（Y-方向の法線）
    int bottomCenter = mesh.getNumVertices();
    mesh.addVertex(0, -halfH, 0);
    mesh.addNormal(0, -1, 0);

    // 底面の円周上の頂点
    int bottomBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, -halfH, z);
        mesh.addNormal(0, -1, 0);
    }

    // 底面のインデックス（逆回り）
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(bottomCenter, bottomBase + i + 1, bottomBase + i);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// IcoSphere（正二十面体ベースの球）
// ---------------------------------------------------------------------------
inline Mesh createIcoSphere(float radius, int subdivisions = 2) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    // 正二十面体の黄金比
    float t = (1.0f + sqrt(5.0f)) / 2.0f;

    // 正規化用
    float len = sqrt(1.0f + t * t);
    float a = 1.0f / len;
    float b = t / len;

    // 12頂点（単位球上）- 法線は頂点位置と同じ
    auto addVertexWithNormal = [&](float x, float y, float z) {
        mesh.addVertex(x, y, z);
        mesh.addNormal(x, y, z);  // 単位球上なので位置=法線
    };

    addVertexWithNormal(-a,  b,  0);
    addVertexWithNormal( a,  b,  0);
    addVertexWithNormal(-a, -b,  0);
    addVertexWithNormal( a, -b,  0);
    addVertexWithNormal( 0, -a,  b);
    addVertexWithNormal( 0,  a,  b);
    addVertexWithNormal( 0, -a, -b);
    addVertexWithNormal( 0,  a, -b);
    addVertexWithNormal( b,  0, -a);
    addVertexWithNormal( b,  0,  a);
    addVertexWithNormal(-b,  0, -a);
    addVertexWithNormal(-b,  0,  a);

    // 20面
    std::vector<unsigned int> indices = {
        0, 11, 5,  0, 5, 1,  0, 1, 7,  0, 7, 10,  0, 10, 11,
        1, 5, 9,  5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1, 8,
        3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
        4, 9, 5,  2, 4, 11,  6, 2, 10,  8, 6, 7,  9, 8, 1
    };

    // 細分化
    for (int s = 0; s < subdivisions; s++) {
        std::vector<unsigned int> newIndices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpointCache;

        auto getMidpoint = [&](unsigned int i1, unsigned int i2) -> unsigned int {
            auto key = std::make_pair(std::min(i1, i2), std::max(i1, i2));
            auto it = midpointCache.find(key);
            if (it != midpointCache.end()) {
                return it->second;
            }

            auto& v1 = mesh.getVertices()[i1];
            auto& v2 = mesh.getVertices()[i2];
            Vec3 mid = {(v1.x + v2.x) * 0.5f, (v1.y + v2.y) * 0.5f, (v1.z + v2.z) * 0.5f};
            // 正規化（単位球上に投影）
            float l = sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z);
            mid.x /= l;
            mid.y /= l;
            mid.z /= l;

            unsigned int idx = mesh.getNumVertices();
            mesh.addVertex(mid);
            mesh.addNormal(mid.x, mid.y, mid.z);  // 正規化済みの位置が法線
            midpointCache[key] = idx;
            return idx;
        };

        for (size_t i = 0; i < indices.size(); i += 3) {
            unsigned int v0 = indices[i];
            unsigned int v1 = indices[i + 1];
            unsigned int v2 = indices[i + 2];

            unsigned int ma = getMidpoint(v0, v1);
            unsigned int mb = getMidpoint(v1, v2);
            unsigned int mc = getMidpoint(v2, v0);

            newIndices.push_back(v0); newIndices.push_back(ma); newIndices.push_back(mc);
            newIndices.push_back(v1); newIndices.push_back(mb); newIndices.push_back(ma);
            newIndices.push_back(v2); newIndices.push_back(mc); newIndices.push_back(mb);
            newIndices.push_back(ma); newIndices.push_back(mb); newIndices.push_back(mc);
        }

        indices = newIndices;
    }

    // 頂点にスケールを適用（法線はそのまま）
    for (auto& v : mesh.getVertices()) {
        v.x *= radius;
        v.y *= radius;
        v.z *= radius;
    }

    // インデックスを追加
    for (auto idx : indices) {
        mesh.addIndex(idx);
    }

    return mesh;
}

} // namespace trussc
