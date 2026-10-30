// cgltf is not vendored in this repo: CMakeLists.txt fetches the upstream
// single header at tag v1.15 (github.com/jkuhlmann/cgltf), without patches;
// see PROVENANCE.md.
// load() runs its own range checks (checkDataRanges()) and then
// cgltf_validate() on every file, before any accessor, index or image data
// is read.
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>

namespace {
thread_local bool invalidModelInteger = false;
long long modelInteger(const char* text) noexcept {
    char* end = nullptr;
    errno = 0;
    long long value = std::strtoll(text, &end, 10);
    // Bound (abi): cgltf integer sizes must fit its conversion and the platform size type. Overflow rejects the model and is reported by LoadResult.
    if (errno || end == text || *end || value < 0 ||
        static_cast<unsigned long long>(value) > (std::numeric_limits<size_t>::max)()) {
        invalidModelInteger = true;
        return 0;
    }
    return value;
}
int modelInt(const char* text) noexcept {
    long long value = modelInteger(text);
    // Bound (abi): cgltf reference and enum fields use int. Overflow rejects the model and is reported by LoadResult.
    if (value > INT_MAX) {
        invalidModelInteger = true;
        return 0;
    }
    return static_cast<int>(value);
}
} // namespace

// cgltf's default atoi/atoll silently narrow file counts and references.
#define CGLTF_ATOI(text) modelInt(text)
#define CGLTF_ATOLL(text) modelInteger(text)
#define CGLTF_IMPLEMENTATION
#include "tcxGltfCgltf.h"

#include "tcxGltf.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include <system_error>

using namespace std;
using namespace tc;

