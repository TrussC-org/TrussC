// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
// Abstract bases (e.g. VideoPlayerBase) expose members but cannot be constructed.
// An empty guarded constructor list must not enable sol2's automatic default ctor.
namespace {
template<typename Owner, typename... Signatures>
auto tcxLuaConstructors() {
    if constexpr (std::is_abstract_v<Owner> || sizeof...(Signatures) == 0) return sol::no_constructor;
    else return sol::constructors<Signatures...>();
}
}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_08(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Path
            , trussc::Path()
            , trussc::Path(const std::vector<trussc::Vec2> &)
            , trussc::Path(const std::vector<trussc::Vec3> &)
        >();
        sol::usertype<trussc::Path> t = lua->new_usertype<trussc::Path>("Path", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::index] = [](const trussc::Path& a, int b){ return a[b]; };
        t["addVertex"] = sol::overload([](trussc::Path& self, float x, float y) { return self.addVertex(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.addVertex(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & v) { return self.addVertex(v); }, [](trussc::Path& self, const trussc::Vec3 & v) { return self.addVertex(v); });
        t["addVertices"] = sol::overload([](trussc::Path& self, const std::vector<trussc::Vec2> & verts) { return self.addVertices(verts); }, [](trussc::Path& self, const std::vector<trussc::Vec3> & verts) { return self.addVertices(verts); });
        t["arc"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise, int circleResolution) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, clockwise, circleResolution); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(x, y, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arc(x, y, radiusX, radiusY, angleBegin, angleEnd, circleResolution); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arc(center, radiusX, radiusY, angleBegin, angleEnd, circleResolution); }, [](trussc::Path& self, const trussc::Vec3 & center, float radius, float angleBegin, float angleEnd) { return self.arc(center, radius, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec3 & center, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radius, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, float x, float y, float radius, float angleBegin, float angleEnd) { return self.arc(x, y, radius, angleBegin, angleEnd); }, [](trussc::Path& self, float x, float y, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(x, y, radius, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, const trussc::Vec2 & center, float radius, float angleBegin, float angleEnd) { return self.arc(center, radius, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec2 & center, float radius, float angleBegin, float angleEnd, bool clockwise) { return self.arc(center, radius, angleBegin, angleEnd, clockwise); });
        t["arcDeg"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arcDeg(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise) { return self.arcDeg(center, radiusX, radiusY, angleBegin, angleEnd, clockwise); }, [](trussc::Path& self, const trussc::Vec3 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, bool clockwise, int circleResolution) { return self.arcDeg(center, radiusX, radiusY, angleBegin, angleEnd, clockwise, circleResolution); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arcDeg(x, y, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, float x, float y, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arcDeg(x, y, radiusX, radiusY, angleBegin, angleEnd, circleResolution); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd) { return self.arcDeg(center, radiusX, radiusY, angleBegin, angleEnd); }, [](trussc::Path& self, const trussc::Vec2 & center, float radiusX, float radiusY, float angleBegin, float angleEnd, int circleResolution) { return self.arcDeg(center, radiusX, radiusY, angleBegin, angleEnd, circleResolution); });
        t["bezierTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & cp1, const trussc::Vec3 & cp2, const trussc::Vec3 & to, int resolution) { return self.bezierTo(cp1, cp2, to, resolution); }, [](trussc::Path& self, float cx1, float cy1, float cx2, float cy2, float x, float y, int resolution) { return self.bezierTo(cx1, cy1, cx2, cy2, x, y, resolution); }, [](trussc::Path& self, const trussc::Vec2 & cp1, const trussc::Vec2 & cp2, const trussc::Vec2 & to, int resolution) { return self.bezierTo(cp1, cp2, to, resolution); });
        t["buildFillTriangles"] = &trussc::Path::buildFillTriangles;
        t["clear"] = &trussc::Path::clear;
        t["close"] = &trussc::Path::close;
        t["curveTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & to, int resolution) { return self.curveTo(to, resolution); }, [](trussc::Path& self, float x, float y) { return self.curveTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.curveTo(x, y, z); }, [](trussc::Path& self, float x, float y, float z, int resolution) { return self.curveTo(x, y, z, resolution); }, [](trussc::Path& self, const trussc::Vec2 & to, int resolution) { return self.curveTo(to, resolution); });
        t["draw"] = &trussc::Path::draw;
        t["drawFill"] = &trussc::Path::drawFill;
        t["drawStroke"] = &trussc::Path::drawStroke;
        t["empty"] = &trussc::Path::empty;
        t["getBounds"] = &trussc::Path::getBounds;
        t["getNumSubpaths"] = &trussc::Path::getNumSubpaths;
        t["getPerimeter"] = &trussc::Path::getPerimeter;
        t["getSubpathRange"] = &trussc::Path::getSubpathRange;
        t["getVertices"] = [](trussc::Path& self) -> decltype(auto) { return self.getVertices(); };
        t["isClosed"] = &trussc::Path::isClosed;
        t["isSubpathClosed"] = &trussc::Path::isSubpathClosed;
        t["lineTo"] = sol::overload([](trussc::Path& self, float x, float y) { return self.lineTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.lineTo(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & p) { return self.lineTo(p); }, [](trussc::Path& self, const trussc::Vec3 & p) { return self.lineTo(p); });
        t["moveTo"] = sol::overload([](trussc::Path& self, float x, float y) { return self.moveTo(x, y); }, [](trussc::Path& self, float x, float y, float z) { return self.moveTo(x, y, z); }, [](trussc::Path& self, const trussc::Vec2 & p) { return self.moveTo(p); }, [](trussc::Path& self, const trussc::Vec3 & p) { return self.moveTo(p); });
        t["quadBezierTo"] = sol::overload([](trussc::Path& self, const trussc::Vec3 & cp, const trussc::Vec3 & to, int resolution) { return self.quadBezierTo(cp, to, resolution); }, [](trussc::Path& self, float cx, float cy, float x, float y, int resolution) { return self.quadBezierTo(cx, cy, x, y, resolution); }, [](trussc::Path& self, const trussc::Vec2 & cp, const trussc::Vec2 & to, int resolution) { return self.quadBezierTo(cp, to, resolution); });
        t["reverseWinding"] = sol::overload([](trussc::Path& self, size_t i) -> decltype(auto) { return self.reverseWinding(i); }, [](trussc::Path& self) -> decltype(auto) { return self.reverseWinding(); });
        t["setClosed"] = &trussc::Path::setClosed;
        t["size"] = &trussc::Path::size;
        t["toFillMesh"] = &trussc::Path::toFillMesh;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Vec4
            , trussc::Vec4()
            , trussc::Vec4(float, float, float, float)
            , trussc::Vec4(float)
            , trussc::Vec4(const trussc::Vec3 &)
            , trussc::Vec4(const trussc::Vec3 &, float)
            , trussc::Vec4(const trussc::Vec2 &)
            , trussc::Vec4(const trussc::Vec2 &, float)
            , trussc::Vec4(const trussc::Vec2 &, float, float)
        >();
        sol::usertype<trussc::Vec4> t = lua->new_usertype<trussc::Vec4>("Vec4", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a * b; }, [](const trussc::Vec4& a, float b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec4& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a / b; }, [](const trussc::Vec4& a, float b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec4& a, int b){ return a[b]; };
        t["w"] = &trussc::Vec4::w;
        t["x"] = &trussc::Vec4::x;
        t["y"] = &trussc::Vec4::y;
        t["z"] = &trussc::Vec4::z;
        t["dot"] = &trussc::Vec4::dot;
        t["length"] = &trussc::Vec4::length;
        t["lengthSquared"] = &trussc::Vec4::lengthSquared;
        t["lerp"] = &trussc::Vec4::lerp;
        t["normalize"] = &trussc::Vec4::normalize;
        t["normalized"] = &trussc::Vec4::normalized;
        t["set"] = sol::overload([](trussc::Vec4& self, float x_, float y_, float z_, float w_) -> decltype(auto) { return self.set(x_, y_, z_, w_); }, [](trussc::Vec4& self, const trussc::Vec4 & v) -> decltype(auto) { return self.set(v); });
        t["xy"] = &trussc::Vec4::xy;
        t["xyz"] = &trussc::Vec4::xyz;
    }
    {
        sol::usertype<trussc::HasTexture> t = lua->new_usertype<trussc::HasTexture>("HasTexture");
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["save"] = &trussc::HasTexture::save;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
    }
    {
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["build"] = &trussc::ChipSoundBundle::build;
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
    }
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    {
        sol::usertype<trussc::SerialDisconnectEventArgs> t = lua->new_usertype<trussc::SerialDisconnectEventArgs>("SerialDisconnectEventArgs");
        t["baudRate"] = &trussc::SerialDisconnectEventArgs::baudRate;
        t["portName"] = &trussc::SerialDisconnectEventArgs::portName;
        t["reason"] = &trussc::SerialDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::SerialDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
        t["name"] = &trussc::AudioDeviceInfo::name;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
