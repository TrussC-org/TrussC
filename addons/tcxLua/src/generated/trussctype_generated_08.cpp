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
        auto constructors = tcxLuaConstructors<trussc::Window
            , trussc::Window()
        >();
        sol::usertype<trussc::Window> t = lua->new_usertype<trussc::Window>("Window", constructors, sol::call_constructor, constructors);
        t["close"] = &trussc::Window::close;
        t["dispatchKeyPressToTree"] = &trussc::Window::dispatchKeyPressToTree;
        t["dispatchKeyReleaseToTree"] = &trussc::Window::dispatchKeyReleaseToTree;
        t["dispatchMousePressToTree"] = &trussc::Window::dispatchMousePressToTree;
        t["dispatchMouseReleaseToTree"] = &trussc::Window::dispatchMouseReleaseToTree;
        t["dispatchMouseScrollToTree"] = &trussc::Window::dispatchMouseScrollToTree;
        t["drawTreeNow"] = &trussc::Window::drawTreeNow;
        t["events"] = &trussc::Window::events;
        t["getApp"] = &trussc::Window::getApp;
        t["getFps"] = &trussc::Window::getFps;
        t["getHeight"] = &trussc::Window::getHeight;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getPosition"] = &trussc::Window::getPosition;
#endif
        t["getTitle"] = &trussc::Window::getTitle;
        t["getWidth"] = &trussc::Window::getWidth;
        t["isFullscreen"] = &trussc::Window::isFullscreen;
        t["isOccluded"] = &trussc::Window::isOccluded;
        t["isOpen"] = &trussc::Window::isOpen;
        t["setApp"] = &trussc::Window::setApp;
        t["setClearColor"] = &trussc::Window::setClearColor;
        t["setFps"] = &trussc::Window::setFps;
        t["setFullscreen"] = &trussc::Window::setFullscreen;
        t["setIndependentFps"] = &trussc::Window::setIndependentFps;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setPosition"] = &trussc::Window::setPosition;
#endif
        t["setSize"] = &trussc::Window::setSize;
        t["setTitle"] = &trussc::Window::setTitle;
        t["syncRootSize"] = &trussc::Window::syncRootSize;
        t["tickTree"] = &trussc::Window::tickTree;
        t["toggleFullscreen"] = &trussc::Window::toggleFullscreen;
    }
    {
        sol::usertype<trussc::AudioStats> t = lua->new_usertype<trussc::AudioStats>("AudioStats");
        t["clippedSamples"] = &trussc::AudioStats::clippedSamples;
        t["cpuUsage"] = &trussc::AudioStats::cpuUsage;
        t["cpuUsagePeak"] = &trussc::AudioStats::cpuUsagePeak;
        t["droppedDecoderError"] = &trussc::AudioStats::droppedDecoderError;
        t["droppedNotRunning"] = &trussc::AudioStats::droppedNotRunning;
        t["droppedPlays"] = &trussc::AudioStats::droppedPlays;
        t["droppedPolyphonyLimit"] = &trussc::AudioStats::droppedPolyphonyLimit;
        t["droppedStreamLimit"] = &trussc::AudioStats::droppedStreamLimit;
        t["initFailure"] = &trussc::AudioStats::initFailure;
        t["initFailureResult"] = &trussc::AudioStats::initFailureResult;
        t["peak"] = &trussc::AudioStats::peak;
        t["rms"] = &trussc::AudioStats::rms;
        t["stalled"] = &trussc::AudioStats::stalled;
        t["underrunFrames"] = &trussc::AudioStats::underrunFrames;
        t["voicesStoppedByReinit"] = &trussc::AudioStats::voicesStoppedByReinit;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLab
            , trussc::ColorOKLab()
            , trussc::ColorOKLab(float, float, float)
            , trussc::ColorOKLab(float, float, float, float)
        >();
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab", constructors, sol::call_constructor, constructors);
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["b"] = &trussc::ColorOKLab::b;
        t["lerp"] = &trussc::ColorOKLab::lerp;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
    }
    {
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isApple"] = &trussc::Platform::isApple;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["name"] = &trussc::Platform::name;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LogEventArgs
            , trussc::LogEventArgs(trussc::LogLevel, const std::string &)
        >();
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs", constructors, sol::call_constructor, constructors);
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::OnceGate
            , trussc::OnceGate()
            , trussc::OnceGate(double)
        >();
        sol::usertype<trussc::OnceGate> t = lua->new_usertype<trussc::OnceGate>("OnceGate", constructors, sol::call_constructor, constructors);
        t["isFirstTime"] = &trussc::OnceGate::isFirstTime;
        t["reset"] = &trussc::OnceGate::reset;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LogStream
            , trussc::LogStream(trussc::LogLevel)
            , trussc::LogStream(trussc::LogLevel, const std::string &)
        >();
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream", constructors, sol::call_constructor, constructors);
    }
    lua->new_usertype<trussc::SoundBuffer::Kind>("SoundBufferKind",
        sol::meta_function::equal_to, [](trussc::SoundBuffer::Kind a, trussc::SoundBuffer::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundBuffer::Kind::Eager),
        "Stream", sol::var(trussc::SoundBuffer::Kind::Stream));
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
