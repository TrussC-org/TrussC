#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"

#include <cstdio>
#include <cstring>
#include <utility>

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void topologyChecks() {
    using internal::meshListIndices;
    using P = PrimitiveMode;
    const std::vector<unsigned int> sequential{0, 1, 2, 3};
    const std::vector<unsigned int> reordered{3, 1, 0, 2};
    check("triangle list gets sequential indices", meshListIndices(P::Triangles, 4, {}) == sequential);
    check("line list gets sequential indices", meshListIndices(P::Lines, 4, {}) == sequential);
    check("indexed lists preserve order", meshListIndices(P::Triangles, 4, reordered) == reordered &&
          meshListIndices(P::Lines, 4, reordered) == reordered);
    check("strip flips odd winding", meshListIndices(P::TriangleStrip, 4, {}) ==
          std::vector<unsigned int>({0, 1, 2, 2, 1, 3}));
    check("indexed strip uses vertex indices", meshListIndices(P::TriangleStrip, 4, reordered) ==
          std::vector<unsigned int>({3, 1, 0, 0, 1, 2}));
    check("fan expands around first vertex", meshListIndices(P::TriangleFan, 4, {}) ==
          std::vector<unsigned int>({0, 1, 2, 0, 2, 3}));
    check("indexed fan", meshListIndices(P::TriangleFan, 4, reordered) ==
          std::vector<unsigned int>({3, 1, 0, 3, 0, 2}));
    check("line strip", meshListIndices(P::LineStrip, 4, {}) ==
          std::vector<unsigned int>({0, 1, 1, 2, 2, 3}));
    check("indexed line strip", meshListIndices(P::LineStrip, 4, reordered) ==
          std::vector<unsigned int>({3, 1, 1, 0, 0, 2}));
    check("line loop closes", meshListIndices(P::LineLoop, 4, {}) ==
          std::vector<unsigned int>({0, 1, 1, 2, 2, 3, 3, 0}));
    check("indexed line loop closes", meshListIndices(P::LineLoop, 4, reordered) ==
          std::vector<unsigned int>({3, 1, 1, 0, 0, 2, 2, 3}));
    for (auto p : {P::TriangleStrip, P::TriangleFan, P::LineStrip, P::LineLoop}) {
        check("empty topology", meshListIndices(p, 0, {}).empty());
        check("one vertex makes no primitive", meshListIndices(p, 1, {}).empty());
    }
    check("two vertices make no triangle", meshListIndices(P::TriangleStrip, 2, {}).empty() &&
          meshListIndices(P::TriangleFan, 2, {}).empty());
    internal::WindowRequestAccess::HostState secondaryState; internal::WindowContext& secondary = secondaryState.context;
    secondary.swapchainTarget.colorFormat = SG_PIXELFORMAT_BGRA8;
    secondary.swapchainTarget.sampleCount = 1;
    auto target = internal::swapchainTargetFormat(secondary);
    check("secondary target preserves BGRA8 and 1x", target.colorFormat == SG_PIXELFORMAT_BGRA8 && target.sampleCount == 1);
}

#if defined(SOKOL_GLCORE) || (defined(__linux__) && defined(SOKOL_GLES3) && !defined(__ANDROID__))
bool completed = false;
#if defined(SOKOL_GLES3)
#define TC_PBR_TEST_GLSL_VERSION "#version 300 es\nprecision highp float;\n"
#else
#define TC_PBR_TEST_GLSL_VERSION "#version 330\n"
#endif
const sg_shader_desc* testShader(sg_backend) {
    static const sg_shader_desc desc = [] {
        sg_shader_desc d = {};
        d.vertex_func.source = TC_PBR_TEST_GLSL_VERSION "layout(location=0) in vec3 position;"
            "layout(location=1) in vec2 uv; layout(location=2) in vec4 color;"
            "out vec4 c; void main(){gl_Position=vec4(position,1); c=color+vec4(uv,0,0)*0.01;}";
        d.fragment_func.source = TC_PBR_TEST_GLSL_VERSION "in vec4 c; out vec4 frag; void main(){frag=c;}";
        d.attrs[0].glsl_name = "position";
        d.attrs[1].glsl_name = "uv";
        d.attrs[2].glsl_name = "color";
        return d;
    }();
    return &desc;
}
#undef TC_PBR_TEST_GLSL_VERSION
struct InspectShader : Shader {
    sg_pipeline targetPipeline() { return pipelineForCurrentTarget(); }
};

