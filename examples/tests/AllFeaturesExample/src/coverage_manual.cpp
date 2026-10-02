// =============================================================================
// AllFeaturesExample — hand-written API coverage
//
// What coverage_generated.cpp can't express: templates (instantiated with
// concrete types here) and the bundled addons this example links. Same rule
// as the generated file: referenced, never run (see coverage.h).
// =============================================================================

#include "coverage.h"

#include <tcLut.h>
#include <tcxDepthCamera.h>
#include <tcxDepthRecord.h>
#include <tcxImGui.h>
#include <tcxObj.h>
#include <tcxOsc.h>
#include <tcxQuadWarp.h>

namespace trussc {

volatile bool af::never = false;
void* volatile af::sink = nullptr;

namespace {

void coverTemplates() {
    Tween<float> tw;
    tw.from(0.0f).to(1.0f).duration(1.0f).ease(EaseType::Cubic).loop(1).yoyo().delay(0.0f);
    tw.start();
    tw.pause();
    tw.resume();
    tw.reset();
    tw.finish();
    (void)tw.getValue();
    (void)tw.getProgress();
    (void)tw.getElapsed();
    Tween<Vec3> twv;
    twv.from(Vec3()).to(Vec3(1, 2, 3)).start();

    Event<int> ev;
    EventListener l = ev.listen([](int&) {});
    int arg = 0;
    ev.notify(arg);
    (void)ev.listenerCount();
    ev.clear();

    ThreadChannel<int> ch;
    ch.send(1);
    int got = 0;
    (void)ch.tryReceive(got);
    (void)ch.tryReceive(got, 1);
    (void)ch.receive(got);
    std::vector<int> all = ch.receiveAll();
    (void)all;
    (void)ch.empty();
    (void)ch.size();
    (void)ch.isClosed();
    ch.clear();
    ch.close();

    (void)toHex(255);
    (void)toHex(std::string("ab"));
    (void)toHex(af::val<std::uint32_t>());
    (void)toString(1.5f);
    (void)toString(1.5f, 2);
    (void)toString(7, 3, '0');
    (void)toString(std::vector<int>{1, 2});

    Node& node = af::val<Node>();
    Json j = reflectToJson(node);
    (void)reflectFromJson(node, j);

    (void)enumLabel(BlendMode::Add);
    (void)enumNames<BlendMode>();
    (void)enumValues<BlendMode>();
    (void)enumReflectedSpan<BlendMode>();
}

void coverAddons() {
    // tcxLut
    tcx::lut::Lut3D lut = tcx::lut::createVintage(16);
    (void)lut.load(fs::path("grade.cube"));
    (void)lut.isAllocated();
    (void)lut.getSize();
    lut.clear();

    // tcxOsc
    tcx::osc::OscSender sender;
    (void)sender.setup("127.0.0.1", 12345);
    sender.disconnect();
    tcx::osc::OscReceiver receiver;
    (void)receiver.setup(12346);
    (void)receiver.hasNewMessage();
    receiver.close();

    // tcxObj
    tcx::obj::ObjLoader loader;
    (void)loader.load(fs::path("model.obj"));
    (void)loader.getMesh();
    (void)loader.getNumGroups();
    tcx::obj::ObjExporter exporter;
    exporter.addMesh(af::val<Mesh>(), "mesh").setScale(1.0f).setFlipYZ(false);

    // tcxQuadWarp
    tcx::quadwarp::QuadWarp warp;
    warp.setup();
    warp.setSourceRect(Rect(0, 0, 100, 100));
    warp.setTargetRect(Rect(0, 0, 100, 100));
    warp.update();
    warp.draw();

    // tcxDepthCamera / tcxDepthRecord
    tcx::depthcamera::SyntheticDepthCamera cam;
    tcx::depthrecord::DepthRecorder recorder;
    (void)recorder.isRecording();
    (void)recorder.getFrameCount();
    recorder.record(cam);
    recorder.stop();

    // tcxImGui
    tcx::imgui::imguiSetup();
    tcx::imgui::imguiBegin();
    ImGui::Text("coverage");
    tcx::imgui::imguiEnd();
    (void)tcx::imgui::imguiWantsMouse();
    (void)tcx::imgui::imguiWantsKeyboard();
}

} // namespace

void af::coverManual() {
    coverTemplates();
    coverAddons();
}

} // namespace trussc
