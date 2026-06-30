#include "tcxObjExporter.h"
#include <fstream>
#include <cmath>
#include <algorithm>

using namespace std;

namespace tcx {

// =============================================================================
// Check if entry needs a material
// =============================================================================

bool ObjExporter::needsMaterial(const ObjExportEntry& entry) const {
    if (entry.texture) return true;
    if (vertexColorMode_ == VertexColorMode::BakeTexture &&
        entry.mesh->hasColors() && entry.mesh->hasTexCoords()) {
        return true;
    }
    return false;
}

// =============================================================================
// Save
// =============================================================================

bool ObjExporter::save(const fs::path& path) const {
    if (entries_.empty()) {
        logWarning() << "ObjExporter::save: no meshes added";
        return false;
    }

    // Resolve path
    fs::path objPath = path.is_absolute() ? path : fs::path(getDataPath(path.string()));

    // Ensure parent directory exists
    fs::path dir = objPath.parent_path();
    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    // Derive .mtl filename
    string baseName = objPath.stem().string();
    fs::path mtlPath = dir / (baseName + ".mtl");
    string mtlFileName = mtlPath.filename().string();

    // Check if any entry needs materials
    bool hasMaterials = false;
    for (auto& entry : entries_) {
        if (needsMaterial(entry)) {
            hasMaterials = true;
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Write .obj
    // -------------------------------------------------------------------------
    ofstream objFile(objPath);
    if (!objFile.is_open()) {
        logError() << "ObjExporter::save: cannot open " << objPath;
        return false;
    }

    objFile << "# Exported by tcxObj (TrussC)" << endl;

    if (hasMaterials) {
        objFile << "mtllib " << mtlFileName << endl;
    }

    objFile << endl;

    int vertexOffset = 0;
    int normalOffset = 0;
    int texCoordOffset = 0;

    for (auto& entry : entries_) {
        writeMeshGeometry(objFile, entry, vertexOffset, normalOffset, texCoordOffset);
    }

    objFile.close();

    // -------------------------------------------------------------------------
    // Write .mtl + textures
    // -------------------------------------------------------------------------
    if (hasMaterials) {
        ofstream mtlFile(mtlPath);
        if (!mtlFile.is_open()) {
            logError() << "ObjExporter::save: cannot open " << mtlPath;
            return false;
        }

        mtlFile << "# Material file exported by tcxObj (TrussC)" << endl;
        mtlFile << endl;

        for (auto& entry : entries_) {
            if (!needsMaterial(entry)) continue;

            string matName = "mat_" + entry.name;
            mtlFile << "newmtl " << matName << endl;
            mtlFile << "Ka 1.0 1.0 1.0" << endl;
            mtlFile << "Kd 1.0 1.0 1.0" << endl;
            mtlFile << "Ks 0.0 0.0 0.0" << endl;
            mtlFile << "d 1.0" << endl;

            string texFileName = baseName + "_" + entry.name + ".png";
            mtlFile << "map_Kd " << texFileName << endl;
            fs::path texPath = dir / texFileName;

            if (entry.texture) {
                // Save provided texture
                if (!saveTexture(*entry.texture, texPath)) {
                    logWarning() << "ObjExporter: failed to save texture " << texPath;
                }
            } else {
                // Bake vertex colors to texture
                int texSize = bakeTextureSize_;
                if (maxTextureSize_ > 0) texSize = min(texSize, maxTextureSize_);
                Pixels baked = bakeVertexColors(*entry.mesh, texSize);
                if (!savePixels(baked, texPath)) {
                    logWarning() << "ObjExporter: failed to save baked texture " << texPath;
                }
            }

            mtlFile << endl;
        }

        mtlFile.close();
    }

    logNotice() << "ObjExporter: saved " << objPath;
    return true;
}

// =============================================================================
// Write geometry for one mesh
// =============================================================================

void ObjExporter::writeMeshGeometry(ofstream& file, const ObjExportEntry& entry,
                                     int& vertexOffset, int& normalOffset,
                                     int& texCoordOffset) const {
    const Mesh& mesh = *entry.mesh;

    file << "o " << entry.name << endl;

    // Use material if needed
    if (needsMaterial(entry)) {
        file << "usemtl mat_" << entry.name << endl;
    }

    const auto& vertices = mesh.getVertices();
    const auto& normals = mesh.getNormals();
    const auto& colors = mesh.getColors();
    const auto& texCoords = mesh.getTexCoords();
    const auto& indices = mesh.getIndices();

    bool hasNormals = mesh.hasNormals() && normals.size() >= vertices.size();
    bool hasTexCoords = mesh.hasTexCoords() && texCoords.size() >= vertices.size();
    bool hasColors = mesh.hasColors() && colors.size() >= vertices.size();

    // Write vertex colors as RGB when in VertexRGB mode (and not baking)
    bool writeVertexRGB = hasColors && vertexColorMode_ == VertexColorMode::VertexRGB;

    // Vertices
    for (size_t i = 0; i < vertices.size(); i++) {
        float x = vertices[i].x * scale_;
        float y = vertices[i].y * scale_;
        float z = vertices[i].z * scale_;
        if (flipYZ_) swap(y, z);

        if (writeVertexRGB) {
            file << "v " << x << " " << y << " " << z
                 << " " << colors[i].r << " " << colors[i].g << " " << colors[i].b << endl;
        } else {
            file << "v " << x << " " << y << " " << z << endl;
        }
    }

    // Normals
    if (hasNormals) {
        for (auto& n : normals) {
            float nx = n.x;
            float ny = n.y;
            float nz = n.z;
            if (flipYZ_) swap(ny, nz);
            file << "vn " << nx << " " << ny << " " << nz << endl;
        }
    }

    // Texture coordinates
    if (hasTexCoords) {
        for (auto& t : texCoords) {
            // OBJ convention: V is flipped (0 = bottom)
            file << "vt " << t.x << " " << (1.0f - t.y) << endl;
        }
    }

    // Faces (OBJ indices are 1-based)
    if (mesh.hasIndices()) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            file << "f";
            for (int j = 0; j < 3; j++) {
                int vi = static_cast<int>(indices[i + j]) + 1 + vertexOffset;

                if (hasTexCoords && hasNormals) {
                    int ti = static_cast<int>(indices[i + j]) + 1 + texCoordOffset;
                    int ni = static_cast<int>(indices[i + j]) + 1 + normalOffset;
                    file << " " << vi << "/" << ti << "/" << ni;
                } else if (hasTexCoords) {
                    int ti = static_cast<int>(indices[i + j]) + 1 + texCoordOffset;
                    file << " " << vi << "/" << ti;
                } else if (hasNormals) {
                    int ni = static_cast<int>(indices[i + j]) + 1 + normalOffset;
                    file << " " << vi << "//" << ni;
                } else {
                    file << " " << vi;
                }
            }
            file << endl;
        }
    } else {
        // No indices — treat sequential vertices as triangles
        for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
            file << "f";
            for (int j = 0; j < 3; j++) {
                int vi = static_cast<int>(i + j) + 1 + vertexOffset;

                if (hasTexCoords && hasNormals) {
                    file << " " << vi << "/" << vi << "/" << vi;
                } else if (hasTexCoords) {
                    file << " " << vi << "/" << vi;
                } else if (hasNormals) {
                    file << " " << vi << "//" << vi;
                } else {
                    file << " " << vi;
                }
            }
            file << endl;
        }
    }

