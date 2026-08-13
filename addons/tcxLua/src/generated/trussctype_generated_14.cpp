// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_14(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::LayoutMod> t = lua->new_usertype<trussc::LayoutMod>("LayoutMod",
            sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>(),
            sol::call_constructor, sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>());
        t["getDirection"] = &trussc::LayoutMod::getDirection;
        t["setDirection"] = &trussc::LayoutMod::setDirection;
        t["getSpacing"] = &trussc::LayoutMod::getSpacing;
        t["setSpacing"] = &trussc::LayoutMod::setSpacing;
        t["getCrossAxis"] = &trussc::LayoutMod::getCrossAxis;
        t["setCrossAxis"] = &trussc::LayoutMod::setCrossAxis;
        t["getMainAxis"] = &trussc::LayoutMod::getMainAxis;
        t["setMainAxis"] = &trussc::LayoutMod::setMainAxis;
        t["getPaddingLeft"] = &trussc::LayoutMod::getPaddingLeft;
        t["getPaddingTop"] = &trussc::LayoutMod::getPaddingTop;
        t["getPaddingRight"] = &trussc::LayoutMod::getPaddingRight;
        t["getPaddingBottom"] = &trussc::LayoutMod::getPaddingBottom;
        t["setPadding"] = sol::overload([](trussc::LayoutMod& self, float padding) -> decltype(auto) { return self.setPadding(padding); }, [](trussc::LayoutMod& self, float vertical, float horizontal) -> decltype(auto) { return self.setPadding(vertical, horizontal); }, [](trussc::LayoutMod& self, float top, float right, float bottom, float left) -> decltype(auto) { return self.setPadding(top, right, bottom, left); });
        t["setPaddingLeft"] = &trussc::LayoutMod::setPaddingLeft;
        t["setPaddingTop"] = &trussc::LayoutMod::setPaddingTop;
        t["setPaddingRight"] = &trussc::LayoutMod::setPaddingRight;
        t["setPaddingBottom"] = &trussc::LayoutMod::setPaddingBottom;
        t["updateLayout"] = &trussc::LayoutMod::updateLayout;
    }
    {
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh",
            sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>(),
            sol::call_constructor, sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>());
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["clear"] = &trussc::StrokeMesh::clear;
        t["update"] = &trussc::StrokeMesh::update;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
    }
    {
        sol::usertype<trussc::CoreEvents> t = lua->new_usertype<trussc::CoreEvents>("CoreEvents");
        t["setup"] = &trussc::CoreEvents::setup;
        t["update"] = &trussc::CoreEvents::update;
        t["draw"] = &trussc::CoreEvents::draw;
        t["onRender"] = &trussc::CoreEvents::onRender;
        t["afterFrame"] = &trussc::CoreEvents::afterFrame;
        t["exit"] = &trussc::CoreEvents::exit;
        t["exitRequested"] = &trussc::CoreEvents::exitRequested;
        t["keyPressed"] = &trussc::CoreEvents::keyPressed;
        t["keyReleased"] = &trussc::CoreEvents::keyReleased;
        t["mousePressed"] = &trussc::CoreEvents::mousePressed;
        t["mouseReleased"] = &trussc::CoreEvents::mouseReleased;
        t["mouseMoved"] = &trussc::CoreEvents::mouseMoved;
        t["mouseDragged"] = &trussc::CoreEvents::mouseDragged;
        t["mouseScrolled"] = &trussc::CoreEvents::mouseScrolled;
        t["windowResized"] = &trussc::CoreEvents::windowResized;
        t["filesDropped"] = &trussc::CoreEvents::filesDropped;
        t["clipboardPasted"] = &trussc::CoreEvents::clipboardPasted;
        t["console"] = &trussc::CoreEvents::console;
        t["touchPressed"] = &trussc::CoreEvents::touchPressed;
        t["touchMoved"] = &trussc::CoreEvents::touchMoved;
        t["touchReleased"] = &trussc::CoreEvents::touchReleased;
        t["rawEvent"] = &trussc::CoreEvents::rawEvent;
    }
    {
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
        t["build"] = &trussc::ChipSoundBundle::build;
    }
    {
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream",
            sol::constructors<trussc::SoundStream()>(),
            sol::call_constructor, sol::constructors<trussc::SoundStream()>());
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["view"] = &trussc::CameraContext::view;
        t["projection"] = &trussc::CameraContext::projection;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["pickable"] = &trussc::CameraContext::pickable;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    lua->new_usertype<trussc::VideoCodec>("VideoCodec",
        sol::meta_function::equal_to, [](trussc::VideoCodec a, trussc::VideoCodec b){ return a == b; },
        "H264", sol::var(trussc::VideoCodec::H264),
        "HEVC", sol::var(trussc::VideoCodec::HEVC),
        "ProRes422", sol::var(trussc::VideoCodec::ProRes422),
        "ProRes4444", sol::var(trussc::VideoCodec::ProRes4444));
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    {
        sol::usertype<trussc::EnumLabelSpan> t = lua->new_usertype<trussc::EnumLabelSpan>("EnumLabelSpan");
        t["count"] = &trussc::EnumLabelSpan::count;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