namespace tcx::gltf {

// cgltf receives an empty model filename. The callback keeps the native path
// intact; external buffer URIs are decoded on cgltf's temporary copy before
// reaching this callback (cgltf_load_buffer_file with an empty base filename).
// No cgltf operation opens a narrow filename, and '%' in the model directory
// is never interpreted as a URI escape.
struct GltfFileContext {
    fs::path modelPath;
    bool readingModel = true;
    explicit GltfFileContext(const fs::path& path) : modelPath(path) {}
};

namespace internal {
// These are implementation-only helpers, also exercised with synthetic sizes.
bool jsonSkipFits(size_t tokens, size_t members) {
    // Bound (abi): cgltf_skip_json's accumulated int end is bounded by tokens + object members; overflow rejects via LoadResult.
    return tokens <= static_cast<size_t>(INT_MAX) && members <= static_cast<size_t>(INT_MAX) - tokens;
}
static thread_local size_t jsonPaddingForTests = 0;
static thread_local size_t jsonObjectMembersForTests = 0;
void setJsonPaddingForTests(size_t bytes) { jsonPaddingForTests = bytes; }
void setJsonObjectMembersForTests(size_t members) { jsonObjectMembersForTests = members; }
bool jsonObjectMembersFit(size_t members) {
    // Bound (abi): each object member product must fit cgltf's int skip arithmetic; overflow rejects via LoadResult.
    return members <= static_cast<size_t>(INT_MAX) / 2;
}
bool jsonOffsetsFit(size_t bytes) {
    // Bound (abi): JSON offsets must fit jsmn's ptrdiff_t fields; unrepresentable offsets reject via LoadResult.
    return bytes <= static_cast<size_t>(PTRDIFF_MAX);
}
bool jsonTokenFits(size_t count, size_t length, bool stringToken) {
    // Bound (abi): token counts and indices must fit int and the sentinel table must fit size_t; overflow rejects via LoadResult.
    if (count > INT_MAX || count > SIZE_MAX / sizeof(jsmntok_t) - 1) return false;
    // Bound (abi): individual token lengths and string terminator additions must fit int; overflow rejects via LoadResult.
    return length <= static_cast<size_t>(INT_MAX) - (stringToken ? 1 : 0);
}
} // namespace internal

// Count actual lexical tokens before jsmn's int counters can overflow, and
// guard object-member products and individual token widths before cgltf parses.
static bool modelJsonFits(const unsigned char* bytes, size_t count) {
    const unsigned char* json = bytes;
    size_t jsonBytes = count;
    if (count >= 4 && memcmp(bytes, "glTF", 4) == 0) {
        if (count < 20) return false;
        uint32_t total, jsonLength;
        memcpy(&total, bytes + 8, 4);
        memcpy(&jsonLength, bytes + 12, 4);
        if (total < 20 || total != count || jsonLength % 4 || jsonLength > total - 20) return false;
        size_t rest = total - 20 - jsonLength;
        if (rest) {
            if (rest < 8) return false;
            uint32_t binLength;
            memcpy(&binLength, bytes + 20 + jsonLength, 4);
            if (binLength % 4 || binLength > rest - 8) return false;
        }
        json = bytes + 20;
        jsonBytes = jsonLength;
    }
    // Test seam: virtual trailing whitespace exercises the actual load-time
    // document-size gate without allocating or scanning a multi-GiB fixture.
    const size_t physicalBytes = jsonBytes;
    if (internal::jsonPaddingForTests > SIZE_MAX - jsonBytes) return false;
    jsonBytes += internal::jsonPaddingForTests;
    if (!internal::jsonOffsetsFit(jsonBytes)) return false;
    struct Container { bool object; bool value; size_t members; };
    // Bound (security): reject untrusted JSON nesting beyond 64 containers via LoadResult; node hierarchy traversal remains iterative and unrestricted in depth.
    Container containers[64]{};
    size_t depth = 0, tokens = 0, tokenStart = 0, members = 0;
    bool quoted = false, escaped = false, primitive = false;
    size_t scannedBytes = physicalBytes;
    for (size_t i = 0; i < physicalBytes; ++i) {
        unsigned char c = json[i];
        // jsmn treats NUL as end of input, including JSON chunk padding.
        if (!c) { scannedBytes = i; break; }
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') {
                if (!internal::jsonTokenFits(tokens, i - tokenStart, true)) return false;
                quoted = false;
            }
            continue;
        }
        bool delimiter = c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                         c == ':' || c == ',' || c == ']' || c == '}';
        if (primitive) {
            if (!delimiter) continue;
            if (!internal::jsonTokenFits(tokens, i - tokenStart, false)) return false;
            primitive = false;
        }
        if (depth && c == ':') containers[depth - 1].value = true;
        if (depth && c == ',') containers[depth - 1].value = false;
        // jsmn counts a key in its object, but attaches the value to the key.
        // Count keys even when a malformed member has no colon/value.
        if ((c == '"' || c == '{' || c == '[' || !delimiter) &&
            depth && containers[depth - 1].object && !containers[depth - 1].value) {
            if (!internal::jsonObjectMembersFit(++containers[depth - 1].members) ||
                !internal::jsonSkipFits(tokens, ++members)) return false;
        }
        if (c == '"') {
            quoted = true;
            tokenStart = i + 1;
        } else if (c == '{' || c == '[') {
            if (depth == 64) return false;
            containers[depth++] = {c == '{', false, internal::jsonObjectMembersForTests};
            if (c == '{') {
                // Synthetic per-object members exercise cumulative end arithmetic.
                if (internal::jsonObjectMembersForTests > SIZE_MAX - members) return false;
                members += internal::jsonObjectMembersForTests;
            }
        } else if (c == '}' || c == ']') {
            if (!depth) return false;
            --depth;
            continue;
        } else if (delimiter) continue;
        else {
            primitive = true;
            tokenStart = i;
        }
        // Checked at each new token, before jsmn increments its signed count.
        if (!internal::jsonTokenFits(++tokens, 0, false) ||
            !internal::jsonSkipFits(tokens, members)) return false;
    }
    if (quoted || depth != 0 || !tokens) return false;
    if (primitive && !internal::jsonTokenFits(tokens, scannedBytes - tokenStart, false)) return false;
    jsmn_parser parser;
    jsmn_init(&parser);
    int parsedTokens = jsmn_parse(&parser, reinterpret_cast<const char*>(json), physicalBytes, nullptr, 0);
    return parsedTokens > 0 && static_cast<size_t>(parsedTokens) == tokens;
}

static void releaseGltfFile(const cgltf_memory_options* memory,
                            const cgltf_file_options*, void* data) noexcept {
    if (memory->free_func) memory->free_func(memory->user_data, data);
    else std::free(data);
}

