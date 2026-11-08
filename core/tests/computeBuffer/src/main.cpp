#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#define GL_GLEXT_PROTOTYPES
#endif
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "pattern.compute.glsl.h"
#include "reader.compute.glsl.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#if defined(SOKOL_GLCORE)
#include <GL/gl.h>
#include <GL/glext.h>
#elif defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#endif
using namespace tc;
namespace {
int failures = 0;
bool completed = false;
void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
class BufferReader : public Shader {
    sg_pipeline_desc createPipelineDesc() override {
        auto desc = Shader::createPipelineDesc();
        desc.layout = {};
        return desc;
    }
};
void headlessChecks() {
    StorageBuffer buffer;
    check("headless allocation is refused", !buffer.allocate(64) && !buffer.isAllocated());
    const Vec4 value(1, 0, 0, 1);
    check("headless update is refused", !buffer.update(&value, sizeof(value)));
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    uint32_t sentinel = 0x12345678;
    check("empty read leaves destination untouched", !buffer.read(&sentinel, sizeof(sentinel)) && sentinel == 0x12345678);
    check("zero read needs no GPU or destination", buffer.read(nullptr, 0));
#endif
    ComputeShader compute;
    Shader graphics;
    check("headless compute binding is refused", !compute.setStorageBuffer(0, buffer));
    check("headless graphics binding is refused", !graphics.setStorageBuffer(0, buffer));
    check("headless checks leave GPU uninitialized", !sg_isvalid());
}

int ownerCallbacks = 0, ownerDestroyed = 0;
std::weak_ptr<int> ownerCapture;
class ReadbackRequester : public App {
    StorageBuffer buffer_;
    int marker_ = 7;
public:
    ~ReadbackRequester() override { ++ownerDestroyed; }
    void setup() override {
        auto capture = std::make_shared<int>(42);
        ownerCapture = capture;
        buffer_.readAsync([this, capture](ReadbackResult result) {
            ++ownerCallbacks;
            check("live requester callback retains its captures", marker_ == 7 && *capture == 42);
            check("headless owner request delivers deferred failure", !result.ok());
            check("callback restores requesting App owner", internal::currentAppOwner() == internal::appOwner(*this));
            check("callback resolves requesting Window", internal::resolveWindow() == getWindow());
            check("callback restores requesting Window context",
                internal::currentWindowCtx() == &internal::WindowRequestAccess::context(*getWindow()));
            check("callback sees requesting Window width", getWindowWidth() == 222);
            check("callback sees requesting App events", &tc::events() == internal::WindowRequestAccess::context(*getWindow()).coreEvents);
        });
    }
};
class ReadbackSibling : public App {};

void asyncOwnerChecks(bool closeRequester) {
    ownerCallbacks = ownerDestroyed = 0;
    getMainThreadId();
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    auto a = std::make_shared<Window>(), b = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*a, 401);
    internal::WindowRequestAccess::bindForTests(*b, 402);
    internal::retainRuntimeWindow(a);
    internal::retainRuntimeWindow(b);
    internal::WindowRequestAccess::context(*a).fbWidth = 111;
    internal::WindowRequestAccess::context(*b).fbWidth = 222;
    a->setApp<ReadbackSibling>();
    b->setApp<ReadbackRequester>();
    internal::applyPendingAppForTests(*a);
    internal::applyPendingAppForTests(*b);
    {
        internal::WindowEntryScope scope(*b, true);
        check("requester update is admitted", bool(scope));
        internal::runMainUpdate(); // B's setup requests after the queue poll.
    }
    check("owner request waits for another update", ownerCallbacks == 0 && !ownerCapture.expired());
    // A backend request must still retire when its App-owned callback is canceled.
    int retired = 0;
    struct RetirementProbe : internal::BufferReadbackRequest {
        int& retired;
        explicit RetirementProbe(int& count) : retired(count) {}
        ~RetirementProbe() override { ++retired; }
    };
    auto retirement = std::make_unique<RetirementProbe>(retired);
    retirement->owner = internal::appOwner(*b->getApp());
    retirement->callback = [&](ReadbackResult) {
        check("backend resources retire before live callback", !closeRequester && retired == 1);
    };
    internal::bufferReadbacks().enqueue(std::move(retirement));
    if (closeRequester) {
        internal::WindowRequestAccess::teardown(*b);
        check("requesting App is destroyed before delivery", ownerDestroyed == 1);
        check("App teardown releases pending callback captures", ownerCapture.expired());
    }
    bool unownedCalled = false;
    StorageBuffer empty;
    check("outside-App request has no owner", !internal::currentAppOwner());
    empty.readAsync([&](ReadbackResult) {
        unownedCalled = true;
        check("outside-App callback uses polling Window context", !internal::currentAppOwner() &&
            internal::currentWindowCtx() == &internal::WindowRequestAccess::context(*a));
    });
    {
        internal::WindowEntryScope scope(*a, true);
        check("sibling update is admitted", bool(scope));
        internal::runMainUpdate();
        check("delivery restores polling Window context", internal::currentWindowCtx() == &internal::WindowRequestAccess::context(*a));
    }
    check("ended request is dropped; live request is delivered once", ownerCallbacks == (closeRequester ? 0 : 1));
    check("delivered callback releases its captures", ownerCapture.expired());
    check("queue retires requests after requester teardown", retired == 1 && unownedCalled);
    if (!closeRequester) {
        auto owner = internal::appOwner(*b->getApp());
        const auto before = owner->cancellations.size();
        int finished = 0;
        bool attributed = true;
        internal::WindowEntryScope polling(*a, true);
        for (int i = 0; i < 100000; ++i) {
            {
                internal::AppCallbackScope requester(owner);
                empty.readAsync([&](ReadbackResult) {
                    ++finished;
                    attributed &= internal::currentAppOwner() == owner && getWindowWidth() == 222;
                });
            }
            internal::bufferReadbacks().poll();
        }
        check("100000 completed readbacks keep App cancellation records constant",
            finished == 100000 && owner->cancellations.size() == before);
        check("repeated readbacks retain requester attribution and restore polling context",
            attributed && internal::currentWindowCtx() == &internal::WindowRequestAccess::context(*a));
    }
    internal::shutdownRuntime();
    internal::endFrameworkLifetime();
}
class ComputeBufferApp : public App {
    StorageBuffer buffer_;
    ComputeShader compute_;
    Shader reader_;
    Shader instanced_;
    BufferReader legacy_;
    Shader ordinary_;
    Fbo target_;
    std::array<ShaderVertex, 24> vertices_ = {};
    void drawBuffer(float offset) {
        reader_.setUniform(UB_draw_params, Vec4(offset, 0, 0, 0));
        if (offset == 0) reader_.begin();
        reader_.drawVertices(8 * 3);
        if (offset == 0) reader_.end();
    }
    void vertexlessChecks() {
        target_.begin(0, 0, 0, 1);
        drawBuffer(0); drawBuffer(1);
        const auto& draws = internal::currentTarget().deferredShaderDraws;
        check("drawVertices records no stream or vertex/index bindings", draws.size() == 2 &&
            !draws[0].stream && draws[0].vertices.empty() &&
            !draws[0].bindings.vertex_buffers[0].id && !draws[0].bindings.index_buffer.id);
        check("scoped and standalone calls balance scopes", internal::currentWindowContext().shaderStack.empty());
        target_.end();
        expect("plain drawVertices storage pixels");
        target_.begin(0, 0, 0, 1);
        instanced_.setUniform(UB_draw_params, Vec4(0, 0, 0, 0));
        instanced_.drawVertices(3, 8);
        instanced_.setUniform(UB_draw_params, Vec4(1, 0, 0, 0));
        instanced_.drawVertices(3, 8);
        target_.end();
        expect("instanced drawVertices storage pixels and frozen uniforms");
        target_.begin(0, 0, 0, 1);
        for (int offset = 0; offset < 2; ++offset) {
            legacy_.setUniform(UB_draw_params, Vec4(float(offset), 0, 0, 0));
            legacy_.begin();
            legacy_.submitVertices(vertices_.data(), int(vertices_.size()), PrimitiveType::Triangles);
            legacy_.end();
        }
        target_.end();
        expect("submitVertices storage pixels are unchanged");
        StorageBuffer alternate;
        std::array<Vec4, 4> green = {Vec4(0,1,0,1), Vec4(0,1,1,1), Vec4(0,1,0,1), Vec4(0,1,1,1)};
        alternate.allocate(sizeof(green), green.data());
        target_.begin(0, 0, 0, 1);
        drawBuffer(0);
        reader_.setStorageBuffer(VIEW_pattern_input, alternate);
        drawBuffer(1);
        reader_.setStorageBuffer(VIEW_pattern_input, buffer_);
        target_.end();
        expect("storage bindings are frozen per drawVertices", false, true);
        target_.begin(0, 0, 0, 1);
        Shader missing;
        missing.load(buffer_reader_shader_desc);
        missing.drawVertices(24);
        check("drawVertices shares the missing-storage guard", internal::currentTarget().deferredShaderDraws.empty());
        target_.end();


        target_.begin(0, 0, 0, 1);
        auto& target = internal::currentTarget();
        const auto queued = target.deferredShaderDraws.size();
        const auto layer = target.sglLayerNext;
        int warnings = 0, attributeWarnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Warning && event.message.find("drawVertices requires nonnegative") != std::string::npos) ++warnings;
            if (event.level == LogLevel::Warning && event.message.find("drawVertices requires a shader without vertex attributes") != std::string::npos) ++attributeWarnings;
        });
        reader_.drawVertices(0); reader_.drawVertices(24, 0);
        check("zero counts do not record or advance a layer", target.deferredShaderDraws.size() == queued && target.sglLayerNext == layer && warnings == 0);
        reader_.drawVertices(-1); reader_.drawVertices(24, -1); reader_.drawVertices(-1);
        check("negative counts are refused and warn once per second", target.deferredShaderDraws.size() == queued && target.sglLayerNext == layer && warnings == 1);
        ordinary_.drawVertices(3); ordinary_.drawVertices(3); ordinary_.drawVertices(3);
        check("attribute-consuming shader is refused and warns once per second", target.deferredShaderDraws.size() == queued && target.sglLayerNext == layer && attributeWarnings == 1);
        ordinary_.begin();
        setColor(1, 0, 0, 1);
        drawRect(-1, -1, 2, 2);
        ordinary_.end();
        target_.end();
        std::array<unsigned char, 32 * 4 * 4> pixels = {};
        bool red = target_.readPixels(pixels.data());
        for (size_t i = 0; i < pixels.size(); i += 4) red &= pixels[i] == 255 && pixels[i+1] == 0 && pixels[i+2] == 0 && pixels[i+3] == 255;
        check("ordinary Shader draw pixels are unchanged", red);
    }

