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
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_12(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Mesh
            , trussc::Mesh()
        >();
        sol::usertype<trussc::Mesh> t = lua->new_usertype<trussc::Mesh>("Mesh", constructors, sol::call_constructor, constructors);
        t["setMode"] = &trussc::Mesh::setMode;
        t["getMode"] = &trussc::Mesh::getMode;
        t["addVertex"] = sol::overload([](trussc::Mesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::Mesh& self, const trussc::Vec2 & v) -> decltype(auto) { return self.addVertex(v); }, [](trussc::Mesh& self, const trussc::Vec3 & v) -> decltype(auto) { return self.addVertex(v); });
        t["addVertices"] = &trussc::Mesh::addVertices;
        t["getVertices"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getVertices(); };
        t["getNumVertices"] = &trussc::Mesh::getNumVertices;
        t["addColor"] = sol::overload([](trussc::Mesh& self, const trussc::Color & c) -> decltype(auto) { return self.addColor(c); }, [](trussc::Mesh& self, float r, float g, float b) -> decltype(auto) { return self.addColor(r, g, b); }, [](trussc::Mesh& self, float r, float g, float b, float a) -> decltype(auto) { return self.addColor(r, g, b, a); });
        t["addColors"] = &trussc::Mesh::addColors;
        t["getColors"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getColors(); };
        t["getNumColors"] = &trussc::Mesh::getNumColors;
        t["hasColors"] = &trussc::Mesh::hasColors;
        t["addIndex"] = &trussc::Mesh::addIndex;
        t["addIndices"] = &trussc::Mesh::addIndices;
        t["addTriangle"] = &trussc::Mesh::addTriangle;
        t["getIndices"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getIndices(); };
        t["getNumIndices"] = &trussc::Mesh::getNumIndices;
        t["hasIndices"] = &trussc::Mesh::hasIndices;
        t["addNormal"] = sol::overload([](trussc::Mesh& self, float nx, float ny, float nz) -> decltype(auto) { return self.addNormal(nx, ny, nz); }, [](trussc::Mesh& self, const trussc::Vec3 & n) -> decltype(auto) { return self.addNormal(n); });
        t["addNormals"] = &trussc::Mesh::addNormals;
        t["setNormal"] = &trussc::Mesh::setNormal;
        t["getNormal"] = &trussc::Mesh::getNormal;
        t["getNormals"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getNormals(); };
        t["getNumNormals"] = &trussc::Mesh::getNumNormals;
        t["hasNormals"] = &trussc::Mesh::hasNormals;
        t["addTexCoord"] = sol::overload([](trussc::Mesh& self, float u, float v) -> decltype(auto) { return self.addTexCoord(u, v); }, [](trussc::Mesh& self, const trussc::Vec2 & t) -> decltype(auto) { return self.addTexCoord(t); });
        t["getTexCoords"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getTexCoords(); };
        t["getNumTexCoords"] = &trussc::Mesh::getNumTexCoords;
        t["hasTexCoords"] = &trussc::Mesh::hasTexCoords;
        t["hasValidTexCoords"] = &trussc::Mesh::hasValidTexCoords;
        t["addTangent"] = sol::overload([](trussc::Mesh& self, float tx, float ty, float tz) -> decltype(auto) { return self.addTangent(tx, ty, tz); }, [](trussc::Mesh& self, float tx, float ty, float tz, float tw) -> decltype(auto) { return self.addTangent(tx, ty, tz, tw); }, [](trussc::Mesh& self, const trussc::Vec4 & t) -> decltype(auto) { return self.addTangent(t); }, [](trussc::Mesh& self, const trussc::Vec3 & t) -> decltype(auto) { return self.addTangent(t); }, [](trussc::Mesh& self, const trussc::Vec3 & t, float w) -> decltype(auto) { return self.addTangent(t, w); });
        t["getTangents"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getTangents(); };
        t["getNumTangents"] = &trussc::Mesh::getNumTangents;
        t["hasTangents"] = &trussc::Mesh::hasTangents;
        t["clear"] = &trussc::Mesh::clear;
        t["clearVertices"] = &trussc::Mesh::clearVertices;
        t["clearNormals"] = &trussc::Mesh::clearNormals;
        t["clearColors"] = &trussc::Mesh::clearColors;
        t["clearIndices"] = &trussc::Mesh::clearIndices;
        t["clearTexCoords"] = &trussc::Mesh::clearTexCoords;
        t["clearTangents"] = &trussc::Mesh::clearTangents;
        t["translate"] = sol::overload([](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.translate(x, y, z); }, [](trussc::Mesh& self, const trussc::Vec3 & offset) -> decltype(auto) { return self.translate(offset); });
        t["rotateX"] = &trussc::Mesh::rotateX;
        t["rotateY"] = &trussc::Mesh::rotateY;
        t["rotateZ"] = &trussc::Mesh::rotateZ;
        t["scale"] = sol::overload([](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.scale(x, y, z); }, [](trussc::Mesh& self, float s) -> decltype(auto) { return self.scale(s); }, [](trussc::Mesh& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scale(s); });
        t["transform"] = &trussc::Mesh::transform;
        t["append"] = &trussc::Mesh::append;
        t["draw"] = sol::overload([](trussc::Mesh& self) { return self.draw(); }, [](trussc::Mesh& self, const trussc::Texture & texture) { return self.draw(texture); }, [](trussc::Mesh& self, const trussc::Image & image) { return self.draw(image); });
        t["drawNoLighting"] = &trussc::Mesh::drawNoLighting;
        t["drawWithLighting"] = &trussc::Mesh::drawWithLighting;
        t["drawNoLightingWithTexture"] = &trussc::Mesh::drawNoLightingWithTexture;
        t["drawWireframe"] = &trussc::Mesh::drawWireframe;
        t["markGpuDirty"] = &trussc::Mesh::markGpuDirty;
        t["getDataRevision"] = &trussc::Mesh::getDataRevision;
        t["uploadToGpu"] = &trussc::Mesh::uploadToGpu;
        t["drawGpuPbr"] = &trussc::Mesh::drawGpuPbr;
        t["drawGpuPoints"] = &trussc::Mesh::drawGpuPoints;
        t["uploadPointsToGpu"] = &trussc::Mesh::uploadPointsToGpu;
        t["getGpuVertexBuffer"] = &trussc::Mesh::getGpuVertexBuffer;
        t["getGpuIndexBuffer"] = &trussc::Mesh::getGpuIndexBuffer;
        t["getGpuVertexCount"] = &trussc::Mesh::getGpuVertexCount;
        t["getGpuIndexCount"] = &trussc::Mesh::getGpuIndexCount;
        t["getGpuPointBuffer"] = &trussc::Mesh::getGpuPointBuffer;
        t["getGpuPointCount"] = &trussc::Mesh::getGpuPointCount;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Color
            , trussc::Color()
            , trussc::Color(float, float, float)
            , trussc::Color(float, float, float, float)
            , trussc::Color(float)
            , trussc::Color(float, float)
        >();
        sol::usertype<trussc::Color> t = lua->new_usertype<trussc::Color>("Color", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::addition] = [](const trussc::Color& a, const trussc::Color & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Color& a, const trussc::Color & b){ return a - b; };
        t[sol::meta_function::multiplication] = [](const trussc::Color& a, float b){ return a * b; };
        t[sol::meta_function::division] = [](const trussc::Color& a, float b){ return a / b; };
        t[sol::meta_function::equal_to] = [](const trussc::Color& a, const trussc::Color & b){ return a == b; };
        t["r"] = &trussc::Color::r;
        t["g"] = &trussc::Color::g;
        t["b"] = &trussc::Color::b;
        t["a"] = &trussc::Color::a;
        t["set"] = sol::overload([](trussc::Color& self, float r_, float g_, float b_) -> decltype(auto) { return self.set(r_, g_, b_); }, [](trussc::Color& self, float r_, float g_, float b_, float a_) -> decltype(auto) { return self.set(r_, g_, b_, a_); }, [](trussc::Color& self, float gray) -> decltype(auto) { return self.set(gray); }, [](trussc::Color& self, float gray, float a_) -> decltype(auto) { return self.set(gray, a_); }, [](trussc::Color& self, const trussc::Color & c) -> decltype(auto) { return self.set(c); });
        t["toHex"] = sol::overload([](trussc::Color& self) { return self.toHex(); }, [](trussc::Color& self, bool includeAlpha) { return self.toHex(includeAlpha); });
        t["toLinear"] = &trussc::Color::toLinear;
        t["toHSB"] = &trussc::Color::toHSB;
        t["toOKLab"] = &trussc::Color::toOKLab;
        t["toOKLCH"] = &trussc::Color::toOKLCH;
        t["clamped"] = &trussc::Color::clamped;
        t["lerpRGB"] = &trussc::Color::lerpRGB;
        t["lerpLinear"] = &trussc::Color::lerpLinear;
        t["lerpHSB"] = &trussc::Color::lerpHSB;
        t["lerpOKLab"] = &trussc::Color::lerpOKLab;
        t["lerpOKLCH"] = &trussc::Color::lerpOKLCH;
        t["lerp"] = &trussc::Color::lerp;
        t["fromBytes"] = sol::overload([](int r, int g, int b) { return trussc::Color::fromBytes(r, g, b); }, [](int r, int g, int b, int a) { return trussc::Color::fromBytes(r, g, b, a); });
        t["fromHex"] = sol::overload([](uint32_t hex) { return trussc::Color::fromHex(hex); }, [](uint32_t hex, bool hasAlpha) { return trussc::Color::fromHex(hex, hasAlpha); });
        t["fromHSB"] = sol::overload([](float h, float s, float b) { return trussc::Color::fromHSB(h, s, b); }, [](float h, float s, float b, float a) { return trussc::Color::fromHSB(h, s, b, a); });
        t["fromOKLCH"] = sol::overload([](float L, float C, float H) { return trussc::Color::fromOKLCH(L, C, H); }, [](float L, float C, float H, float a) { return trussc::Color::fromOKLCH(L, C, H, a); });
        t["fromOKLab"] = sol::overload([](float L, float a_lab, float b_lab) { return trussc::Color::fromOKLab(L, a_lab, b_lab); }, [](float L, float a_lab, float b_lab, float alpha) { return trussc::Color::fromOKLab(L, a_lab, b_lab, alpha); });
        t["fromLinear"] = sol::overload([](float r, float g, float b) { return trussc::Color::fromLinear(r, g, b); }, [](float r, float g, float b, float a) { return trussc::Color::fromLinear(r, g, b, a); });
    }
    {
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["width"] = &trussc::WindowSettings::width;
        t["height"] = &trussc::WindowSettings::height;
        t["title"] = &trussc::WindowSettings::title;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["clipboardSize"] = &trussc::WindowSettings::clipboardSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["uniformBufferReserve"] = &trussc::WindowSettings::uniformBufferReserve;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setClipboardSize"] = &trussc::WindowSettings::setClipboardSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["reserveUniformBuffer"] = &trussc::WindowSettings::reserveUniformBuffer;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorHSB
            , trussc::ColorHSB()
            , trussc::ColorHSB(float, float, float)
            , trussc::ColorHSB(float, float, float, float)
        >();
        sol::usertype<trussc::ColorHSB> t = lua->new_usertype<trussc::ColorHSB>("ColorHSB", constructors, sol::call_constructor, constructors);
        t["h"] = &trussc::ColorHSB::h;
        t["s"] = &trussc::ColorHSB::s;
        t["b"] = &trussc::ColorHSB::b;
        t["a"] = &trussc::ColorHSB::a;
        t["toRGB"] = &trussc::ColorHSB::toRGB;
        t["toLinear"] = &trussc::ColorHSB::toLinear;
        t["toOKLab"] = &trussc::ColorHSB::toOKLab;
        t["toOKLCH"] = &trussc::ColorHSB::toOKLCH;
        t["lerp"] = sol::overload([](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t) { return self.lerp(target, t); }, [](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    {
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
        t["duration"] = &trussc::VideoRecordSettings::duration;
        t["audio"] = &trussc::VideoRecordSettings::audio;
        t["audioBitrate"] = &trussc::VideoRecordSettings::audioBitrate;
        t["audioSampleRate"] = &trussc::VideoRecordSettings::audioSampleRate;
        t["audioChannels"] = &trussc::VideoRecordSettings::audioChannels;
    }
    {
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["r"] = &trussc::ShaderVertex::r;
        t["g"] = &trussc::ShaderVertex::g;
        t["b"] = &trussc::ShaderVertex::b;
        t["a"] = &trussc::ShaderVertex::a;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    {
        sol::usertype<trussc::TcpClientConnectEventArgs> t = lua->new_usertype<trussc::TcpClientConnectEventArgs>("TcpClientConnectEventArgs");
        t["clientId"] = &trussc::TcpClientConnectEventArgs::clientId;
        t["host"] = &trussc::TcpClientConnectEventArgs::host;
        t["port"] = &trussc::TcpClientConnectEventArgs::port;
    }
    lua->new_usertype<trussc::CurveStyle::Mode>("CurveStyleMode",
        sol::meta_function::equal_to, [](trussc::CurveStyle::Mode a, trussc::CurveStyle::Mode b){ return a == b; },
        "Tolerance", sol::var(trussc::CurveStyle::Mode::Tolerance),
        "Resolution", sol::var(trussc::CurveStyle::Mode::Resolution));
    {
        sol::usertype<trussc::VideoErrorEventArgs> t = lua->new_usertype<trussc::VideoErrorEventArgs>("VideoErrorEventArgs");
        t["message"] = &trussc::VideoErrorEventArgs::message;
        t["errorCode"] = &trussc::VideoErrorEventArgs::errorCode;
    }
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["message"] = &trussc::UdpErrorEventArgs::message;
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
