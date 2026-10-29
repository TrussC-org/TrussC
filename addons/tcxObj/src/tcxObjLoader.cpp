#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "tcxObjLoader.h"
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <climits>
#include <stdexcept>

using namespace std;

namespace tcx::obj {

// Recovery must also work when every subsequent allocation fails. Use the
// logger directly inside a best-effort diagnostic boundary.
static LoadResult recoveryFailure(const char* reason, const char* detail = nullptr) noexcept {
    LoadResult result{LoadError::DecodeFailed, {}};
    try {
        result.message = reason;
        if (detail) result.message += detail;
        getLogger().log(LogLevel::Error, "[ObjLoader] " + result.message);
    } catch (...) {
        // The error code and cleared model remain usable without diagnostics.
    }
    return result;
}

// =============================================================================
// Load
// =============================================================================

LoadResult ObjLoader::load(const fs::path& path) try {
    clear();

    fs::path objPath = tc::internal::resolveLoadPath(path);

    auto entry = tc::internal::checkLoadFile(objPath, "ObjLoader");
    if (!entry) return entry;

    fs::path baseDir = objPath.parent_path();

    tinyobj::attrib_t attrib;
    vector<tinyobj::shape_t> shapes;
    vector<tinyobj::material_t> materials;
    string warn, err;

    // This bundled tinyobjloader converts UTF-8 to wide paths for both
    // LoadObj and MaterialFileReader on Windows (including long paths).
    bool ok = tinyobj::detail::LoadObjWithoutTags(&attrib, &shapes, &materials, &warn, &err,
                               pathToUtf8(objPath).c_str(),
                               (pathToUtf8(baseDir) + "/").c_str());

    // Preserve pending parser Warnings even when a later OBJ statement fails.
    if (!warn.empty()) {
        getLogger().log(LogLevel::Warning, "ObjLoader: " + warn);
    }
    if (!ok) {
        string message = (err.empty() ? "failed to parse OBJ" : err) +
                         ": " + tc::internal::pathToDisplayUtf8(objPath);
        getLogger().log(LogLevel::Error, "[ObjLoader] " + message);
        return LoadResult::fail(LoadError::DecodeFailed, message);
    }
    // MTL diagnostics are optional-material failures, matching a missing MTL.
    // An iostream open failure (including swallowed bad_alloc) follows this
    // Warning path; failure to allocate the diagnostic reaches recovery below.
    warn = err;
    if (any_of(materials.begin(), materials.end(), [](const tinyobj::material_t& material) {
            return !material.unknown_parameter.empty();
        })) warn += "Unsupported MTL statements; using available material properties.\n";
    if (!warn.empty()) {
        getLogger().log(LogLevel::Warning, "ObjLoader: " + warn);
    }

    // Load material textures
    vector<fs::path> texturePaths(materials.size());
    for (size_t i = 0; i < materials.size(); i++) {
        if (!materials[i].diffuse_texname.empty()) {
            texturePaths[i] = baseDir / utf8ToPath(materials[i].diffuse_texname);
        }
    }

    size_t skippedFaces = 0;

    // Image's noexcept move can allocate. Reserve before constructing any
    // group, then fill in place so neither insertion nor growth moves Images.
    groups_.reserve(shapes.empty() ? 1 : shapes.size());
    // Process each shape
    for (auto& shape : shapes) {
        // Bound (abi): Mesh exposes signed int vertex counts. Overflow rejects the model and is reported by the load result.
        if (shape.mesh.indices.size() > static_cast<size_t>(INT_MAX))
            throw length_error("mesh indices exceed the Mesh index width");
        // Bound (security): face counts must fit the parsed index table before reads. Overflow rejects the model and is reported by the load result.
        size_t remainingIndices = shape.mesh.indices.size();
        for (auto count : shape.mesh.num_face_vertices) {
            if (count > remainingIndices)
                throw runtime_error("face count exceeds its index table");
            remainingIndices -= count;
        }
        groups_.emplace_back();
        auto& group = groups_.back();
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
        vector<bool> missingNormals;

        // Check each face index against the list it is read from.
        const size_t numPositions = attrib.vertices.size() / 3;
        const size_t numNormals = attrib.normals.size() / 3;
        const size_t numTexCoords = attrib.texcoords.size() / 2;
        auto indexInRange = [&](const tinyobj::index_t& idx) {
            if (idx.vertex_index < 0 || static_cast<size_t>(idx.vertex_index) >= numPositions) {
                return false;
            }
            if (hasNormals && idx.normal_index >= 0 &&
                static_cast<size_t>(idx.normal_index) >= numNormals) {
                return false;
            }
            if (hasTexCoords && idx.texcoord_index >= 0 &&
                static_cast<size_t>(idx.texcoord_index) >= numTexCoords) {
                return false;
            }
            return true;
        };

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];

            // Skip a face with any index out of range
            bool faceOk = true;
            for (int v = 0; v < fv && faceOk; v++) {
                faceOk = indexInRange(shape.mesh.indices[indexOffset + v]);
            }
            if (!faceOk) {
                skippedFaces++;
                indexOffset += fv;
                continue;
            }

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

                        // Offsets are computed in size_t
                        size_t vi = static_cast<size_t>(idx.vertex_index);

                        // Position
                        float vx = attrib.vertices[3 * vi + 0];
                        float vy = attrib.vertices[3 * vi + 1];
                        float vz = attrib.vertices[3 * vi + 2];
                        group.mesh.addVertex(vx, vy, vz);

                        // Normal
                        if (hasNormals && idx.normal_index >= 0) {
                            size_t ni = static_cast<size_t>(idx.normal_index);
                            float nx = attrib.normals[3 * ni + 0];
                            float ny = attrib.normals[3 * ni + 1];
                            float nz = attrib.normals[3 * ni + 2];
                            group.mesh.addNormal(nx, ny, nz);
                            missingNormals.push_back(false);
                        } else if (hasNormals) {
                            group.mesh.addNormal(0, 0, 0);
                            missingNormals.push_back(true);
                        }

                        // Texture coordinate
                        if (hasTexCoords && idx.texcoord_index >= 0) {
                            size_t ti = static_cast<size_t>(idx.texcoord_index);
                            float u = attrib.texcoords[2 * ti + 0];
                            float v_coord = attrib.texcoords[2 * ti + 1];
                            // OBJ V=0 is bottom, TrussC V=0 is top
                            group.mesh.addTexCoord(u, 1.0f - v_coord);
                        } else if (hasTexCoords) {
                            group.mesh.addTexCoord(0, 0);
                        }

                        // Vertex color (if present in OBJ)
                        if (attrib.colors.size() > 3 * vi + 2) {
                            float cr = attrib.colors[3 * vi + 0];
                            float cg = attrib.colors[3 * vi + 1];
                            float cb = attrib.colors[3 * vi + 2];
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
                        getLogger().log(LogLevel::Warning, "ObjLoader: failed to load texture: " +
                                        tc::internal::pathToDisplayUtf8(texturePaths[matId]));
                    }
                }
            }
        }

        // Auto-compute normals if not present
        if (!group.mesh.hasNormals() && group.mesh.hasIndices()) {
            computeNormals(group.mesh);
        } else if (find(missingNormals.begin(), missingNormals.end(), true) != missingNormals.end()) {
            computeNormals(group.mesh, missingNormals);
        }

    }

    if (skippedFaces > 0) {
        getLogger().log(LogLevel::Warning, "ObjLoader: skipped " + to_string(skippedFaces) +
                        " face(s) with an index past the end of the vertex, normal or texcoord list: " +
                        tc::internal::pathToDisplayUtf8(objPath));
    }

    // If no shapes but there are vertices, create a single group
    if (shapes.empty() && !attrib.vertices.empty()) {
        // Bound (abi): Mesh exposes signed int vertex counts. Overflow rejects the model and is reported by the load result.
        if (attrib.vertices.size() / 3 > static_cast<size_t>(INT_MAX))
            throw length_error("point count exceeds the Mesh index width");
        groups_.emplace_back();
        auto& group = groups_.back();
        group.name = "default";
        for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
            group.mesh.addVertex(attrib.vertices[i], attrib.vertices[i+1], attrib.vertices[i+2]);
        }
    }

    getLogger().log(LogLevel::Notice, "ObjLoader: loaded " + tc::internal::pathToDisplayUtf8(objPath) +
                    " (" + to_string(groups_.size()) + " groups)");
    return LoadResult::success();
} catch (const bad_alloc&) {
    clear();
    return recoveryFailure("not enough memory for the model data");
} catch (const exception& e) {
    clear();
    return recoveryFailure("failed to read model: ", e.what());
} catch (...) {
    clear();
    return recoveryFailure("failed to read model");
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

void ObjLoader::computeNormals(Mesh& mesh, const vector<bool>& missingNormals) {
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

        for (auto index : {i0, i1, i2}) {
            if (missingNormals.empty() || missingNormals[index]) {
                normals[index] = normals[index] + fn;
            }
        }
    }

    // Fill only missing normals; authored normals keep their exact values.
    for (size_t i = 0; i < normals.size(); ++i) {
        if (!missingNormals.empty() && !missingNormals[i]) continue;
        auto& n = normals[i];
        float len = n.length();
        if (len > 0.0f) {
            n = n / len;
        } else {
            n = Vec3(0, 0, 1);
        }
        if (missingNormals.empty()) {
            mesh.addNormal(n);
        } else {
            mesh.setNormal(i, n);
        }
    }
}

} // namespace tcx::obj