#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    void readbackChecks() {
        struct Sample { std::array<uint32_t, 4> integers; std::array<float, 4> floats; };
        std::array<Sample, 4> expected = {}, snapshot = {}, later = {};
        for (uint32_t i = 0; i < expected.size(); ++i) {
            expected[i].integers = {i, 0xffffffffu - i, 0x12345678u, i * 37u};
            expected[i].floats = {float(i), -0.5f, 0.125f, 1024.0f};
        }
        StorageBuffer data;
        ComputeShader writer;
        check("readback storage allocates", data.allocate(sizeof(expected)));
        check("readback shader loads", writer.load(buffer_readback_shader_desc));
        check("readback shader binds", writer.setStorageBuffer(VIEW_readback_output, data));
        writer.setUniform(UB_readback_params, Vec4(0, 0, 0, 0));
        check("readback pattern dispatch", writer.dispatch(1));
        const auto frame = sg_query_stats().prev_frame.frame_index;
        check("integer and float compute patterns read exactly", data.read(snapshot.data(), sizeof(snapshot)) &&
            std::memcmp(snapshot.data(), expected.data(), sizeof(expected)) == 0);
        writer.setUniform(UB_readback_params, Vec4(16, 0, 0, 0));
        check("later compute write dispatch", writer.dispatch(1));
        check("second read succeeds in same frame", data.read(later.data(), sizeof(later)));
        check("earlier read is an owned CPU snapshot", std::memcmp(snapshot.data(), expected.data(), sizeof(expected)) == 0);
        for (auto& value : expected) { value.integers[0] += 16; value.floats[0] += 16; }
        check("second read sees later compute", std::memcmp(later.data(), expected.data(), sizeof(expected)) == 0);
        check("read does not commit a device frame", frame == sg_query_stats().prev_frame.frame_index);
        std::array<unsigned char, sizeof(expected) + 4> sentinel;
        sentinel.fill(0xa5);
        const auto untouched = sentinel;
        int warnings = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Warning && event.message.find("read requires") != std::string::npos) ++warnings;
        });
        StorageBuffer small;
        small.allocate(sizeof(expected));
        check("oversized read is refused twice", !small.read(sentinel.data(), sentinel.size()) &&
            !small.read(sentinel.data(), sentinel.size()));
        check("oversized reads warn once per 5 seconds", warnings == 1);
        check("failed read leaves all destination bytes untouched", sentinel == untouched);
        check("null destination is refused", !data.read(nullptr, 4));
        check("zero read succeeds on allocated storage", data.read(nullptr, 0));
        StorageBuffer empty;
        check("empty storage is refused", !empty.read(sentinel.data(), 4) && sentinel == untouched);
        check("zero read succeeds on empty storage", empty.read(nullptr, 0));
        check("partial byte read succeeds without alignment restriction", data.read(sentinel.data(), 7) &&
            std::memcmp(sentinel.data(), expected.data(), 7) == 0);
        bool tailUntouched = true;
        for (size_t i = 7; i < sentinel.size(); ++i) tailUntouched &= sentinel[i] == 0xa5;
        check("partial read preserves destination suffix", tailUntouched);

        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        compute_.dispatch(1);
        target_.begin(0, 0, 0, 1);
        drawBuffer(0);
        std::array<Vec4, 4> colors = {};
        check("read between Fbo draws succeeds", buffer_.read(colors.data(), sizeof(colors)) && colors[0] == Vec4(1,0,0,1));
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        compute_.dispatch(1);
        drawBuffer(1);
        check("read flushes later Fbo draw", buffer_.read(colors.data(), sizeof(colors)) && colors[0] == Vec4(0,1,0,1));
        target_.end();
        expect("Fbo draws preserve ordering across reads", false, true);
    }
