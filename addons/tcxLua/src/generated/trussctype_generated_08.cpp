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
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["height"] = &trussc::WindowSettings::height;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["maxFrameLatency"] = &trussc::WindowSettings::maxFrameLatency;
        t["pipelinePoolSize"] = &trussc::WindowSettings::pipelinePoolSize;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["quitOnClose"] = &trussc::WindowSettings::quitOnClose;
        t["resourcePoolSize"] = &trussc::WindowSettings::resourcePoolSize;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["shaderPoolSize"] = &trussc::WindowSettings::shaderPoolSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["title"] = &trussc::WindowSettings::title;
        t["visible"] = &trussc::WindowSettings::visible;
        t["width"] = &trussc::WindowSettings::width;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setMaxFrameLatency"] = &trussc::WindowSettings::setMaxFrameLatency;
        t["setParentWindow"] = sol::overload([](trussc::WindowSettings& self, const std::shared_ptr<trussc::Window> & parent) -> decltype(auto) { return self.setParentWindow(parent); }, [](trussc::WindowSettings& self, trussc::Window & parent) -> decltype(auto) { return self.setParentWindow(parent); });
        t["setPipelinePoolSize"] = &trussc::WindowSettings::setPipelinePoolSize;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setQuitOnClose"] = &trussc::WindowSettings::setQuitOnClose;
        t["setResourcePoolSize"] = &trussc::WindowSettings::setResourcePoolSize;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setShaderPoolSize"] = &trussc::WindowSettings::setShaderPoolSize;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
        t["setVisible"] = &trussc::WindowSettings::setVisible;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Ray
            , trussc::Ray()
            , trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)
        >();
        sol::usertype<trussc::Ray> t = lua->new_usertype<trussc::Ray>("Ray", constructors, sol::call_constructor, constructors);
        t["direction"] = &trussc::Ray::direction;
        t["origin"] = &trussc::Ray::origin;
        t["at"] = &trussc::Ray::at;
        t["intersectAABB"] = [](trussc::Ray& self, const trussc::Vec3 & boxMin, const trussc::Vec3 & boxMax) { return self.intersectAABB(boxMin, boxMax); };
        t["intersectPlane"] = [](trussc::Ray& self, const trussc::Vec3 & planeNormal, float planeD) { return self.intersectPlane(planeNormal, planeD); };
        t["intersectSphere"] = [](trussc::Ray& self, float radius) { return self.intersectSphere(radius); };
        t["intersectZPlane"] = [](trussc::Ray& self) { return self.intersectZPlane(); };
        t["transformed"] = &trussc::Ray::transformed;
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
    }
    {
        auto constructors = tcxLuaConstructors<trussc::IVec2
            , trussc::IVec2()
            , trussc::IVec2(int, int)
            , trussc::IVec2(int)
        >();
        sol::usertype<trussc::IVec2> t = lua->new_usertype<trussc::IVec2>("IVec2", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec2& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec2& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; };
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
    }
    lua->new_usertype<trussc::EaseType>("EaseType",
        sol::meta_function::equal_to, [](trussc::EaseType a, trussc::EaseType b){ return a == b; },
        "Linear", sol::var(trussc::EaseType::Linear),
        "Quad", sol::var(trussc::EaseType::Quad),
        "Cubic", sol::var(trussc::EaseType::Cubic),
        "Quart", sol::var(trussc::EaseType::Quart),
        "Quint", sol::var(trussc::EaseType::Quint),
        "Sine", sol::var(trussc::EaseType::Sine),
        "Expo", sol::var(trussc::EaseType::Expo),
        "Circ", sol::var(trussc::EaseType::Circ),
        "Back", sol::var(trussc::EaseType::Back),
        "Elastic", sol::var(trussc::EaseType::Elastic),
        "Bounce", sol::var(trussc::EaseType::Bounce),
        "Custom", sol::var(trussc::EaseType::Custom));
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["injected"] = &trussc::KeyEventArgs::injected;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["key"] = &trussc::KeyEventArgs::key;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["sourceTag"] = &trussc::KeyEventArgs::sourceTag;
        t["super"] = &trussc::KeyEventArgs::super;
    }
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["backend"] = &trussc::AudioDeviceInfo::backend;
        t["channels"] = &trussc::AudioDeviceInfo::channels;
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
        t["name"] = &trussc::AudioDeviceInfo::name;
        t["sampleRates"] = &trussc::AudioDeviceInfo::sampleRates;
        t["uniqueId"] = &trussc::AudioDeviceInfo::uniqueId;
    }
    lua->new_usertype<trussc::StrokeMesh::CapType>("StrokeMeshCapType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::CapType a, trussc::StrokeMesh::CapType b){ return a == b; },
        "CAP_BUTT", sol::var(trussc::StrokeMesh::CapType::CAP_BUTT),
        "CAP_ROUND", sol::var(trussc::StrokeMesh::CapType::CAP_ROUND),
        "CAP_SQUARE", sol::var(trussc::StrokeMesh::CapType::CAP_SQUARE));
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
    }
    {
        sol::usertype<trussc::GrabberFrame> t = lua->new_usertype<trussc::GrabberFrame>("GrabberFrame");
        t["pixels"] = &trussc::GrabberFrame::pixels;
        t["timestampUs"] = &trussc::GrabberFrame::timestampUs;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