    file << endl;

    // Update offsets for next mesh
    vertexOffset += static_cast<int>(vertices.size());
    normalOffset += hasNormals ? static_cast<int>(normals.size()) : 0;
    texCoordOffset += hasTexCoords ? static_cast<int>(texCoords.size()) : 0;
}

// =============================================================================
// Save texture (with optional resize)
// =============================================================================

bool ObjExporter::saveTexture(const Image& image, const fs::path& path) const {
    const Pixels& pixels = image.getPixels();
    if (!pixels.isAllocated()) return false;

    return savePixels(pixels, path);
}

bool ObjExporter::savePixels(const Pixels& pixels, const fs::path& path) const {
    if (!pixels.isAllocated()) return false;

    if (maxTextureSize_ > 0 &&
        (pixels.getWidth() > maxTextureSize_ || pixels.getHeight() > maxTextureSize_)) {
        Pixels resized = resizePixels(pixels, maxTextureSize_);
        return resized.save(path);
    }

    return pixels.save(path);
}

// =============================================================================
// Bake vertex colors into a texture
// =============================================================================

Pixels ObjExporter::bakeVertexColors(const Mesh& mesh, int texSize) const {
    Pixels tex;
    tex.allocate(texSize, texSize, 4);  // RGBA

    unsigned char* data = tex.getData();
    // Clear to black transparent
    memset(data, 0, texSize * texSize * 4);

    const auto& vertices = mesh.getVertices();
    const auto& colors = mesh.getColors();
    const auto& texCoords = mesh.getTexCoords();
    const auto& indices = mesh.getIndices();

    if (colors.size() < vertices.size() || texCoords.size() < vertices.size()) {
        logWarning() << "ObjExporter::bakeVertexColors: need both vertex colors and UVs";
        return tex;
    }

    // Rasterize each triangle
    auto rasterizeTriangle = [&](size_t i0, size_t i1, size_t i2) {
        // UV coordinates in pixel space
        float u0 = texCoords[i0].x * texSize, v0 = texCoords[i0].y * texSize;
        float u1 = texCoords[i1].x * texSize, v1 = texCoords[i1].y * texSize;
        float u2 = texCoords[i2].x * texSize, v2 = texCoords[i2].y * texSize;

        const Color& c0 = colors[i0];
        const Color& c1 = colors[i1];
        const Color& c2 = colors[i2];

        // Bounding box
        int minX = max(0, (int)floor(min({u0, u1, u2})));
        int maxX = min(texSize - 1, (int)ceil(max({u0, u1, u2})));
        int minY = max(0, (int)floor(min({v0, v1, v2})));
        int maxY = min(texSize - 1, (int)ceil(max({v0, v1, v2})));

        // Barycentric rasterization
        for (int py = minY; py <= maxY; py++) {
            for (int px = minX; px <= maxX; px++) {
                float x = px + 0.5f;
                float y = py + 0.5f;

                // Barycentric coordinates
                float denom = (v1 - v2) * (u0 - u2) + (u2 - u1) * (v0 - v2);
                if (fabs(denom) < 0.0001f) continue;

                float w0 = ((v1 - v2) * (x - u2) + (u2 - u1) * (y - v2)) / denom;
                float w1 = ((v2 - v0) * (x - u2) + (u0 - u2) * (y - v2)) / denom;
                float w2 = 1.0f - w0 - w1;

                // Inside triangle check (with small margin for edge coverage)
                if (w0 >= -0.01f && w1 >= -0.01f && w2 >= -0.01f) {
                    w0 = max(0.0f, w0);
                    w1 = max(0.0f, w1);
                    w2 = max(0.0f, w2);
                    float wsum = w0 + w1 + w2;
                    w0 /= wsum; w1 /= wsum; w2 /= wsum;

                    float r = c0.r * w0 + c1.r * w1 + c2.r * w2;
                    float g = c0.g * w0 + c1.g * w1 + c2.g * w2;
                    float b = c0.b * w0 + c1.b * w1 + c2.b * w2;
                    float a = c0.a * w0 + c1.a * w1 + c2.a * w2;

                    int offset = (py * texSize + px) * 4;
                    data[offset + 0] = (unsigned char)(min(1.0f, max(0.0f, r)) * 255.0f + 0.5f);
                    data[offset + 1] = (unsigned char)(min(1.0f, max(0.0f, g)) * 255.0f + 0.5f);
                    data[offset + 2] = (unsigned char)(min(1.0f, max(0.0f, b)) * 255.0f + 0.5f);
                    data[offset + 3] = (unsigned char)(min(1.0f, max(0.0f, a)) * 255.0f + 0.5f);
                }
            }
        }
    };

    if (mesh.hasIndices()) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            rasterizeTriangle(indices[i], indices[i+1], indices[i+2]);
        }
    } else {
        for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
            rasterizeTriangle(i, i+1, i+2);
        }
    }

    return tex;
}