#endif

    uint32_t setupFrame_ = 0;
    void expectBytes(const unsigned char* pixels, int width, int height, const char* label,
                     bool prefixGreen, bool suffixGreen, bool firstOnly, bool checkAlpha) {
        bool matches = pixels != nullptr;
        for (int y = 0; y < height && matches; ++y) for (int x = 0; x < width; ++x) {
            const auto* pixel = &pixels[(y * width + x) * 4];
            const int cell = (2 * x + 1) * 4 / width;
            const bool green = (cell < 4 ? prefixGreen : suffixGreen) && (!firstOnly || cell % 4 == 0);
            matches &= pixel[0] == (green ? 0 : 255) && pixel[1] == (green ? 255 : 0) &&
                pixel[2] == ((cell % 2) ? 255 : 0) && (!checkAlpha || pixel[3] == 255);
        }
        check(label, matches);
    }
    void expect(const char* label, bool prefixGreen = false, bool suffixGreen = false, bool firstOnly = false) {
        std::array<unsigned char, 32 * 4 * 4> pixels = {};
        check("buffer drawing readback succeeds", target_.readPixels(pixels.data()));
        expectBytes(pixels.data(), 32, 4, label, prefixGreen, suffixGreen, firstOnly, true);
    }

public:
    void setup() override {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
        std::printf("GPU: %s / %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
#endif
        check("backend supports compute", sg_query_features().compute);
        if (!sg_query_features().compute) { exitApp(1); return; }
        std::array<Vec4, 4> initial = {Vec4(1,0,0,1), Vec4(1,0,1,1), Vec4(1,0,0,1), Vec4(1,0,1,1)};
        check("storage allocation with initial data succeeds", buffer_.allocate(sizeof(initial), initial.data()));
        const auto desc = sg_query_buffer_desc(buffer_.getBuffer());
        check("storage has persistent byte size and view", buffer_.getSize() == sizeof(initial) &&
            desc.size == sizeof(initial) && desc.usage.copy_src && desc.usage.storage_buffer && !desc.usage.write_transient &&
            sg_query_view_buffer(buffer_.getView()).id == buffer_.getBuffer().id);
        check("compute descriptor loads", compute_.load(buffer_pattern_shader_desc));
        check("compute buffer binding succeeds", compute_.setStorageBuffer(VIEW_pattern_output, buffer_));
        check("graphics descriptor loads", reader_.load(buffer_reader_shader_desc));
        check("graphics storage is fragment-stage readonly",
            buffer_reader_shader_desc(sg_query_backend())->views[VIEW_pattern_input].storage_buffer.stage == SG_SHADERSTAGE_FRAGMENT &&
            buffer_reader_shader_desc(sg_query_backend())->views[VIEW_pattern_input].storage_buffer.readonly);
        check("graphics buffer binding succeeds", reader_.setStorageBuffer(VIEW_pattern_input, buffer_));
        check("negative compute slot is refused", !compute_.setStorageBuffer(-1, buffer_));
        check("undeclared compute slot is refused", !compute_.setStorageBuffer(1, buffer_));
        check("graphics slot limit is refused", !reader_.setStorageBuffer(SG_MAX_VIEW_BINDSLOTS, buffer_));
        check("undeclared graphics slot is refused", !reader_.setStorageBuffer(1, buffer_));
        check("instanced descriptor loads", instanced_.load(buffer_instanced_reader_shader_desc));
        check("instanced storage binding succeeds", instanced_.setStorageBuffer(VIEW_pattern_input, buffer_));
        check("legacy descriptor loads", legacy_.load(buffer_reader_shader_desc));
        check("legacy storage binding succeeds", legacy_.setStorageBuffer(VIEW_pattern_input, buffer_));
        check("ordinary descriptor loads", ordinary_.load(buffer_ordinary_shader_desc));
        target_.allocate(32, 4);
        vertexlessChecks();
        target_.begin();
        drawBuffer(0); drawBuffer(1);
        target_.end();
        expect("initial CPU data reaches graphics");
        initial[0] = Vec4(0, 1, 0, 1);
        check("first CPU upload succeeds", buffer_.update(initial.data(), sizeof(Vec4)));
        setupFrame_ = sg_query_stats().prev_frame.frame_index;
        target_.begin();
        drawBuffer(0); drawBuffer(1);
        target_.end();
        expect("partial CPU upload reaches graphics and preserves remaining bytes", true, true, true);
        check("second CPU upload in same device frame is refused", !buffer_.update(initial.data(), sizeof(initial)));
        check("null CPU upload is refused", !buffer_.update(nullptr, 16));
        check("oversized CPU upload is refused", !buffer_.update(initial.data(), sizeof(initial) + 16));
        check("unaligned CPU upload is refused", !buffer_.update(initial.data(), 3));
    }
    void draw() override {
        auto logs = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level >= LogLevel::Error) { ++failures; std::printf("GPU ERROR %s\n", event.message.c_str()); }
        });
        // setup() and the first draw may share a device frame.
        if (setupFrame_ == sg_query_stats().prev_frame.frame_index) return;

        reader_.begin();
        const int layer = internal::currentTarget().sglLayerNext;
        reader_.setUniform(UB_draw_params, Vec4(0, 0, 0, 0));
        reader_.drawVertices(24);
        check("drawVertices reuses the active swapchain scope", internal::currentTarget().sglLayerNext == layer &&
            internal::currentWindowContext().shaderStack.size() == 1);
        reader_.end();
        std::array<Vec4, 4> initial = {Vec4(1,0,0,1), Vec4(1,0,1,1), Vec4(1,0,0,1), Vec4(1,0,1,1)};
        check("CPU upload in a later device frame succeeds", buffer_.update(initial.data(), sizeof(initial)));
        target_.begin();
        drawBuffer(0);
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        check("compute writes known pattern mid-frame", compute_.dispatch(1));
        drawBuffer(1);
        target_.end();
        expect("graphics prefix sees CPU data and suffix sees compute output", false, true);
        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        check("restore buffer before window prefix", compute_.dispatch(1));
        drawBuffer(0);
        check("window buffer prefix is lazy", !internal::currentTarget().inPass);
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
        std::array<Vec4, 4> windowRead = {};
        check("read flushes window draw prefix", buffer_.read(windowRead.data(), sizeof(windowRead)) && windowRead[0] == Vec4(1,0,0,1));
