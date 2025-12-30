#pragma once

// 3D primitive generation functions
// Returns Mesh, so tc/graphics/tcMesh.h must be included first

namespace trussc {

// ---------------------------------------------------------------------------
// Plane
// ---------------------------------------------------------------------------
inline Mesh createPlane(float width, float height, int cols = 2, int rows = 2) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    // Generate vertices and normals
    for (int y = 0; y <= rows; y++) {
        for (int x = 0; x <= cols; x++) {
            float px = -halfW + (width * x / cols);
            float py = -halfH + (height * y / rows);
            mesh.addVertex(px, py, 0);
            mesh.addNormal(0, 0, 1);  // All vertices face Z+ direction
            mesh.addTexCoord((float)x / cols, (float)y / rows);
        }
    }

    // Generate indices (triangles)
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
// Box - for flat shading (24 vertices)
// ---------------------------------------------------------------------------
inline Mesh createBox(float width, float height, float depth) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float w = width * 0.5f;
    float h = height * 0.5f;
    float d = depth * 0.5f;

    // 4 vertices per face (for flat shading)
    // Front face (Z+) - vertices 0-3
    mesh.addVertex(-w, -h,  d); mesh.addNormal(0, 0, 1); mesh.addTexCoord(0, 1);
    mesh.addVertex( w, -h,  d); mesh.addNormal(0, 0, 1); mesh.addTexCoord(1, 1);
    mesh.addVertex( w,  h,  d); mesh.addNormal(0, 0, 1); mesh.addTexCoord(1, 0);
    mesh.addVertex(-w,  h,  d); mesh.addNormal(0, 0, 1); mesh.addTexCoord(0, 0);

    // Back face (Z-) - vertices 4-7
    mesh.addVertex( w, -h, -d); mesh.addNormal(0, 0, -1); mesh.addTexCoord(0, 1);
    mesh.addVertex(-w, -h, -d); mesh.addNormal(0, 0, -1); mesh.addTexCoord(1, 1);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(0, 0, -1); mesh.addTexCoord(1, 0);
    mesh.addVertex( w,  h, -d); mesh.addNormal(0, 0, -1); mesh.addTexCoord(0, 0);

    // Top face (Y+) - vertices 8-11
    mesh.addVertex(-w,  h,  d); mesh.addNormal(0, 1, 0); mesh.addTexCoord(0, 1);
    mesh.addVertex( w,  h,  d); mesh.addNormal(0, 1, 0); mesh.addTexCoord(1, 1);
    mesh.addVertex( w,  h, -d); mesh.addNormal(0, 1, 0); mesh.addTexCoord(1, 0);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(0, 1, 0); mesh.addTexCoord(0, 0);

    // Bottom face (Y-) - vertices 12-15
    mesh.addVertex(-w, -h, -d); mesh.addNormal(0, -1, 0); mesh.addTexCoord(0, 1);
    mesh.addVertex( w, -h, -d); mesh.addNormal(0, -1, 0); mesh.addTexCoord(1, 1);
    mesh.addVertex( w, -h,  d); mesh.addNormal(0, -1, 0); mesh.addTexCoord(1, 0);
    mesh.addVertex(-w, -h,  d); mesh.addNormal(0, -1, 0); mesh.addTexCoord(0, 0);

    // Right face (X+) - vertices 16-19
    mesh.addVertex( w, -h,  d); mesh.addNormal(1, 0, 0); mesh.addTexCoord(0, 1);
    mesh.addVertex( w, -h, -d); mesh.addNormal(1, 0, 0); mesh.addTexCoord(1, 1);
    mesh.addVertex( w,  h, -d); mesh.addNormal(1, 0, 0); mesh.addTexCoord(1, 0);
    mesh.addVertex( w,  h,  d); mesh.addNormal(1, 0, 0); mesh.addTexCoord(0, 0);

    // Left face (X-) - vertices 20-23
    mesh.addVertex(-w, -h, -d); mesh.addNormal(-1, 0, 0); mesh.addTexCoord(0, 1);
    mesh.addVertex(-w, -h,  d); mesh.addNormal(-1, 0, 0); mesh.addTexCoord(1, 1);
    mesh.addVertex(-w,  h,  d); mesh.addNormal(-1, 0, 0); mesh.addTexCoord(1, 0);
    mesh.addVertex(-w,  h, -d); mesh.addNormal(-1, 0, 0); mesh.addTexCoord(0, 0);