static cgltf_result readGltfFile(const cgltf_memory_options* memory,
                                 const cgltf_file_options* options,
                                 const char* uri, cgltf_size* size, void** data) noexcept {
    auto& context = *static_cast<GltfFileContext*>(options->user_data);
    try {
        fs::path path = context.modelPath;
        if (!context.readingModel) {
            // operator/ appends a relative URI and keeps an absolute one.
            path = path.parent_path() / utf8ToPath(uri);
        }
        ifstream file(path, ios::binary | ios::ate);
        if (!file) return cgltf_result_file_not_found;
        auto length = file.tellg();
        if (length < 0 || static_cast<uintmax_t>(length) > SIZE_MAX)
            return cgltf_result_io_error;
        cgltf_size bytes = *size ? *size : static_cast<cgltf_size>(length);
        // Bound (abi): file reads must fit both the available bytes and the stream count width. Overflow rejects the model and is reported by LoadResult.
        if (bytes > static_cast<cgltf_size>(length) ||
            bytes > static_cast<cgltf_size>((numeric_limits<streamsize>::max)()))
            return cgltf_result_io_error;
        file.seekg(0);
        void* buffer = memory->alloc_func ? memory->alloc_func(memory->user_data, bytes)
                                         : std::malloc(bytes);
        if (!buffer) return cgltf_result_out_of_memory;
        if (!file.read(static_cast<char*>(buffer), static_cast<streamsize>(bytes))) {
            releaseGltfFile(memory, options, buffer);
            return cgltf_result_io_error;
        }
        if (context.readingModel && !modelJsonFits(static_cast<const unsigned char*>(buffer), bytes)) {
            releaseGltfFile(memory, options, buffer);
            return cgltf_result_invalid_gltf;
        }
        *size = bytes;
        *data = buffer;
        return cgltf_result_success;
    } catch (const bad_alloc&) {
        return cgltf_result_out_of_memory;
    } catch (...) {
        return cgltf_result_io_error;
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// True when `count` elements of `elemSize` bytes, `stride` bytes apart and
// starting at `offset`, lie inside `size` bytes. Written so that no step can
// wrap around, whatever values the file holds.
static bool rangeFits(cgltf_size size, cgltf_size offset, cgltf_size stride,
                      cgltf_size elemSize, cgltf_size count) {
    if (offset > size) return false;
    if (count == 0) return true;
    // Bound (security): untrusted file buffer ranges must fit actual storage before access. Overflow rejects the model and is reported by LoadResult.
    if (elemSize > size - offset) return false;
    // The last element starts at offset + stride * (count - 1).
    cgltf_size room = size - offset - elemSize;
    // Bound (security): untrusted file accessor strides and counts must fit actual storage before access. Overflow rejects the model and is reported by LoadResult.
    return stride == 0 || count - 1 <= room / stride;
}

// Reads sparse index `k` (component type 8u, 16u or 32u) from `data`.
static cgltf_size readSparseIndex(const uint8_t* data, cgltf_component_type type,
                                  cgltf_size k) {
    switch (type) {
        case cgltf_component_type_r_8u:
            return data[k];
        case cgltf_component_type_r_16u: {
            uint16_t v;
            memcpy(&v, data + k * sizeof(v), sizeof(v));
            return v;
        }
        default: {
            uint32_t v;
            memcpy(&v, data + k * sizeof(v), sizeof(v));
            return v;
        }
    }
}

// Our own range checks, run before cgltf_validate(). Returns nullptr when
// the data passes, or the reason it is refused (for the warning).
//   - Every buffer view lies inside its buffer, and every accessor
//     (including sparse parts) inside its buffer view.
//   - Every accessor, with or without a buffer view, has a count whose float
//     array size (count * components * sizeof(float)) can be addressed in
//     size_t; a count that cannot is refused. This is not a limit on model
//     size.
//   - An accessor on a buffer view with a byteStride has a stride at least
//     its element size, as glTF requires.
//   - A sparse accessor's indices are strictly increasing and below its
//     count, as glTF requires (checked when its index data is in memory), so
//     it never lists more values than it has elements.
// An accessor or sparse index type whose component type has no size is left
// to cgltf_validate(), which refuses it.
static const char* checkDataRanges(const cgltf_data* data) {
    const char* outOfRange =
        "model data refers past the end of a buffer, or has a count too large to address";
    for (cgltf_size i = 0; i < data->buffer_views_count; i++) {
        const cgltf_buffer_view& view = data->buffer_views[i];
        if (!view.buffer || !rangeFits(view.buffer->size, view.offset, 0, view.size, 1)) {
            return outOfRange;
        }
        if (view.has_meshopt_compression) {
            const auto& compressed = view.meshopt_compression;
            if (!compressed.buffer || !rangeFits(compressed.buffer->size, compressed.offset,
                                                 0, compressed.size, 1) ||
                (compressed.count && compressed.stride > SIZE_MAX / compressed.count))
                return outOfRange;
        }
    }
    for (cgltf_size i = 0; i < data->images_count; ++i) {
        // Bound (abi): the embedded image decoder takes an int byte count. Overflow rejects the model and is reported by LoadResult.
        if (data->images[i].buffer_view && data->images[i].buffer_view->size > static_cast<size_t>(INT_MAX))
            return outOfRange;
    }
    for (cgltf_size i = 0; i < data->accessors_count; i++) {
        const cgltf_accessor& acc = data->accessors[i];
        cgltf_size numComp = cgltf_num_components(acc.type);
        // Bound (abi): file accessor counts must fit the unpacked float-array index width. Overflow rejects the model and is reported by LoadResult.
        if (!numComp || acc.count > SIZE_MAX / (sizeof(float) * numComp)) return outOfRange;
        cgltf_size elemSize = cgltf_calc_size(acc.type, acc.component_type);
        if (elemSize == 0) continue;
        cgltf_size componentSize = cgltf_component_size(acc.component_type);
        if (acc.buffer_view) {
            // acc.stride is the view's byteStride, or elemSize without one
            if (acc.stride < elemSize) {
                return "an accessor's buffer view has a byteStride smaller than its element";
            }
            // glTF 2.0 section 3.6.2.4 requires component-aligned offsets
            // within both the view and buffer, and component-aligned strides.
            if (acc.stride % componentSize || acc.offset % componentSize ||
                acc.buffer_view->offset % componentSize)
                return "an accessor's offset or byteStride violates component alignment";
            if (!rangeFits(acc.buffer_view->size, acc.offset, acc.stride, elemSize, acc.count)) {
                return outOfRange;
            }
        }
        if (acc.is_sparse) {
            const cgltf_accessor_sparse& sp = acc.sparse;
            if (sp.count > acc.count) return outOfRange;
            cgltf_size indexSize = cgltf_component_size(sp.indices_component_type);
            if (indexSize == 0) continue;
            if (!sp.indices_buffer_view || !sp.values_buffer_view) return outOfRange;
            if (sp.indices_byte_offset % indexSize || sp.indices_buffer_view->offset % indexSize ||
                sp.values_byte_offset % componentSize || sp.values_buffer_view->offset % componentSize)
                return "a sparse accessor's offset violates component alignment";
            // Sparse replacement values require actual storage; an absent
            // payload must not turn into successful zero-filled replacements.
            if (sp.count && (!cgltf_buffer_view_data(sp.indices_buffer_view) ||
                             !cgltf_buffer_view_data(sp.values_buffer_view)))
                return outOfRange;
            if (!rangeFits(sp.indices_buffer_view->size, sp.indices_byte_offset,
                           indexSize, indexSize, sp.count)) {
                return outOfRange;
            }
            // Sparse values are tightly packed (see readAccessorFloats)
            if (!rangeFits(sp.values_buffer_view->size, sp.values_byte_offset,
                           elemSize, elemSize, sp.count)) {
                return outOfRange;
            }
            bool indexType = sp.indices_component_type == cgltf_component_type_r_8u ||
                             sp.indices_component_type == cgltf_component_type_r_16u ||
                             sp.indices_component_type == cgltf_component_type_r_32u;
            const uint8_t* indexData = cgltf_buffer_view_data(sp.indices_buffer_view);
            if (indexType && indexData) {
                indexData += sp.indices_byte_offset;
                cgltf_size prev = 0;
                for (cgltf_size k = 0; k < sp.count; k++) {
                    cgltf_size idx = readSparseIndex(indexData, sp.indices_component_type, k);
                    if (idx >= acc.count || (k > 0 && idx <= prev)) {
                        return "sparse accessor indices are not strictly increasing below its count";
                    }
                    prev = idx;
                }
            }
        }
    }
    return nullptr;
}

static bool bufferUrisFit(const cgltf_data* data) {
    for (size_t i = 0; i < data->buffers_count; ++i) {
        const auto& buffer = data->buffers[i];
        const char* uri = buffer.uri;
        if (!uri || strncmp(uri, "data:", 5) != 0) continue;
        const char* comma = strchr(uri, ',');
        if (!comma || comma - uri < 7 || strncmp(comma - 7, ";base64", 7) != 0) return false;
        const char* encoded = comma + 1;
        size_t length = strlen(encoded);
        size_t remainder = length % 4;
        if (remainder == 1) return false;
        size_t padding = 0;
        if (length && encoded[length - 1] == '=') ++padding;
        if (length > 1 && encoded[length - 2] == '=') ++padding;
        if (padding && remainder) return false;
        // Divide first so the decoded-size calculation cannot overflow.
        size_t decoded = length / 4 * 3 + (remainder ? remainder - 1 : 0) - padding;
        // Bound (security): inline buffer lengths must fit the URI's decoded bytes before allocation. Overflow rejects the model and is reported by LoadResult.
        if (buffer.size > decoded) return false;
        for (size_t k = 0; k < length; ++k) {
            char c = encoded[k];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '+' || c == '/') continue;
            if (c != '=' || k < length - padding) return false;
        }
    }
    return true;
}

// Read accessor data as float array (handles all component types)
static vector<float> readAccessorFloats(const cgltf_accessor* acc) {
    cgltf_size numComp = cgltf_num_components(acc->type);
    // Bound (abi): unpacked accessor storage must fit vector's element width. Overflow rejects the model and is reported by LoadResult.
    if (acc->count > vector<float>().max_size() / numComp)
        throw length_error("unpacked accessor exceeds vector element width");
    vector<float> out(acc->count * numComp);
    if (!acc->is_sparse) {
        cgltf_accessor_unpack_floats(acc, out.data(), out.size());
        return out;
    }

    // glTF packs sparse values tightly, whatever the base view's byteStride.
    // Read the base without them, then apply the values here at their packed
    // stride.
    cgltf_accessor base = *acc;
    base.is_sparse = false;
    cgltf_accessor_unpack_floats(&base, out.data(), out.size());

    const cgltf_accessor_sparse& sp = acc->sparse;
    cgltf_accessor values = base;
    values.buffer_view = sp.values_buffer_view;
    values.offset = sp.values_byte_offset;
    values.stride = cgltf_calc_size(acc->type, acc->component_type);
    values.count = sp.count;
    vector<float> vals(sp.count * numComp);
    cgltf_accessor_unpack_floats(&values, vals.data(), vals.size());

    cgltf_accessor indices = {};
    indices.type = cgltf_type_scalar;
    indices.component_type = sp.indices_component_type;
    indices.buffer_view = sp.indices_buffer_view;
    indices.offset = sp.indices_byte_offset;
    indices.stride = cgltf_component_size(sp.indices_component_type);
    indices.count = sp.count;
    // Bound by the array actually allocated, not by acc->count
    cgltf_size outElems = out.size() / numComp;
    for (cgltf_size i = 0; i < sp.count; i++) {
        cgltf_size idx = cgltf_accessor_read_index(&indices, i);
        if (idx >= outElems) continue;  // checkDataRanges() refuses these
        copy(vals.begin() + i * numComp, vals.begin() + (i + 1) * numComp,
             out.begin() + idx * numComp);
    }
    return out;
}

// Read accessor data as uint32 indices. checkDataRanges() has refused counts
// too large to address; the loop runs over the array actually allocated.
static vector<unsigned int> readAccessorIndices(const cgltf_accessor* acc) {
    vector<unsigned int> out(acc->count);
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (unsigned int)cgltf_accessor_read_index(acc, i);
    }
    return out;
}

