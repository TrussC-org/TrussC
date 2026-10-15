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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Mesh>::user_metatable()];
        sol::usertype<trussc::Mesh> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Mesh>>()
            : lua->new_usertype<trussc::Mesh>("Mesh", constructors, sol::call_constructor, constructors);
        (*lua)["Mesh"] = t;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Color>::user_metatable()];
        sol::usertype<trussc::Color> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Color>>()
            : lua->new_usertype<trussc::Color>("Color", constructors, sol::call_constructor, constructors);
        (*lua)["Color"] = t;
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
    lua->new_usertype<trussc::Cursor>("Cursor",
        sol::meta_function::equal_to, [](trussc::Cursor a, trussc::Cursor b){ return a == b; },
        "Default", sol::var(trussc::Cursor::Default),
        "Arrow", sol::var(trussc::Cursor::Arrow),
        "IBeam", sol::var(trussc::Cursor::IBeam),
        "Crosshair", sol::var(trussc::Cursor::Crosshair),
        "Hand", sol::var(trussc::Cursor::Hand),
        "ResizeEW", sol::var(trussc::Cursor::ResizeEW),
        "ResizeNS", sol::var(trussc::Cursor::ResizeNS),
        "ResizeNWSE", sol::var(trussc::Cursor::ResizeNWSE),
        "ResizeNESW", sol::var(trussc::Cursor::ResizeNESW),
        "ResizeAll", sol::var(trussc::Cursor::ResizeAll),
        "NotAllowed", sol::var(trussc::Cursor::NotAllowed),
        "Custom0", sol::var(trussc::Cursor::Custom0),
        "Custom1", sol::var(trussc::Cursor::Custom1),
        "Custom2", sol::var(trussc::Cursor::Custom2),
        "Custom3", sol::var(trussc::Cursor::Custom3),
        "Custom4", sol::var(trussc::Cursor::Custom4),
        "Custom5", sol::var(trussc::Cursor::Custom5),
        "Custom6", sol::var(trussc::Cursor::Custom6),
        "Custom7", sol::var(trussc::Cursor::Custom7),
        "Custom8", sol::var(trussc::Cursor::Custom8),
        "Custom9", sol::var(trussc::Cursor::Custom9),
        "Custom10", sol::var(trussc::Cursor::Custom10),
        "Custom11", sol::var(trussc::Cursor::Custom11),
        "Custom12", sol::var(trussc::Cursor::Custom12),
        "Custom13", sol::var(trussc::Cursor::Custom13),
        "Custom14", sol::var(trussc::Cursor::Custom14),
        "Custom15", sol::var(trussc::Cursor::Custom15));
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::MouseDragEventArgs>::user_metatable()];
        sol::usertype<trussc::MouseDragEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::MouseDragEventArgs>>()
            : lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        (*lua)["MouseDragEventArgs"] = t;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::FileReader
            , trussc::FileReader()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::FileReader>::user_metatable()];
        sol::usertype<trussc::FileReader> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::FileReader>>()
            : lua->new_usertype<trussc::FileReader>("FileReader", constructors, sol::call_constructor, constructors);
        (*lua)["FileReader"] = t;
        t["open"] = &trussc::FileReader::open;
        t["close"] = &trussc::FileReader::close;
        t["isOpen"] = &trussc::FileReader::isOpen;
        t["eof"] = &trussc::FileReader::eof;
        t["readLine"] = [](trussc::FileReader& self) { return self.readLine(); };
        t["readChar"] = &trussc::FileReader::readChar;
        t["seek"] = &trussc::FileReader::seek;
        t["tell"] = &trussc::FileReader::tell;
        t["remaining"] = &trussc::FileReader::remaining;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::EventListener
            , trussc::EventListener()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::EventListener>::user_metatable()];
        sol::usertype<trussc::EventListener> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::EventListener>>()
            : lua->new_usertype<trussc::EventListener>("EventListener", constructors, sol::call_constructor, constructors);
        (*lua)["EventListener"] = t;
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
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
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpErrorEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpErrorEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpErrorEventArgs>>()
            : lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        (*lua)["TcpErrorEventArgs"] = t;
        t["message"] = &trussc::TcpErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ConsoleEventArgs>::user_metatable()];
        sol::usertype<trussc::ConsoleEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ConsoleEventArgs>>()
            : lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        (*lua)["ConsoleEventArgs"] = t;
        t["raw"] = &trussc::ConsoleEventArgs::raw;
        t["args"] = &trussc::ConsoleEventArgs::args;
    }
    lua->new_usertype<trussc::TextureWrap>("TextureWrap",
        sol::meta_function::equal_to, [](trussc::TextureWrap a, trussc::TextureWrap b){ return a == b; },
        "Repeat", sol::var(trussc::TextureWrap::Repeat),
        "ClampToEdge", sol::var(trussc::TextureWrap::ClampToEdge),
        "MirroredRepeat", sol::var(trussc::TextureWrap::MirroredRepeat));
    lua->new_usertype<trussc::ScrollBar::Direction>("ScrollBarDirection",
        sol::meta_function::equal_to, [](trussc::ScrollBar::Direction a, trussc::ScrollBar::Direction b){ return a == b; },
        "Vertical", sol::var(trussc::ScrollBar::Direction::Vertical),
        "Horizontal", sol::var(trussc::ScrollBar::Direction::Horizontal));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