    // Indices (2 triangles per face)
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
// Sphere
// ---------------------------------------------------------------------------
inline Mesh createSphere(float radius, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    int rings = resolution;
    int sectors = resolution;

    // Generate vertices and normals
    for (int r = 0; r <= rings; r++) {
        float v = (float)r / rings;
        float phi = v * HALF_TAU;

        for (int s = 0; s <= sectors; s++) {
            float u = (float)s / sectors;
            float theta = u * TAU;

            // Point on unit sphere (this becomes the normal directly)
            float x = cos(theta) * sin(phi);
            float y = cos(phi);
            float z = sin(theta) * sin(phi);

            mesh.addVertex(x * radius, y * radius, z * radius);
            mesh.addNormal(x, y, z);  // Already normalized
            mesh.addTexCoord(u, v);
        }
    }

    // Generate indices
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
// Cylinder
// ---------------------------------------------------------------------------
inline Mesh createCylinder(float radius, float height, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfH = height * 0.5f;

    // Side vertices (radial normals)
    int baseIndex = 0;
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float nx = cos(angle);  // Normal (already normalized)
        float nz = sin(angle);
        float x = nx * radius;
        float z = nz * radius;

        mesh.addVertex(x, -halfH, z);  // Bottom
        mesh.addNormal(nx, 0, nz);
        mesh.addVertex(x,  halfH, z);  // Top
        mesh.addNormal(nx, 0, nz);
    }

    // Side indices
    for (int i = 0; i < resolution; i++) {
        int i0 = baseIndex + i * 2;
        int i1 = i0 + 1;
        int i2 = i0 + 2;
        int i3 = i0 + 3;
        mesh.addTriangle(i0, i2, i1);
        mesh.addTriangle(i1, i2, i3);
    }

    // Top cap center (Y+ normal)
    int topCenter = mesh.getNumVertices();
    mesh.addVertex(0, halfH, 0);
    mesh.addNormal(0, 1, 0);

    // Top cap vertices
    int topBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, halfH, z);
        mesh.addNormal(0, 1, 0);
    }

    // Top cap indices
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(topCenter, topBase + i, topBase + i + 1);
    }

    // Bottom cap center (Y- normal)
    int bottomCenter = mesh.getNumVertices();
    mesh.addVertex(0, -halfH, 0);
    mesh.addNormal(0, -1, 0);

    // Bottom cap vertices
    int bottomBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, -halfH, z);
        mesh.addNormal(0, -1, 0);
    }

    // Bottom cap indices (reverse winding)
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(bottomCenter, bottomBase + i + 1, bottomBase + i);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// Cone
// ---------------------------------------------------------------------------
inline Mesh createCone(float radius, float height, int resolution = 16) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    float halfH = height * 0.5f;

    // For calculating side normals
    // Y and horizontal components based on cone slope angle
    float slopeLen = sqrt(radius * radius + height * height);
    float ny = radius / slopeLen;      // Upward component
    float nHoriz = height / slopeLen;  // Horizontal component

    // Create side vertices independently per triangle (for flat shading)
    for (int i = 0; i < resolution; i++) {
        float angle0 = TAU * i / resolution;
        float angle1 = TAU * (i + 1) / resolution;
        float angleMid = (angle0 + angle1) * 0.5f;

        // Normal for this triangle (at mid angle)
        float nx = cos(angleMid) * nHoriz;
        float nz = sin(angleMid) * nHoriz;

        // Apex vertex
        mesh.addVertex(0, halfH, 0);
        mesh.addNormal(nx, ny, nz);

        // Two base vertices
        mesh.addVertex(cos(angle0) * radius, -halfH, sin(angle0) * radius);
        mesh.addNormal(nx, ny, nz);
        mesh.addVertex(cos(angle1) * radius, -halfH, sin(angle1) * radius);
        mesh.addNormal(nx, ny, nz);

        int base = i * 3;
        mesh.addTriangle(base, base + 1, base + 2);
    }

    // Bottom cap center (Y- normal)
    int bottomCenter = mesh.getNumVertices();
    mesh.addVertex(0, -halfH, 0);
    mesh.addNormal(0, -1, 0);

    // Bottom cap vertices
    int bottomBase = mesh.getNumVertices();
    for (int i = 0; i <= resolution; i++) {
        float angle = TAU * i / resolution;
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        mesh.addVertex(x, -halfH, z);
        mesh.addNormal(0, -1, 0);
    }

    // Bottom cap indices (reverse winding)
    for (int i = 0; i < resolution; i++) {
        mesh.addTriangle(bottomCenter, bottomBase + i + 1, bottomBase + i);
    }

    return mesh;
}

// ---------------------------------------------------------------------------
// IcoSphere (icosahedron-based sphere)
// ---------------------------------------------------------------------------
inline Mesh createIcoSphere(float radius, int subdivisions = 2) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    // Golden ratio for icosahedron
    float t = (1.0f + sqrt(5.0f)) / 2.0f;

    // For normalization
    float len = sqrt(1.0f + t * t);
    float a = 1.0f / len;
    float b = t / len;

    // 12 vertices (on unit sphere) - normal equals vertex position
    auto addVertexWithNormal = [&](float x, float y, float z) {
        mesh.addVertex(x, y, z);
        mesh.addNormal(x, y, z);  // On unit sphere, position = normal
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

    // 20 faces
    std::vector<unsigned int> indices = {
        0, 11, 5,  0, 5, 1,  0, 1, 7,  0, 7, 10,  0, 10, 11,
        1, 5, 9,  5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1, 8,
        3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
        4, 9, 5,  2, 4, 11,  6, 2, 10,  8, 6, 7,  9, 8, 1
    };

    // Subdivision
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
            // Normalize (project onto unit sphere)
            float l = sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z);
            mid.x /= l;
            mid.y /= l;
            mid.z /= l;

            unsigned int idx = mesh.getNumVertices();
            mesh.addVertex(mid);
            mesh.addNormal(mid.x, mid.y, mid.z);  // Normalized position is the normal
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

    // Apply scale to vertices (normals stay as-is)
    for (auto& v : mesh.getVertices()) {
        v.x *= radius;
        v.y *= radius;
        v.z *= radius;
    }

    // Add indices
    for (auto idx : indices) {
        mesh.addIndex(idx);
    }

    return mesh;
}

} // namespace trussc