#endif
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        check("dispatch flushes recorded window buffer drawing", compute_.dispatch(1));
        drawBuffer(1);
        internal::ensureCurrentPass();
        internal::parkWindowPass();
        Pixels screen;
        check("window buffer readback succeeds", grabScreen(screen));
        expectBytes(screen.getData(), screen.getWidth(), screen.getHeight(),
                    "window prefix sees old buffer and suffix sees compute output", false, true, false, false);

#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
        readbackChecks();
#endif

        // Deferred recordings keep handles alive when the owner is cleared.
        target_.begin();
        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        check("repeat dispatch in same device frame succeeds", compute_.dispatch(1));
        drawBuffer(0); drawBuffer(1);
        StorageBuffer moved(std::move(buffer_));
        check("move transfers buffer, view and size", moved.isAllocated() && moved.getSize() == sizeof(initial) &&
            !buffer_.isAllocated() && !buffer_.getBuffer().id && !buffer_.getView().id && !buffer_.getSize());
        moved.clear();
        check("clear resets storage", !moved.isAllocated() && !moved.getSize() && !moved.getBuffer().id && !moved.getView().id);
        target_.end();
        expect("cleared owner retains resources for recorded graphics");
        check("binding rejects cleared storage", !compute_.setStorageBuffer(0, moved));
        StorageBuffer empty;
        check("zero allocation is refused", !empty.allocate(0));
        check("unaligned allocation is refused", !empty.allocate(3));
        check("allocation without initial data succeeds", empty.allocate(64));
        StorageBuffer assigned;
        assigned.allocate(16);
        assigned = std::move(empty);
        check("move assignment transfers storage", assigned.isAllocated() && assigned.getSize() == 64 && !empty.isAllocated());
        completed = true;
        exitApp();
    }
};