Mesh makeMesh(PrimitiveMode mode) {
    Mesh m;
    m.setMode(mode);
    m.addVertex(16, 16, 0).addVertex(48, 16, 0).addVertex(16, 48, 0).addVertex(48, 48, 0);
    for (int i = 0; i < 4; ++i) m.addNormal(0, 0, 1);
    return m;
}

class SecondaryApp : public App {
public:
    explicit SecondaryApp(InspectShader* source) : shader(source) {}
    InspectShader* shader = nullptr;
    void draw() override {
        if (completed) return;
        auto& ctx = internal::currentWindowContext();
        check("real secondary window context", ctx.hostWindow && ctx.swapchainTarget.sampleCount > 0);
        Material mat;
        mat.setEmissive(1, 0, 0);
        setMaterial(mat);
        mesh = makeMesh(PrimitiveMode::TriangleStrip);
        mesh.draw();
        auto pbr = sg_query_pipeline_desc(ctx.currentTarget->deferredPbrDraws.back().cmd.pip);
        check("secondary PBR pipeline target", pbr.sample_count == ctx.swapchainTarget.sampleCount && pbr.colors[0].pixel_format == ctx.swapchainTarget.colorFormat);
        points = makeMesh(PrimitiveMode::Points);
        for (auto style : {PointStyle::Square, PointStyle::Pixel}) {
            setPointStyle(style);
            points.draw();
            auto point = sg_query_pipeline_desc(ctx.currentTarget->deferredPointDraws.back().cmd.pip);
            check("secondary point pipeline target", point.sample_count == ctx.swapchainTarget.sampleCount && point.colors[0].pixel_format == ctx.swapchainTarget.colorFormat);
        }
        clearMaterial();
        ShaderVertex v[] = {{-1,-1,0,0,0,0,1,0,1}, {1,-1,0,0,0,0,1,0,1}, {0,1,0,0,0,0,1,0,1}};
        shader->submitVertices(v, 3, PrimitiveType::Triangles);
        auto custom = sg_query_pipeline_desc(ctx.currentTarget->deferredShaderDraws.back().pipeline);
        check("secondary custom Shader pipeline target", custom.sample_count == ctx.swapchainTarget.sampleCount && custom.colors[0].pixel_format == ctx.swapchainTarget.colorFormat);
        completed = true; // Main exits on its next tick, after this window presents.
    }
private:
    Mesh mesh, points;
};

