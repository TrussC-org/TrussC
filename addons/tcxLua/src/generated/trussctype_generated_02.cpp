// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_02(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Mesh> t = lua->new_usertype<trussc::Mesh>("Mesh",
            sol::constructors<trussc::Mesh()>(),
            sol::call_constructor, sol::constructors<trussc::Mesh()>());
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
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isApple"] = &trussc::Platform::isApple;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["name"] = &trussc::Platform::name;
    }
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
    }
    lua->new_usertype<trussc::TcyMode>("TcyMode",
        sol::meta_function::equal_to, [](trussc::TcyMode a, trussc::TcyMode b){ return a == b; },
        "Rotate", sol::var(trussc::TcyMode::Rotate),
        "Upright", sol::var(trussc::TcyMode::Upright),
        "Combine", sol::var(trussc::TcyMode::Combine));
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