bool asyncExitMode = false;
bool asyncMapExitMode = false;
int asyncCallbacks = 0;
class AsyncReadbackApp : public App {
    StorageBuffer buffer_;
    ComputeShader compute_;
    std::thread::id thread_;
    uint64_t requestUpdate_ = 0;
    uint32_t requestFrame_ = 0;
    std::vector<ReadbackResult> results_;
    EventListener errors_;
#if defined(SOKOL_WGPU)
    bool drew_ = false;
    uint32_t drawFrame_ = 0;
#endif
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
    GLuint bindings_[2] = {};
    void checkCopyBindings() {
        GLint readBinding = 0, writeBinding = 0;
        glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &readBinding);
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &writeBinding);
        check("readAsync restores copy bindings", GLuint(readBinding) == bindings_[0] && GLuint(writeBinding) == bindings_[1]);
    }
#endif
    void receive(int index, ReadbackResult result, float red, float green) {
        check("readAsync callback uses main thread", std::this_thread::get_id() == thread_ && isMainThread());
        check("readAsync callback uses a later update", internal::currentWindowContext().updateCount > requestUpdate_);
        check("readAsync callbacks preserve request order", asyncCallbacks++ == index);
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
        checkCopyBindings();
#endif
#if defined(SOKOL_WGPU) || defined(SOKOL_METAL) || defined(SOKOL_D3D11) || ((defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__))
        std::array<Vec4, 4> expected = {Vec4(red,green,0,1), Vec4(red,green,1,1),
                                       Vec4(red,green,0,1), Vec4(red,green,1,1)};
        check("readAsync owns exact snapshot bytes", result.ok() && result.size() == sizeof(expected) &&
            std::memcmp(result.getData(), expected.data(), sizeof(expected)) == 0);
#else
        (void)red; (void)green;
        check("pending backend reports deferred failure", !result.ok() && result.size() == 0);
#endif
        results_.push_back(std::move(result));
    }