class GpuApp : public App {
public:
    void setup() override {
        fbo.allocate(64, 64);
        check("custom shader loads", shader.load(testShader));
        light.setDirectional(0, 0, -1);
        light.enableShadow(64);
        addLight(light);
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.sampleCount = 1;
        secondary = createWindow(settings);
        if (secondary) secondary->setPosition(32, 32);
        check("secondary window created", bool(secondary));
        if (secondary) {
            secondary->setApp<SecondaryApp>(&shader);
        }
    }
    void draw() override {
        ++frames;
        if (ran) {
            check("Shader clear releases cached pipelines after frame", sg_query_pipeline_state(retiredPipeline) == SG_RESOURCESTATE_INVALID);
            if (completed || !secondary) {
                exitApp();
            } else if (frames >= 300) {
                check("secondary window did not present within 300 main frames", false);
                exitApp();
            }
            return;
        }
        ran = true;
        auto& ctx = internal::currentWindowContext();
        const auto mainTarget = internal::swapchainTargetFormat(ctx);
        check("main target records actual swapchain attachments", mainTarget.colorFormat == sg_query_desc().environment.defaults.color_format && mainTarget.sampleCount == sapp_sample_count());
        std::printf("Actual GLX MSAA: %d (secondary windows share the GLX config)\n", sapp_sample_count());
        const auto mainPipeline = shader.targetPipeline();
        const auto savedFormat = ctx.swapchainTarget.colorFormat;
        const auto savedSamples = ctx.swapchainTarget.sampleCount;
        // GLX shares one framebuffer configuration across native windows.
        // Capture (without replaying) distinct targets to cover format/MSAA
        // selection even when the local driver gives both windows 1x RGBA8.
        Material targetMaterial;
        setMaterial(targetMaterial);
        Mesh targetMesh = makeMesh(PrimitiveMode::Triangles);
        Mesh targetPoints = makeMesh(PrimitiveMode::Points);
        for (auto format : {SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_BGRA8}) {
            for (int samples : {1, 4}) {
                ctx.swapchainTarget.colorFormat = format;
                ctx.swapchainTarget.sampleCount = samples;
                auto pip = shader.targetPipeline();
                auto desc = sg_query_pipeline_desc(pip);
                check("custom Shader resolves and caches synthetic target", pip.id == shader.targetPipeline().id &&
                      (pip.id == mainPipeline.id) == (format == mainTarget.colorFormat && samples == mainTarget.sampleCount) && desc.colors[0].pixel_format == format && desc.sample_count == samples);
                targetMesh.draw();
                desc = sg_query_pipeline_desc(ctx.currentTarget->deferredPbrDraws.back().cmd.pip);
                check("PBR resolves synthetic target", desc.colors[0].pixel_format == format && desc.sample_count == samples);
                ctx.currentTarget->deferredPbrDraws.pop_back();
                for (auto style : {PointStyle::Square, PointStyle::Pixel}) {
                    setPointStyle(style);
                    targetPoints.draw();
                    desc = sg_query_pipeline_desc(ctx.currentTarget->deferredPointDraws.back().cmd.pip);
                    check("points resolve synthetic target", desc.colors[0].pixel_format == format && desc.sample_count == samples);
                    ctx.currentTarget->deferredPointDraws.pop_back();
                }
            }
        }
        clearMaterial();
        ctx.swapchainTarget.colorFormat = savedFormat;
        ctx.swapchainTarget.sampleCount = savedSamples;
        normalChecks();
        check("main Shader reuses the target-owned pipeline", shader.targetPipeline().id == mainPipeline.id);

        {
            InspectShader disposable;
            check("disposable shader loads", disposable.load(testShader));
            fbo.begin();
            auto cached = disposable.targetPipeline();
            auto desc = sg_query_pipeline_desc(cached);
            check("custom Shader FBO target", desc.colors[0].pixel_format == ctx.currentTarget->colorFormat &&
                  desc.sample_count == ctx.currentTarget->sampleCount);
            fbo.end();
            disposable.clear();
            retiredPipeline = cached; // GPU destruction is deferred until frame end.
        }

        Material mat;
        mat.setBaseColor(1, 0, 0).setEmissive(1, 0, 0);
        setMaterial(mat);
        for (auto mode : {PrimitiveMode::Triangles, PrimitiveMode::TriangleStrip, PrimitiveMode::TriangleFan,
                          PrimitiveMode::Lines, PrimitiveMode::LineStrip, PrimitiveMode::LineLoop}) {
            for (bool indexed : {false, true}) {
                Mesh m = makeMesh(mode);
                if (indexed) m.addIndices({3, 1, 0, 2});
                auto original = std::as_const(m).getIndices();
                Mesh reference = makeMesh(internal::isLineMesh(mode) ? PrimitiveMode::Lines : PrimitiveMode::Triangles);
                reference.addIndices(internal::meshListIndices(mode, 4, original));
                auto pixels = render(m);
                check("normalized primitive renders", ink(pixels));
                check("normalized primitive matches explicit list", pixels == render(reference));
                check("upload preserves public indices", std::as_const(m).getIndices() == original);
                check("normalized GPU index count", m.getGpuIndexCount() == reference.getGpuIndexCount());
            }
        }
        Mesh line = makeMesh(PrimitiveMode::Lines);
        mat.setEmissive(0, 0, 0);
        setMaterial(mat);
        auto lit = render(line);
        for (int i = 0; i < 4; ++i) line.setNormal(i, Vec3(0, 0, -1));
        check("line shading responds to normals", lit != render(line));

        int warnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning && e.message.find("Index out of range") != std::string::npos) ++warnings;
        });
        Mesh invalid = makeMesh(PrimitiveMode::Triangles);
        render(invalid); // An invalid edit must also discard previously valid GPU buffers.
        invalid.addIndices({0, 1, 2, 0, 1, 4});
        auto fallback = render(invalid);
        render(invalid);
        check("invalid index warns once and falls back", warnings == 1 && invalid.getGpuVertexBuffer().id == 0 && ink(fallback));
        invalid.clearIndices();
        render(invalid);
        check("corrected indices recover GPU upload", invalid.getGpuIndexBuffer().id != 0);
        for (auto mode : {PrimitiveMode::TriangleStrip, PrimitiveMode::TriangleFan,
                          PrimitiveMode::Lines, PrimitiveMode::LineStrip, PrimitiveMode::LineLoop}) {
            Mesh bad = makeMesh(mode);
            bad.addIndices({0, 1, 2, 3, 4});
            auto pixels = render(bad);
            check("invalid topology retains valid CPU primitives", ink(pixels) && bad.getGpuVertexBuffer().id == 0);
        }
        check("invalid meshes share a once-only warning", warnings == 1);
        Mesh unlit = makeMesh(PrimitiveMode::LineStrip);
        unlit.clearNormals();
        auto withMaterial = render(unlit);
        clearMaterial();
        check("mesh without normals stays unlit", withMaterial == render(unlit));

        beginShadowPass(light);
        for (auto mode : {PrimitiveMode::Lines, PrimitiveMode::LineStrip, PrimitiveMode::LineLoop}) {
            Mesh skipped = makeMesh(mode);
            shadowDraw(skipped);
            check("lines do not upload for shadow casting", skipped.getGpuVertexBuffer().id == 0);
        }
        Mesh caster = makeMesh(PrimitiveMode::TriangleStrip);
        shadowDraw(caster);
        check("non-indexed strip casts with expanded indices", caster.getGpuIndexCount() == 6);
        endShadowPass();
        setMaterial(mat);
        fbo.begin(0, 0, 0, 0);
        line.draw();
        check("lit lines retain shadow lookup", ctx.currentTarget->deferredPbrDraws.back().cmd.fsp.shadowMapParams[1] == 1);
        fbo.end();
        clearMaterial();
    }
