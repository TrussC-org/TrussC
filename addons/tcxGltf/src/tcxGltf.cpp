#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "tcxGltf.h"

#include <cstring>
#include <filesystem>

using namespace std;
using namespace tc;

namespace tcx {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Read accessor data as float array (handles all component types)
static vector<float> readAccessorFloats(const cgltf_accessor* acc) {
    vector<float> out(acc->count * cgltf_num_components(acc->type));
    cgltf_accessor_unpack_floats(acc, out.data(), out.size());
    return out;
}

// Read accessor data as uint32 indices
static vector<unsigned int> readAccessorIndices(const cgltf_accessor* acc) {
    vector<unsigned int> out(acc->count);
    for (cgltf_size i = 0; i < acc->count; i++) {
        out[i] = (unsigned int)cgltf_accessor_read_index(acc, i);
    }
    return out;
}

// Compute world transform by walking up the node hierarchy
static Mat4 getWorldTransform(const cgltf_node* node) {
    float m[16];
    cgltf_node_transform_world(node, m);
    // cgltf outputs column-major (OpenGL convention); TrussC Mat4 is row-major
    Mat4 result;
    result.m[0]  = m[0];  result.m[1]  = m[4];  result.m[2]  = m[8];  result.m[3]  = m[12];
    result.m[4]  = m[1];  result.m[5]  = m[5];  result.m[6]  = m[9];  result.m[7]  = m[13];
    result.m[8]  = m[2];  result.m[9]  = m[6];  result.m[10] = m[10]; result.m[11] = m[14];
    result.m[12] = m[3];  result.m[13] = m[7];  result.m[14] = m[11]; result.m[15] = m[15];
    return result;
}

// ---------------------------------------------------------------------------
// Texture loading
// ---------------------------------------------------------------------------

static Texture* loadGltfTexture(const cgltf_texture* tex,
                                 vector<unique_ptr<Texture>>& store,
                                 const string& baseDir,
                                 const cgltf_data* data) {
    if (!tex || !tex->image) return nullptr;

    const cgltf_image* img = tex->image;

    Pixels pixels;

    if (img->buffer_view) {
        // Embedded image data (GLB or base64)
        const uint8_t* ptr = (const uint8_t*)img->buffer_view->buffer->data
                           + img->buffer_view->offset;
        size_t len = img->buffer_view->size;
        pixels.loadFromMemory(ptr, (int)len);
    } else if (img->uri) {
        // External file reference
        string uri(img->uri);
        // Skip data URIs (base64 inline) for now
        if (uri.rfind("data:", 0) == 0) return nullptr;

        string imgPath = baseDir + "/" + uri;
        pixels.load(imgPath);
    }

    if (pixels.getWidth() == 0) return nullptr;

    auto t = make_unique<Texture>();
    t->allocate(pixels, TextureUsage::Immutable, true);  // with mipmaps
    Texture* raw = t.get();
    store.push_back(std::move(t));
    return raw;
}

// ---------------------------------------------------------------------------
// Material loading
// ---------------------------------------------------------------------------

static PbrMaterial loadGltfMaterial(const cgltf_material* mat,
                                    vector<unique_ptr<Texture>>& texStore,
                                    const string& baseDir,
                                    const cgltf_data* data) {
    PbrMaterial m;
    if (!mat) return m;

    if (mat->has_pbr_metallic_roughness) {
        const auto& pbr = mat->pbr_metallic_roughness;
        m.setBaseColor(pbr.base_color_factor[0],
                       pbr.base_color_factor[1],
                       pbr.base_color_factor[2],
                       pbr.base_color_factor[3]);
        m.setMetallic(pbr.metallic_factor);
        m.setRoughness(pbr.roughness_factor);

        if (pbr.base_color_texture.texture) {
            Texture* tex = loadGltfTexture(pbr.base_color_texture.texture,
                                           texStore, baseDir, data);
            if (tex) m.setBaseColorTexture(tex);
        }
        if (pbr.metallic_roughness_texture.texture) {
            Texture* tex = loadGltfTexture(pbr.metallic_roughness_texture.texture,
                                           texStore, baseDir, data);
            if (tex) m.setMetallicRoughnessTexture(tex);
        }
    }

    if (mat->normal_texture.texture) {
        Texture* tex = loadGltfTexture(mat->normal_texture.texture,
                                       texStore, baseDir, data);
        if (tex) m.setNormalMap(tex);
    }

    if (mat->emissive_texture.texture) {
        Texture* tex = loadGltfTexture(mat->emissive_texture.texture,
                                       texStore, baseDir, data);
        if (tex) m.setEmissiveTexture(tex);
    }
    m.setEmissive(mat->emissive_factor[0],
                  mat->emissive_factor[1],
                  mat->emissive_factor[2]);
    if (mat->emissive_factor[0] > 0 || mat->emissive_factor[1] > 0 ||
        mat->emissive_factor[2] > 0) {
        m.setEmissiveStrength(1.0f);
    }

    if (mat->occlusion_texture.texture) {
        Texture* tex = loadGltfTexture(mat->occlusion_texture.texture,
                                       texStore, baseDir, data);
        if (tex) m.setOcclusionTexture(tex);
    }

    return m;
}

// ---------------------------------------------------------------------------
// Mesh loading
// ---------------------------------------------------------------------------

static Mesh loadGltfPrimitive(const cgltf_primitive* prim) {
    Mesh mesh;
    mesh.setMode(PrimitiveMode::Triangles);

    // Attributes
    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        const cgltf_attribute& attr = prim->attributes[a];
        const cgltf_accessor* acc = attr.data;

        if (attr.type == cgltf_attribute_type_position) {
            auto floats = readAccessorFloats(acc);
            for (size_t i = 0; i + 2 < floats.size(); i += 3) {
                mesh.addVertex(floats[i], floats[i+1], floats[i+2]);
            }
        }
        else if (attr.type == cgltf_attribute_type_normal) {
            auto floats = readAccessorFloats(acc);
            for (size_t i = 0; i + 2 < floats.size(); i += 3) {
                mesh.addNormal(floats[i], floats[i+1], floats[i+2]);
            }
        }
        else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
            auto floats = readAccessorFloats(acc);
            for (size_t i = 0; i + 1 < floats.size(); i += 2) {
                mesh.addTexCoord(floats[i], floats[i+1]);
            }
        }
        else if (attr.type == cgltf_attribute_type_tangent) {
            auto floats = readAccessorFloats(acc);
            for (size_t i = 0; i + 3 < floats.size(); i += 4) {
                mesh.addTangent(floats[i], floats[i+1], floats[i+2], floats[i+3]);
            }
        }
    }

    // Fill in missing normals/UVs/tangents with defaults
    size_t vertCount = (size_t)mesh.getNumVertices();
    while ((size_t)mesh.getNumNormals() < vertCount) mesh.addNormal(0, 1, 0);
    while (mesh.getTexCoords().size() < vertCount) mesh.addTexCoord(0, 0);
    while ((size_t)mesh.getNumTangents() < vertCount) mesh.addTangent(1, 0, 0, 1);

    // Indices
    if (prim->indices) {
        auto indices = readAccessorIndices(prim->indices);
        for (auto idx : indices) {
            mesh.addIndex(idx);
        }
    }

    return mesh;
}

