#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "tcxObjLoader.h"
#include <fstream>
#include <unordered_map>

using namespace std;

namespace tcx::obj {

// =============================================================================
// Load
// =============================================================================

bool ObjLoader::load(const fs::path& path) {
    clear();

    fs::path objPath = path.is_absolute() ? path : fs::path(getDataPath(path.string()));

    if (!fs::exists(objPath)) {
        logError() << "ObjLoader::load: file not found: " << objPath;
        return false;
    }

    fs::path baseDir = objPath.parent_path().string() + "/";

    tinyobj::attrib_t attrib;
    vector<tinyobj::shape_t> shapes;
    vector<tinyobj::material_t> materials;
    string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                               objPath.string().c_str(),
                               baseDir.string().c_str());

    if (!warn.empty()) {
        logWarning() << "ObjLoader: " << warn;
    }
    if (!err.empty()) {
        logError() << "ObjLoader: " << err;
    }
    if (!ok) {
        return false;
    }

    // Load material textures
    vector<fs::path> texturePaths(materials.size());
    for (size_t i = 0; i < materials.size(); i++) {
        if (!materials[i].diffuse_texname.empty()) {
            texturePaths[i] = baseDir / materials[i].diffuse_texname;
        }
    }

    // Process each shape
    for (auto& shape : shapes) {
        ObjGroup group;
        group.name = shape.name;

        // Key: (vertex_index, normal_index, texcoord_index) -> unified index
        struct IndexHash {
            size_t operator()(const tuple<int,int,int>& t) const {
                auto h1 = hash<int>{}(get<0>(t));
                auto h2 = hash<int>{}(get<1>(t));
                auto h3 = hash<int>{}(get<2>(t));
                return h1 ^ (h2 << 16) ^ (h3 << 8);
            }
        };
        unordered_map<tuple<int,int,int>, unsigned int, IndexHash> vertexMap;

        bool hasNormals = !attrib.normals.empty();
        bool hasTexCoords = !attrib.texcoords.empty();

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];

            // Triangulate N-gon (fan triangulation)
            for (int v = 1; v < fv - 1; v++) {
                // Triangle: vertex 0, v, v+1
                int triIndices[3] = {0, v, v + 1};

                for (int ti = 0; ti < 3; ti++) {
                    auto& idx = shape.mesh.indices[indexOffset + triIndices[ti]];

                    auto key = make_tuple(idx.vertex_index, idx.normal_index, idx.texcoord_index);
                    auto it = vertexMap.find(key);

                    if (it != vertexMap.end()) {
                        group.mesh.addIndex(it->second);
                    } else {
                        unsigned int newIdx = static_cast<unsigned int>(group.mesh.getNumVertices());

                        // Position
                        float vx = attrib.vertices[3 * idx.vertex_index + 0];
                        float vy = attrib.vertices[3 * idx.vertex_index + 1];
                        float vz = attrib.vertices[3 * idx.vertex_index + 2];
                        group.mesh.addVertex(vx, vy, vz);

                        // Normal
                        if (hasNormals && idx.normal_index >= 0) {
                            float nx = attrib.normals[3 * idx.normal_index + 0];
                            float ny = attrib.normals[3 * idx.normal_index + 1];
                            float nz = attrib.normals[3 * idx.normal_index + 2];
                            group.mesh.addNormal(nx, ny, nz);
                        }

                        // Texture coordinate
                        if (hasTexCoords && idx.texcoord_index >= 0) {
                            float u = attrib.texcoords[2 * idx.texcoord_index + 0];
                            float v_coord = attrib.texcoords[2 * idx.texcoord_index + 1];
                            // OBJ V=0 is bottom, TrussC V=0 is top
                            group.mesh.addTexCoord(u, 1.0f - v_coord);
                        }

                        // Vertex color (if present in OBJ)
                        if (attrib.colors.size() > 3 * static_cast<size_t>(idx.vertex_index) + 2) {
                            float cr = attrib.colors[3 * idx.vertex_index + 0];
                            float cg = attrib.colors[3 * idx.vertex_index + 1];
                            float cb = attrib.colors[3 * idx.vertex_index + 2];
                            group.mesh.addColor(cr, cg, cb, 1.0f);
                        }

                        vertexMap[key] = newIdx;
                        group.mesh.addIndex(newIdx);
                    }
                }
            }

            indexOffset += fv;
        }

        // Load material and texture from first material used by this shape
        if (!shape.mesh.material_ids.empty()) {
            int matId = shape.mesh.material_ids[0];
            if (matId >= 0 && matId < static_cast<int>(materials.size())) {
                auto& mtl = materials[matId];
                // Convert Phong MTL params to PBR Material
                Color diffuse(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
                Color specular(mtl.specular[0], mtl.specular[1], mtl.specular[2]);
                Color emissive(mtl.emission[0], mtl.emission[1], mtl.emission[2]);
                group.material = Material::fromPhong(diffuse, specular, mtl.shininess, emissive);

                // Load diffuse texture
                if (!texturePaths[matId].empty()) {
                    if (group.texture.load(texturePaths[matId])) {
                        group.hasTexture = true;
                    } else {
                        logWarning() << "ObjLoader: failed to load texture: " << texturePaths[matId];
                    }
                }
            }
        }

        // Auto-compute normals if not present
        if (!group.mesh.hasNormals() && group.mesh.hasIndices()) {
            computeNormals(group.mesh);
        }

        groups_.push_back(std::move(group));
    }

    // If no shapes but there are vertices, create a single group
    if (shapes.empty() && !attrib.vertices.empty()) {
        ObjGroup group;
        group.name = "default";
        for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
            group.mesh.addVertex(attrib.vertices[i], attrib.vertices[i+1], attrib.vertices[i+2]);
        }
        groups_.push_back(std::move(group));
    }

    logNotice() << "ObjLoader: loaded " << objPath << " (" << groups_.size() << " groups)";
    return true;
}

// =============================================================================
// Get merged mesh
// =============================================================================

Mesh ObjLoader::getMesh() const {
    Mesh merged;
    for (auto& group : groups_) {
        merged.append(group.mesh);
    }
    return merged;
}

// =============================================================================
// Compute per-vertex normals (smooth shading)
// =============================================================================

void ObjLoader::computeNormals(Mesh& mesh) {
    auto& vertices = mesh.getVertices();
    auto& indices = mesh.getIndices();
    int numVerts = mesh.getNumVertices();

    // Initialize normals to zero
    vector<Vec3> normals(numVerts, Vec3(0, 0, 0));

    // Accumulate face normals per vertex
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        if (i0 >= (unsigned)numVerts || i1 >= (unsigned)numVerts || i2 >= (unsigned)numVerts)
            continue;

        Vec3 e1 = vertices[i1] - vertices[i0];
        Vec3 e2 = vertices[i2] - vertices[i0];
        Vec3 fn = e1.cross(e2);

        normals[i0] = normals[i0] + fn;
        normals[i1] = normals[i1] + fn;
        normals[i2] = normals[i2] + fn;
    }

    // Normalize and add to mesh
    for (auto& n : normals) {
        float len = n.length();
        if (len > 0.0001f) {
            n = n / len;
        } else {
            n = Vec3(0, 0, 1);
        }
        mesh.addNormal(n);
    }
}

} // namespace tcx::obj