// World transform of a node from its parent's world transform and its own
// local one (both column-major, as cgltf writes them). load() takes this
// step once per node, carrying the parent's result down the hierarchy.
static void composeTransform(const float* parentWorld, const float* local, float* out) {
    for (int i = 0; i < 4; ++i) {
        float l0 = local[i * 4 + 0];
        float l1 = local[i * 4 + 1];
        float l2 = local[i * 4 + 2];
        out[i * 4 + 0] = l0 * parentWorld[0] + l1 * parentWorld[4] + l2 * parentWorld[8];
        out[i * 4 + 1] = l0 * parentWorld[1] + l1 * parentWorld[5] + l2 * parentWorld[9];
        out[i * 4 + 2] = l0 * parentWorld[2] + l1 * parentWorld[6] + l2 * parentWorld[10];
        out[i * 4 + 3] = local[i * 4 + 3];
    }
    out[12] += parentWorld[12];
    out[13] += parentWorld[13];
    out[14] += parentWorld[14];
}

// cgltf outputs column-major (OpenGL convention); TrussC Mat4 is row-major
static Mat4 toMat4(const float* m) {
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

// See internal::setTextureLoadHookForTests()
static atomic<void (*)()> g_textureLoadHook{nullptr};

namespace internal {
void setTextureLoadHookForTests(void (*hook)()) {
    g_textureLoadHook.store(hook, memory_order_relaxed);
}
} // namespace internal

static Texture* loadGltfTexture(const cgltf_texture* tex,
                                 vector<unique_ptr<Texture>>& store,
                                 const fs::path& baseDir) {
    if (!tex || !tex->image) return nullptr;
    if (auto hook = g_textureLoadHook.load(memory_order_relaxed)) hook();

    const cgltf_image* img = tex->image;

    Pixels pixels;

    if (img->buffer_view) {
        // Embedded image data (GLB or base64). A buffer without a uri (or a
        // GLB without a BIN chunk) has no data; cgltf_buffer_view_data()
        // returns NULL for it.
        const uint8_t* ptr = cgltf_buffer_view_data(img->buffer_view);
        size_t len = img->buffer_view->size;
        // checkDataRanges() has already checked the decoder's int byte count.
        if (!ptr) return nullptr;
        pixels.loadFromMemory(ptr, (int)len);
    } else if (img->uri) {
        // External file reference
        string uri(img->uri);
        // Skip data URIs (base64 inline) for now
        if (uri.rfind("data:", 0) == 0) return nullptr;

        uri.resize(cgltf_decode_uri(uri.data()));

        // glTF uris are UTF-8: utf8ToPath() converts them without going
        // through the Windows code page, and on Windows it throws for bytes
        // that are not valid UTF-8. Only that image is skipped then. The uri
        // is appended as before (baseDir + "/" + uri), not with operator/,
        // which would let an absolute uri replace baseDir.
        fs::path imgPath = baseDir;
        try {
            imgPath += "/";
            imgPath += utf8ToPath(uri);
        } catch (const system_error& e) {
            getLogger().log(LogLevel::Warning,
                            string("[GltfModel] skipped an image whose uri is not a valid path (") + e.what() + ")");
            return nullptr;
        }
        LoadResult loaded = pixels.load(imgPath);
        if (!loaded) {
            getLogger().log(LogLevel::Warning,
                            string("[GltfModel] skipped an image that could not be loaded (") +
                            loadErrorName(loaded.error) + "): " + loaded.message);
            return nullptr;
        }
    }

    if (pixels.getWidth() == 0) return nullptr;

    auto t = make_unique<Texture>();
    // Texture supports independent axes; cgltf supplies REPEAT for omitted
    // sampler fields. A texture without a sampler has the same glTF default.
    const auto* sampler = tex->sampler;
    auto wrapMode = [](cgltf_wrap_mode wrap) {
        switch (wrap) {
            case cgltf_wrap_mode_clamp_to_edge: return TextureWrap::ClampToEdge;
            case cgltf_wrap_mode_mirrored_repeat: return TextureWrap::MirroredRepeat;
            default: return TextureWrap::Repeat;
        }
    };
    const auto minFilter = sampler ? sampler->min_filter : cgltf_filter_type_undefined;
    const bool mipmaps = minFilter != cgltf_filter_type_nearest &&
                         minFilter != cgltf_filter_type_linear;
    // Configure before allocation so only the final sampler is created.
    t->setWrapU(wrapMode(sampler ? sampler->wrap_s : cgltf_wrap_mode_repeat));
    t->setWrapV(wrapMode(sampler ? sampler->wrap_t : cgltf_wrap_mode_repeat));
    if (sampler) {
        if (sampler->mag_filter == cgltf_filter_type_nearest) {
            t->setMagFilter(TextureFilter::Nearest);
        } else if (sampler->mag_filter == cgltf_filter_type_linear) {
            t->setMagFilter(TextureFilter::Linear);
        }
        // Texture uses linear mipmap interpolation. The *_MIPMAP_NEAREST
        // modes have no matching API and retain the existing default.
        if (minFilter == cgltf_filter_type_nearest ||
            minFilter == cgltf_filter_type_nearest_mipmap_linear) {
            t->setMinFilter(TextureFilter::Nearest);
        } else if (minFilter == cgltf_filter_type_linear ||
                   minFilter == cgltf_filter_type_linear_mipmap_linear) {
            t->setMinFilter(TextureFilter::Linear);
        }
    }
    t->allocate(pixels, TextureUsage::Immutable, mipmaps);
    Texture* raw = t.get();
    store.push_back(std::move(t));
    return raw;
}

// ---------------------------------------------------------------------------
// Material loading
// ---------------------------------------------------------------------------

static Material loadGltfMaterial(const cgltf_material* mat,
                                 vector<unique_ptr<Texture>>& texStore,
                                 const fs::path& baseDir) {
    Material m;
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
                                           texStore, baseDir);
            if (tex) m.setBaseColorTexture(tex);
        }
        if (pbr.metallic_roughness_texture.texture) {
            Texture* tex = loadGltfTexture(pbr.metallic_roughness_texture.texture,
                                           texStore, baseDir);
            if (tex) m.setMetallicRoughnessTexture(tex);
        }
    }

    if (mat->normal_texture.texture) {
        Texture* tex = loadGltfTexture(mat->normal_texture.texture,
                                       texStore, baseDir);
        if (tex) m.setNormalMap(tex);
    }

    if (mat->emissive_texture.texture) {
        Texture* tex = loadGltfTexture(mat->emissive_texture.texture,
                                       texStore, baseDir);
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
                                       texStore, baseDir);
        if (tex) m.setOcclusionTexture(tex);
    }

    return m;
}