// =============================================================================
// Resize pixels (bilinear interpolation)
// =============================================================================

Pixels ObjExporter::resizePixels(const Pixels& src, int maxSize) {
    int srcW = src.getWidth();
    int srcH = src.getHeight();
    int channels = src.getChannels();

    // Calculate target size preserving aspect ratio
    float aspect = static_cast<float>(srcW) / srcH;
    int dstW, dstH;
    if (srcW >= srcH) {
        dstW = maxSize;
        dstH = max(1, static_cast<int>(maxSize / aspect));
    } else {
        dstH = maxSize;
        dstW = max(1, static_cast<int>(maxSize * aspect));
    }

    Pixels dst;
    dst.allocate(dstW, dstH, channels);

    const unsigned char* srcData = src.getData();
    unsigned char* dstData = dst.getData();

    for (int y = 0; y < dstH; y++) {
        float srcY = (y + 0.5f) * srcH / dstH - 0.5f;
        int y0 = max(0, static_cast<int>(srcY));
        int y1 = min(srcH - 1, y0 + 1);
        float fy = srcY - y0;

        for (int x = 0; x < dstW; x++) {
            float srcX = (x + 0.5f) * srcW / dstW - 0.5f;
            int x0 = max(0, static_cast<int>(srcX));
            int x1 = min(srcW - 1, x0 + 1);
            float fx = srcX - x0;

            for (int c = 0; c < channels; c++) {
                float v00 = srcData[(y0 * srcW + x0) * channels + c];
                float v10 = srcData[(y0 * srcW + x1) * channels + c];
                float v01 = srcData[(y1 * srcW + x0) * channels + c];
                float v11 = srcData[(y1 * srcW + x1) * channels + c];

                float v = v00 * (1 - fx) * (1 - fy)
                        + v10 * fx * (1 - fy)
                        + v01 * (1 - fx) * fy
                        + v11 * fx * fy;

                dstData[(y * dstW + x) * channels + c] =
                    static_cast<unsigned char>(min(255.0f, max(0.0f, v + 0.5f)));
            }
        }
    }

    return dst;
}

} // namespace tcx
