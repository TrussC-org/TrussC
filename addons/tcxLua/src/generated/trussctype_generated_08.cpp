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
        auto constructors = tcxLuaConstructors<trussc::IVec3
            , trussc::IVec3()
            , trussc::IVec3(int, int, int)
            , trussc::IVec3(int)
            , trussc::IVec3(const trussc::IVec2 &)
            , trussc::IVec3(const trussc::IVec2 &, int)
        >();
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec3& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec3& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; };
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
    }
    {
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["address"] = &trussc::NetworkInterface::address;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["name"] = &trussc::NetworkInterface::name;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
    }
    {
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["audio"] = &trussc::VideoRecordSettings::audio;
        t["audioBitrate"] = &trussc::VideoRecordSettings::audioBitrate;
        t["audioChannels"] = &trussc::VideoRecordSettings::audioChannels;
        t["audioSampleRate"] = &trussc::VideoRecordSettings::audioSampleRate;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["duration"] = &trussc::VideoRecordSettings::duration;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["pickable"] = &trussc::CameraContext::pickable;
        t["projection"] = &trussc::CameraContext::projection;
        t["view"] = &trussc::CameraContext::view;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    {
        sol::usertype<trussc::BlendDesc> t = lua->new_usertype<trussc::BlendDesc>("BlendDesc");
        t["alpha"] = &trussc::BlendDesc::alpha;
        t["premultiplySource"] = &trussc::BlendDesc::premultiplySource;
        t["rgb"] = &trussc::BlendDesc::rgb;
        t["addKeepAlpha"] = &trussc::BlendDesc::addKeepAlpha;
        t["premultipliedAdd"] = &trussc::BlendDesc::premultipliedAdd;
        t["screenKeepAlpha"] = &trussc::BlendDesc::screenKeepAlpha;
    }
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
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