// ---------------------------------------------------------------------------
// Mesh loading
// ---------------------------------------------------------------------------

// True when the accessor reads from a buffer view whose data is in memory.
// glTF lets an accessor leave out its buffer view (its values are then
// zeros, plus any sparse values), and a buffer without a uri (outside a
// GLB's BIN chunk) has no data. Only accessors with data are read, so each
// array's size is bounded by the data loaded for it.
static bool accessorHasData(const cgltf_accessor* acc) {
    return acc && acc->buffer_view && cgltf_buffer_view_data(acc->buffer_view);
}

// True when the primitive has a POSITION accessor with data, and its index
// accessor (if any) has data too. glTF lets a primitive leave out POSITION.
// Every other attribute has the POSITION count (cgltf_validate() checks
// this), so with these two each array the loader allocates for the
// primitive has a size bounded by the data loaded for it. A primitive
// without them is skipped by load() with a warning, and the rest of the
// file loads.
static bool hasVertexData(const cgltf_primitive* prim) {
    if (prim->indices && !accessorHasData(prim->indices)) return false;
    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        const cgltf_attribute& attr = prim->attributes[a];
        if (attr.type == cgltf_attribute_type_position) {
            return accessorHasData(attr.data);
        }
    }
    return false;
}

// Returns false when an index points past the primitive's vertices.
static bool loadGltfPrimitive(const cgltf_primitive* prim, Mesh& mesh) {
    // Bound (abi): Mesh exposes signed int geometry counts. Overflow rejects the model and is reported by LoadResult.
    if (prim->indices && prim->indices->count > static_cast<size_t>(INT_MAX))
        throw length_error("mesh index count exceeds the Mesh index width");
    for (size_t a = 0; a < prim->attributes_count; ++a) {
        if (prim->attributes[a].data->count > static_cast<size_t>(INT_MAX))
            throw length_error("mesh vertex count exceeds the Mesh index width");
    }
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
            if (idx >= vertCount) return false;
            mesh.addIndex(idx);
        }
    }

    return true;
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