public:
    AsyncReadbackApp() {
#if defined(SOKOL_WGPU)
        // Exercise compute and Fbo rendering without a presentation surface;
        // SwiftShader's headless canvas sharing depends on the Chrome build.
        setIndependentFps(VSYNC, EVENT_DRIVEN);
        internal::currentWindowContext().loop.redrawCount = 0;
#endif
    }
    void setup() override {
        errors_ = getLogger().onLog.listen([](LogEventArgs& event) {
            if (event.level >= LogLevel::Error) { ++failures; std::printf("GPU ERROR %s\n", event.message.c_str()); }
        });
        thread_ = std::this_thread::get_id();
        requestUpdate_ = internal::currentWindowContext().updateCount;
        requestFrame_ = sg_query_stats().prev_frame.frame_index;
        check("async storage allocation succeeds", buffer_.allocate(sizeof(Vec4) * 4));
        check("async compute loads and binds", compute_.load(buffer_pattern_shader_desc) &&
            compute_.setStorageBuffer(VIEW_pattern_output, buffer_));
        compute_.setUniform(UB_buffer_params, Vec4(1, 0, 0, 0));
        check("async first pattern dispatch succeeds", compute_.dispatch(1));
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
        glGenBuffers(2, bindings_);
        glBindBuffer(GL_COPY_READ_BUFFER, bindings_[0]);
        glBindBuffer(GL_COPY_WRITE_BUFFER, bindings_[1]);
#endif
        buffer_.readAsync([this](ReadbackResult result) { receive(0, std::move(result), 1, 0); });
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
        checkCopyBindings();
#endif
        if (asyncExitMode) {
            // Test both an encoded copy and shutdown after polling starts a map.
            if (!asyncMapExitMode) exitApp();
            return;
        }
        check("readAsync never calls back inside the request", asyncCallbacks == 0);
        compute_.setUniform(UB_buffer_params, Vec4(0, 1, 0, 0));
        check("async later compute write succeeds", compute_.dispatch(1));
        buffer_.readAsync([this](ReadbackResult result) { receive(1, std::move(result), 0, 1); });
        {
            StorageBuffer temporary;
            const std::array<Vec4, 4> data = {Vec4(1,1,0,1), Vec4(1,1,1,1), Vec4(1,1,0,1), Vec4(1,1,1,1)};
            check("temporary storage allocation succeeds", temporary.allocate(sizeof(data), data.data()));
            temporary.readAsync([this](ReadbackResult result) { receive(2, std::move(result), 1, 1); });
        }
        // Both the original source and the temporary source are gone before delivery.
        buffer_.clear();
        StorageBuffer empty;
        empty.readAsync([this](ReadbackResult result) {
            check("invalid async request is deferred and ordered", asyncCallbacks++ == 3 && !result.ok() && result.size() == 0);
        });
    }
    void update() override {
#if defined(SOKOL_WGPU)
        // Native resize/bootstrap can request a first presentation after setup.
        internal::currentWindowContext().loop.redrawCount = 0;
#endif
        if (asyncExitMode) {
            if (asyncMapExitMode && requestFrame_ != sg_query_stats().prev_frame.frame_index) exitApp();
            return;
        }
#if defined(SOKOL_WGPU)
        if (drew_) {
            // Let the normal frame submit the draw before reporting success.
            if (drawFrame_ != sg_query_stats().prev_frame.frame_index) {
                completed = true;
                exitApp();
            }
            return;
        }
#endif
        if (asyncCallbacks == 4) {
            check("owned results survive callback return", results_.size() == 3);
            Fbo target;
            target.allocate(4, 4);
            target.begin(0, 1, 0, 1);
            setColor(1, 0, 0, 1);
            drawRect(0, 0, 4, 4);
            target.end();
#if defined(SOKOL_WGPU)
            // WebGPU has no synchronous Fbo readPixels. Replay the actual draw
            // and check its submission for errors instead of calling that API.
            internal::suspendForImageUpload();
            check("drawing after async readback ends its GPU pass", sg_query_pass_state() == SG_PASSSTATE_NONE);
            drew_ = true;
            drawFrame_ = sg_query_stats().prev_frame.frame_index;
#else
            std::array<unsigned char, 4 * 4 * 4> pixels = {};
            bool matches = target.readPixels(pixels.data());
            for (size_t i = 0; i < pixels.size(); i += 4)
                matches &= pixels[i] == 255 && pixels[i+1] == 0 && pixels[i+2] == 0 && pixels[i+3] == 255;
            check("drawing after async readback preserves GPU state", matches);
            completed = true;
            exitApp();
#endif
        }
    }
    void cleanup() override {
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glDeleteBuffers(2, bindings_);
        sg_reset_state_cache();
#endif
    }
};
} // namespace
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__EMSCRIPTEN__)
    char asyncMode[] = "--async-check", exitMode[] = "--async-exit-check", mapExitMode[] = "--async-map-exit-check";
    const int mode = EM_ASM_INT({
        const mode = new URLSearchParams(location.search).get('mode');
        return mode === 'map-exit' ? 2 : mode === 'exit' ? 1 : 0;
    });
    char* webArgs[] = {argv[0], mode == 2 ? mapExitMode : mode == 1 ? exitMode : asyncMode};
    argc = 2;
    argv = webArgs;