private:
    // Compare CPU and actual PBR shader output with analytically baked data.
    void normalChecks() {
        Material material;
        material.setBaseColor(0.6f, 0.7f, 0.8f).setRoughness(0.8f);
        Pixels texel; texel.allocate(1, 1, 4);
        texel.setColor(0, 0, Color(0.8f, 0.6f, 0.9f));
        Texture normalMap; normalMap.allocate(texel);
        const Vec3 normal = Vec3(1, 1, 1).normalized();
        const Vec3 tangent = Vec3(1, -1, 0).normalized();
        const Mat4 rotation = Mat4::rotateY(0.3f) * Mat4::rotateZ(0.2f);
        for (const Vec3& scaling : {Vec3(2, 1, 1), Vec3(2, 1, 0.5f), Vec3(-1, 1, 1), Vec3(1.5f)}) {
            const Mat4 linear = rotation * Mat4::scale(scaling);
            const Mat4 model = Mat4::translate(32, 32, 0) * linear;
            const Vec3 expectedNormal = (rotation * (normal / scaling)).normalized();
            const Vec3 expectedTangent = (linear * tangent).normalized();
            Mesh mesh, baked;
            for (const Vec3& v : {Vec3(-10, -10, 0), Vec3(10, -10, 0), Vec3(0, 10, 0)}) {
                mesh.addVertex(v).addNormal(normal).addTexCoord(0, 0)
                    .addTangent(tangent.x, tangent.y, tangent.z, 1);
                baked.addVertex(model * v).addNormal(expectedNormal).addTexCoord(0, 0)
                    .addTangent(expectedTangent.x, expectedTangent.y, expectedTangent.z, 1);
            }
            for (int path = 0; path < 3; ++path) {
                material.setNormalMap(path == 2 ? &normalMap : nullptr);
                setMaterial(material);
                auto capture = [&](const Mesh& source, bool applyModel) {
                    fbo.begin(0, 0, 0, 1);
                    pushMatrix();
                    if (applyModel) multMatrix(model);
                    if (path == 0) source.drawWithLighting();
                    else source.draw();
                    if (path != 0 && applyModel) {
                        const auto& vsp = internal::currentWindowContext().currentTarget->deferredPbrDraws.back().cmd.vsp;
                        const Vec3 uploaded(vsp.normalMat[0]*normal.x + vsp.normalMat[4]*normal.y + vsp.normalMat[8]*normal.z,
                                            vsp.normalMat[1]*normal.x + vsp.normalMat[5]*normal.y + vsp.normalMat[9]*normal.z,
                                            vsp.normalMat[2]*normal.x + vsp.normalMat[6]*normal.y + vsp.normalMat[10]*normal.z);
                        check("PBR uploads inverse-transpose in GLSL column storage",
                              (uploaded.normalized() - expectedNormal).length() < 1e-5f);
                    }
                    popMatrix(); fbo.end();
                    std::vector<unsigned char> pixels(64*64*4);
                    check("normal fixture readback", fbo.readPixels(pixels.data()));
                    return pixels;
                };
                const auto actual = capture(mesh, true);
                const auto expected = capture(baked, false);
                bool same = true;
                int brightness = 0;
                for (int channel = 0; channel < 3; ++channel) {
                    const size_t index = (32*64 + 32)*4 + channel;
                    same &= std::abs(int(actual[index]) - int(expected[index])) <= 1;
                    brightness += actual[index];
                }
                check(path == 0 ? "CPU normal lighting matches analytic baked reference" :
                      path == 1 ? "PBR normal lighting matches analytic baked reference" :
                                  "PBR model-transformed tangent matches analytic baked reference",
                      same && brightness > 0);
            }
        }
        clearMaterial();
    }
    std::vector<unsigned char> render(const Mesh& m) {
        fbo.begin(0, 0, 0, 0);
        m.draw();
        auto& commands = internal::currentWindowContext().currentTarget->deferredPbrDraws;
        if (!commands.empty()) {
            auto desc = sg_query_pipeline_desc(commands.back().cmd.pip);
            check("PBR primitive kind matches mesh", desc.primitive_type ==
                (internal::isLineMesh(m.getMode()) ? SG_PRIMITIVETYPE_LINES : SG_PRIMITIVETYPE_TRIANGLES));
        }
        fbo.end();
        std::vector<unsigned char> pixels(64 * 64 * 4);
        check("FBO readback succeeds", fbo.readPixels(pixels.data()));
        return pixels;
    }
    static bool ink(const std::vector<unsigned char>& p) {
        for (size_t i = 3; i < p.size(); i += 4) if (p[i]) return true;
        return false;
    }
    Fbo fbo;
    InspectShader shader;
    Light light;
    std::shared_ptr<Window> secondary;
    bool ran = false;
    int frames = 0;
    sg_pipeline retiredPipeline{};
};
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    topologyChecks();
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        if (!internal::supportsSecondaryWindows()) {
            // Only the GPU part is skipped: the CPU topology checks above ran.
            std::printf("SKIP: pbrMeshTargets GPU mode, single-window platform\n");
            return failures ? 1 : 0;
        }
#if defined(SOKOL_GLCORE) || (defined(__linux__) && defined(SOKOL_GLES3) && !defined(__ANDROID__))
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        settings.sampleCount = 4;
        return tcCoreTest::runDisplayTest<GpuApp>(settings, failures, [] {
            check("secondary rendering completed", completed);
        });
#else
        std::printf("GPU checks require Linux OpenGL\n");
        return 2;
#endif
    }
    return failures ? 1 : 0;
}
