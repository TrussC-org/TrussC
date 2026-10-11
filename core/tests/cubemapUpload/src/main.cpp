// #687: daily Linux / Xvfb regression for six-face CPU uploads.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <array>
#include <cstdio>
#include <cstring>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <GL/gl.h>
#endif

using namespace tc;

namespace {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
int failures = 0;
bool completed = false;

void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

constexpr int sideSize = 4;
using Face = std::array<unsigned char, sideSize * sideSize * 4>;
using Faces = std::array<Face, 6>;

void loadFaces(Texture& texture, const Faces& faces, size_t size = sizeof(Face)) {
    const void* data[6];
    for (int face = 0; face < 6; ++face) data[face] = faces[face].data();
    texture.loadCubemapData(data, size);
}

void expectFaces(Texture& texture, const Faces& expected) {
    // Dynamic images cannot have sokol attachment views. For this Linux-only
    // readback, inject the current GL backing texture into a non-owning
    // RenderTarget alias, then read each face through its attachment view.
    // The alias never uploads or copies pixels, so we inspect the actual data.
    const auto info = sg_gl_query_image_info(texture.getImage());
    sg_image_desc desc = {};
    desc.type = SG_IMAGETYPE_CUBE;
    desc.width = sideSize;
    desc.height = sideSize;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.color_attachment = true;
    desc.gl_textures[0] = info.tex[info.active_slot];
    const sg_image alias = sg_make_image(&desc);
    check("readback alias is valid", sg_query_image_state(alias) == SG_RESOURCESTATE_VALID);
    for (int face = 0; face < 6; ++face) {
        sg_view_desc viewDesc = {};
        viewDesc.color_attachment.image = alias;
        viewDesc.color_attachment.slice = face;
        const sg_view view = sg_make_view(&viewDesc);
        check("face attachment view is valid", sg_query_view_state(view) == SG_RESOURCESTATE_VALID);
        sg_pass pass = {};
        pass.attachments.colors[0] = view;
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        sg_begin_pass(&pass);
        Face actual = {};
        glReadPixels(0, 0, sideSize, sideSize, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
        check("face readback has no GL error", glGetError() == GL_NO_ERROR);
        char label[80];
        std::snprintf(label, sizeof(label), "face %d matches every pattern byte", face);
        check(label, actual == expected[face]);
        sg_end_pass();
        sg_destroy_view(view);
    }
    sg_destroy_image(alias);
}

class CubemapUploadApp : public App {
public:
    void setup() override {
        setFps(VSYNC);
        for (int face = 0; face < 6; ++face) {
            for (size_t i = 0; i < a_[face].size(); ++i) {
                a_[face][i] = static_cast<unsigned char>(face * 31 + i);
                b_[face][i] = static_cast<unsigned char>(255 - a_[face][i]);
            }
        }
    }

    void draw() override {
        if (completed) return;
        int duplicateWarnings = 0;
        int usageWarnings = 0;
        int sizeWarnings = 0;
        int mipErrors = 0;
        int immutableErrors = 0;
        int unexpectedErrors = 0;
        EventListener logs = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning) {
                if (e.message == "[Texture] loadCubemapData() called twice in same frame, skipped") ++duplicateWarnings;
                if (e.message == "[Texture] loadCubemapData: requires Dynamic / Stream usage") ++usageWarnings;
                if (e.message.find("[Texture] loadCubemapData: data size mismatch") == 0) ++sizeWarnings;
            }
            if (e.level == LogLevel::Error || e.level == LogLevel::Fatal) {
                if (e.message == "[Texture] allocateCubemap: Dynamic / Stream cubemaps require exactly one mip level") ++mipErrors;
                else if (e.message.find("[Texture] allocateCubemap: Immutable cubemap needs initial data") == 0) ++immutableErrors;
                else ++unexpectedErrors;
            }
        });
        if (secondFrame_) {
            check("device frame advanced", sapp_frame_count() != firstFrame_);
            for (auto& texture : textures_) {
                loadFaces(texture, b_);
                expectFaces(texture, b_);
            }
            check("next frame uploads are accepted without duplicate warnings", duplicateWarnings == 0);
            check("next frame has no validation errors", unexpectedErrors == 0);
            completed = true;
            exitApp();
            return;
        }
        firstFrame_ = sapp_frame_count();
        const TextureUsage usages[] = {TextureUsage::Dynamic, TextureUsage::Stream};
        for (int index = 0; index < 2; ++index) {
            auto& texture = textures_[index];
            texture.allocateCubemap(sideSize, TextureFormat::RGBA8, usages[index], 2);
            check("Dynamic / Stream mip chain is refused", !texture.isAllocated() && texture.getImage().id == 0);
            texture.allocateCubemap(sideSize, TextureFormat::RGBA8, usages[index]);
            check("Dynamic / Stream has one valid mip", texture.getNumMipLevels() == 1 &&
                  sg_query_image_state(texture.getImage()) == SG_RESOURCESTATE_VALID);
            loadFaces(texture, a_, sizeof(Face) - 1);
            loadFaces(texture, a_);
            check("size mismatch does not consume the upload guard", duplicateWarnings == index);
            expectFaces(texture, a_);
            loadFaces(texture, b_);
            check("same frame duplicate warns", duplicateWarnings == index + 1);
            expectFaces(texture, a_);
        }
        check("both invalid mip allocations logged an error", mipErrors == 2);
        check("both size mismatches warned", sizeWarnings == 2);

        // Guard keys include image identity, not only the device frame.
        const uint32_t oldImage = textures_[0].getImage().id;
        textures_[0].allocateCubemap(sideSize, TextureFormat::RGBA8, TextureUsage::Dynamic);
        check("reallocation creates a different image", textures_[0].getImage().id != oldImage);
        loadFaces(textures_[0], a_);
        check("new image accepts an upload in the same frame", duplicateWarnings == 2);
        expectFaces(textures_[0], a_);

        Texture immutable;
        immutable.allocateCubemap(sideSize, TextureFormat::RGBA8, TextureUsage::Immutable);
        check("Immutable cubemap allocation is refused with an error",
              immutableErrors == 1 && !immutable.isAllocated());
        loadFaces(immutable, a_);
        Texture target;
        target.allocateCubemap(sideSize, TextureFormat::RGBA8, TextureUsage::RenderTarget, 3);
        loadFaces(target, a_);
        check("RenderTarget upload warns", usageWarnings == 1);
        check("RenderTarget retains its mip chain", target.getNumMipLevels() == 3 &&
              sg_query_image_state(target.getImage()) == SG_RESOURCESTATE_VALID);
        for (int face = 0; face < 6; ++face) {
            for (int mip = 0; mip < 3; ++mip) {
                check("RenderTarget face/mip attachment remains valid",
                      sg_query_view_state(target.getCubemapFaceAttachmentView(face, mip)) == SG_RESOURCESTATE_VALID);
            }
        }
        check("no unexpected errors or sokol validation errors", unexpectedErrors == 0);
        check("all first-frame checks used the same device frame", sapp_frame_count() == firstFrame_);
        secondFrame_ = true;
    }

private:
    Texture textures_[2];
    Faces a_, b_;
    uint64_t firstFrame_ = 0;
    bool secondFrame_ = false;
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(32, 32);
        settings.setHighDpi(false);
        runApp<CubemapUploadApp>(settings);
        check("both frames completed", completed);
        return failures ? 1 : 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    std::printf("SKIP: cubemapUpload needs --gpu-check on Linux with a display\n");
    return 0;
}