LoadResult GltfModel::load(const fs::path& path) {
    nodes_.clear();
    textures_.clear();
    loaded_ = false;

    // Parser results and one exception boundary handle load failures.
    string resolved;
    auto decodeFailure = [&](const string& reason) {
        string message = reason + ": " + resolved;
        getLogger().log(LogLevel::Error, "[GltfModel] " + message);
        return LoadResult::fail(LoadError::DecodeFailed, message);
    };
    string failure;
    size_t skippedPrimitives = 0;
    try {
        fs::path resolvedPath = tc::internal::resolveLoadPath(path);
        resolved = tc::internal::pathToDisplayUtf8(resolvedPath);
        auto entry = tc::internal::checkLoadFile(resolvedPath, "GltfModel");
        if (!entry) return entry;

        GltfFileContext files{resolvedPath};
        cgltf_options options = {};
        options.file.read = readGltfFile;
        options.file.release = releaseGltfFile;
        options.file.user_data = &files;
        cgltf_data* parsed = nullptr;
        invalidModelInteger = false;
        cgltf_result result = cgltf_parse_file(&options, "", &parsed);
        // Snapshot the per-parse thread-local result before any diagnostic
        // can synchronously invoke a listener that loads another model.
        const bool parseInvalidInteger = invalidModelInteger;
        unique_ptr<cgltf_data, decltype(&cgltf_free)> dataOwner(parsed, cgltf_free);
        cgltf_data* data = dataOwner.get();
        if (result != cgltf_result_success) {
            return decodeFailure(result == cgltf_result_out_of_memory ?
                "not enough memory for the model data" : "failed to parse");
        }
        if (parseInvalidInteger) return decodeFailure("invalid model integer");

        if (!bufferUrisFit(data)) return decodeFailure("invalid inline buffer length");

        // Load external buffers (for .gltf with separate .bin)
        files.readingModel = false;
        result = cgltf_load_buffers(&options, data, "");
        if (result != cgltf_result_success) {
            return decodeFailure(result == cgltf_result_out_of_memory ?
                "not enough memory for the model data" : "failed to load buffers");
        }

        // Validate the model data before reading any of it
        if (const char* reason = checkDataRanges(data)) {
            return decodeFailure(reason);
        }
        result = cgltf_validate(data);
        if (result != cgltf_result_success) {
            return decodeFailure("model data failed validation (cgltf result " +
                                 to_string(static_cast<int>(result)) + ")");
        }
        // Base directory for relative texture paths
        fs::path baseDir = resolvedPath.parent_path();

        // Root nodes: those of the default scene (or scene 0). A file without
        // scenes is loaded from every node that has no parent.
        vector<const cgltf_node*> roots;
        if (data->scenes_count > 0) {
            const cgltf_scene* scene = data->scene ? data->scene : &data->scenes[0];
            roots.assign(scene->nodes, scene->nodes + scene->nodes_count);
        } else if (data->nodes_count == 0) {
            failure = "no scene and no nodes to load";
        } else {
            for (cgltf_size n = 0; n < data->nodes_count; n++) {
                if (!data->nodes[n].parent) roots.push_back(&data->nodes[n]);
            }
        }

        // Depth-first walk with an explicit stack, without recursion, so any
        // depth of hierarchy loads. Each entry carries its parent's world
        // transform, so every world transform is computed once, from its
        // parent's. Nodes are visited in the same order as a recursive
        // pre-order walk.
        struct Pending {
            const cgltf_node* node;
            float parentWorld[16];
            bool hasParent;
        };
        vector<Pending> stack;
        for (size_t r = roots.size(); r-- > 0;) {
            stack.push_back({roots[r], {}, false});
        }
        // glTF requires the node hierarchy to be a set of disjoint trees and
        // a scene's nodes to be unique: every node is reached at most once,
        // and a node reached again fails the load.
        vector<bool> visited(data->nodes_count, false);

        while (!stack.empty() && failure.empty()) {
            Pending cur = stack.back();
            stack.pop_back();
            const cgltf_node* node = cur.node;
            size_t nodeIndex = (size_t)(node - data->nodes);
            if (visited[nodeIndex]) {
                failure = "the node hierarchy reaches a node more than once";
                break;
            }
            visited[nodeIndex] = true;

            float local[16];
            float world[16];
            cgltf_node_transform_local(node, local);
            if (cur.hasParent) {
                composeTransform(cur.parentWorld, local, world);
            } else {
                memcpy(world, local, sizeof(world));
            }

            if (node->mesh) {
                Mat4 worldXform = toMat4(world);

                for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
                    const cgltf_primitive* prim = &node->mesh->primitives[p];
                    if (prim->type != cgltf_primitive_type_triangles) continue;
                    if (!hasVertexData(prim)) {
                        skippedPrimitives++;
                        continue;
                    }

                    Node entry;
                    if (!loadGltfPrimitive(prim, entry.mesh)) {
                        failure = "a mesh index points past its vertices";
                        break;
                    }
                    entry.material = loadGltfMaterial(prim->material, textures_,
                                                      baseDir);
                    entry.transform = worldXform;
                    entry.name = node->name ? node->name : "";

                    // Bake world transform into vertex positions and normals
                    // so draw() doesn't need multMatrix
                    bakeTransform(entry.mesh, worldXform);

                    nodes_.push_back(std::move(entry));
                }
                if (!failure.empty()) break;
            }

            for (cgltf_size c = node->children_count; c-- > 0;) {
                Pending child;
                child.node = node->children[c];
                memcpy(child.parentWorld, world, sizeof(world));
                child.hasParent = true;
                stack.push_back(child);
            }
        }

        if (!failure.empty()) {
            nodes_.clear();
            textures_.clear();
            return decodeFailure(failure);
        }
        if (skippedPrimitives > 0) {
            getLogger().log(LogLevel::Warning, "[GltfModel] skipped " + to_string(skippedPrimitives) +
                            " primitive(s) without POSITION or index data: " + resolved);
        }
        getLogger().log(LogLevel::Notice, "[GltfModel] loaded " + to_string(nodes_.size()) + " nodes, " +
                        to_string(textures_.size()) + " textures from " + tc::internal::pathToDisplayUtf8(path));
        loaded_ = true;
        return LoadResult::success();
    } catch (...) {
        nodes_.clear();
        textures_.clear();
        // Diagnostics (including listeners and LoadResult's message) may also
        // allocate or throw. Keep them inside the load boundary too.
        try {
            try {
                throw;
            } catch (const exception& e) {
                return decodeFailure(string("error while building the model: ") + e.what());
            }
        } catch (...) {
            // A fixed fallback needs no C++ allocation or logger callbacks.
            std::fputs("[GltfModel] load failed\n", stderr);
        }
        return {LoadError::DecodeFailed, {}};
    }
}

void GltfModel::draw() const {
    for (const auto& node : nodes_) {
        Material mat = node.material;
        setMaterial(mat);
        node.mesh.draw();
        clearMaterial();
    }
}

} // namespace tcx::gltf