#endif
    if (argc > 1 && std::strcmp(argv[1], "--async-owner-check") == 0) {
        asyncOwnerChecks(true);
        asyncOwnerChecks(false);
        return failures ? 1 : 0;
    }
    if (argc > 1 && (std::strcmp(argv[1], "--async-check") == 0 || std::strcmp(argv[1], "--async-exit-check") == 0 ||
                    std::strcmp(argv[1], "--async-map-exit-check") == 0)) {
        asyncMapExitMode = std::strcmp(argv[1], "--async-map-exit-check") == 0;
        asyncExitMode = asyncMapExitMode || std::strcmp(argv[1], "--async-exit-check") == 0;
        WindowSettings settings;
        settings.setSize(64, 64).setHighDpi(false).setSampleCount(1);
        return tcCoreTest::runDisplayTest<AsyncReadbackApp>(settings, failures, [] {
            check("async GPU checks completed", asyncExitMode ? asyncCallbacks == 0 : completed);
#if defined(__EMSCRIPTEN__)
            EM_ASM({ window.tcComputeBufferVerdict = $0 ? 'FAIL computeBuffer readAsync' : 'PASS computeBuffer readAsync'; }, failures);
#endif
        });
    }
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64).setHighDpi(false).setSampleCount(1);
        return tcCoreTest::runDisplayTest<ComputeBufferApp>(settings, failures, [] { check("GPU checks completed", completed); });
    }
    headlessChecks();
    asyncOwnerChecks(true);
    asyncOwnerChecks(false);
    return failures ? 1 : 0;
}