// Bake a world transform into mesh vertices and normals in-place.
static void bakeTransform(Mesh& mesh, const Mat4& m) {
    for (auto& v : mesh.getVertices()) {
        float x = m.m[0]*v.x + m.m[1]*v.y + m.m[2]*v.z + m.m[3];
        float y = m.m[4]*v.x + m.m[5]*v.y + m.m[6]*v.z + m.m[7];
        float z = m.m[8]*v.x + m.m[9]*v.y + m.m[10]*v.z + m.m[11];
        v = Vec3(x, y, z);
    }
    for (auto& n : mesh.getNormals()) {
        float x = m.m[0]*n.x + m.m[1]*n.y + m.m[2]*n.z;
        float y = m.m[4]*n.x + m.m[5]*n.y + m.m[6]*n.z;
        float z = m.m[8]*n.x + m.m[9]*n.y + m.m[10]*n.z;
        float len = sqrt(x*x + y*y + z*z);
        if (len > 1e-6f) { x /= len; y /= len; z /= len; }
        n = Vec3(x, y, z);
    }
}

// ---------------------------------------------------------------------------
// GltfModel implementation
// ---------------------------------------------------------------------------

bool GltfModel::load(const string& path) {
    nodes_.clear();
    textures_.clear();
    loaded_ = false;

    string resolved = getDataPath(path);

    // Parse with cgltf
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, resolved.c_str(), &data);
    if (result != cgltf_result_success) {
        logWarning() << "[GltfModel] failed to parse: " << resolved;
        return false;
    }

    // Load external buffers (for .gltf with separate .bin)
    result = cgltf_load_buffers(&options, data, resolved.c_str());
    if (result != cgltf_result_success) {
        logWarning() << "[GltfModel] failed to load buffers: " << resolved;
        cgltf_free(data);
        return false;
    }

    // Base directory for relative texture paths
    string baseDir = filesystem::path(resolved).parent_path().string();

    // Iterate the default scene (or scene 0)
    const cgltf_scene* scene = data->scene ? data->scene : &data->scenes[0];

    // Recursive node visitor
    function<void(const cgltf_node*)> visitNode = [&](const cgltf_node* node) {
        if (node->mesh) {
            Mat4 worldXform = getWorldTransform(node);

            for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
                const cgltf_primitive* prim = &node->mesh->primitives[p];
                if (prim->type != cgltf_primitive_type_triangles) continue;

                Node entry;
                entry.mesh = loadGltfPrimitive(prim);
                entry.material = loadGltfMaterial(prim->material, textures_,
                                                  baseDir, data);
                entry.transform = worldXform;
                entry.name = node->name ? node->name : "";

                // Bake world transform into vertex positions and normals
                // so draw() doesn't need multMatrix
                bakeTransform(entry.mesh, worldXform);

                nodes_.push_back(std::move(entry));
            }
        }

        for (cgltf_size c = 0; c < node->children_count; c++) {
            visitNode(node->children[c]);
        }
    };

    for (cgltf_size n = 0; n < scene->nodes_count; n++) {
        visitNode(scene->nodes[n]);
    }

    cgltf_free(data);
    loaded_ = true;
    logNotice() << "[GltfModel] loaded " << nodes_.size() << " nodes, "
                << textures_.size() << " textures from " << path;
    return true;
}

void GltfModel::draw() const {
    for (const auto& node : nodes_) {
        PbrMaterial mat = node.material;
        setPbrMaterial(mat);
        node.mesh.draw();
        clearPbrMaterial();
    }
}

} // namespace tcx
