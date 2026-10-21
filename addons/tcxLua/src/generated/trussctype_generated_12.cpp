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
void tcxLuaGenShard_12(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Mesh
            , trussc::Mesh()
        >();
        sol::usertype<trussc::Mesh> t = lua->new_usertype<trussc::Mesh>("Mesh", constructors, sol::call_constructor, constructors);
        t["addColor"] = sol::overload([](trussc::Mesh& self, const trussc::Color & c) -> decltype(auto) { return self.addColor(c); }, [](trussc::Mesh& self, float r, float g, float b) -> decltype(auto) { return self.addColor(r, g, b); }, [](trussc::Mesh& self, float r, float g, float b, float a) -> decltype(auto) { return self.addColor(r, g, b, a); });
        t["addColors"] = &trussc::Mesh::addColors;
        t["addIndex"] = &trussc::Mesh::addIndex;
        t["addIndices"] = &trussc::Mesh::addIndices;
        t["addNormal"] = sol::overload([](trussc::Mesh& self, float nx, float ny, float nz) -> decltype(auto) { return self.addNormal(nx, ny, nz); }, [](trussc::Mesh& self, const trussc::Vec3 & n) -> decltype(auto) { return self.addNormal(n); });
        t["addNormals"] = &trussc::Mesh::addNormals;
        t["addTangent"] = sol::overload([](trussc::Mesh& self, float tx, float ty, float tz) -> decltype(auto) { return self.addTangent(tx, ty, tz); }, [](trussc::Mesh& self, float tx, float ty, float tz, float tw) -> decltype(auto) { return self.addTangent(tx, ty, tz, tw); }, [](trussc::Mesh& self, const trussc::Vec4 & t) -> decltype(auto) { return self.addTangent(t); }, [](trussc::Mesh& self, const trussc::Vec3 & t) -> decltype(auto) { return self.addTangent(t); }, [](trussc::Mesh& self, const trussc::Vec3 & t, float w) -> decltype(auto) { return self.addTangent(t, w); });
        t["addTexCoord"] = sol::overload([](trussc::Mesh& self, float u, float v) -> decltype(auto) { return self.addTexCoord(u, v); }, [](trussc::Mesh& self, const trussc::Vec2 & t) -> decltype(auto) { return self.addTexCoord(t); });
        t["addTriangle"] = &trussc::Mesh::addTriangle;
        t["addVertex"] = sol::overload([](trussc::Mesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::Mesh& self, const trussc::Vec2 & v) -> decltype(auto) { return self.addVertex(v); }, [](trussc::Mesh& self, const trussc::Vec3 & v) -> decltype(auto) { return self.addVertex(v); });
        t["addVertices"] = &trussc::Mesh::addVertices;
        t["append"] = &trussc::Mesh::append;
        t["clear"] = &trussc::Mesh::clear;
        t["clearColors"] = &trussc::Mesh::clearColors;
        t["clearIndices"] = &trussc::Mesh::clearIndices;
        t["clearNormals"] = &trussc::Mesh::clearNormals;
        t["clearTangents"] = &trussc::Mesh::clearTangents;
        t["clearTexCoords"] = &trussc::Mesh::clearTexCoords;
        t["clearVertices"] = &trussc::Mesh::clearVertices;
        t["draw"] = sol::overload([](trussc::Mesh& self) { return self.draw(); }, [](trussc::Mesh& self, const trussc::Texture & texture) { return self.draw(texture); }, [](trussc::Mesh& self, const trussc::Image & image) { return self.draw(image); });
        t["drawGpuPbr"] = &trussc::Mesh::drawGpuPbr;
        t["drawGpuPoints"] = &trussc::Mesh::drawGpuPoints;
        t["drawNoLighting"] = &trussc::Mesh::drawNoLighting;
        t["drawNoLightingWithTexture"] = &trussc::Mesh::drawNoLightingWithTexture;
        t["drawWireframe"] = &trussc::Mesh::drawWireframe;
        t["drawWithLighting"] = &trussc::Mesh::drawWithLighting;
        t["getColors"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getColors(); };
        t["getDataRevision"] = &trussc::Mesh::getDataRevision;
        t["getGpuIndexBuffer"] = &trussc::Mesh::getGpuIndexBuffer;
        t["getGpuIndexCount"] = &trussc::Mesh::getGpuIndexCount;
        t["getGpuPointBuffer"] = &trussc::Mesh::getGpuPointBuffer;
        t["getGpuPointCount"] = &trussc::Mesh::getGpuPointCount;
        t["getGpuVertexBuffer"] = &trussc::Mesh::getGpuVertexBuffer;
        t["getGpuVertexCount"] = &trussc::Mesh::getGpuVertexCount;
        t["getIndices"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getIndices(); };
        t["getMode"] = &trussc::Mesh::getMode;
        t["getNormal"] = &trussc::Mesh::getNormal;
        t["getNormals"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getNormals(); };
        t["getNumColors"] = &trussc::Mesh::getNumColors;
        t["getNumIndices"] = &trussc::Mesh::getNumIndices;
        t["getNumNormals"] = &trussc::Mesh::getNumNormals;
        t["getNumTangents"] = &trussc::Mesh::getNumTangents;
        t["getNumTexCoords"] = &trussc::Mesh::getNumTexCoords;
        t["getNumVertices"] = &trussc::Mesh::getNumVertices;
        t["getTangents"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getTangents(); };
        t["getTexCoords"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getTexCoords(); };
        t["getVertices"] = [](trussc::Mesh& self) -> decltype(auto) { return self.getVertices(); };
        t["hasColors"] = &trussc::Mesh::hasColors;
        t["hasIndices"] = &trussc::Mesh::hasIndices;
        t["hasNormals"] = &trussc::Mesh::hasNormals;
        t["hasTangents"] = &trussc::Mesh::hasTangents;
        t["hasTexCoords"] = &trussc::Mesh::hasTexCoords;
        t["hasValidTexCoords"] = &trussc::Mesh::hasValidTexCoords;
        t["markGpuDirty"] = &trussc::Mesh::markGpuDirty;
        t["rotateX"] = &trussc::Mesh::rotateX;
        t["rotateY"] = &trussc::Mesh::rotateY;
        t["rotateZ"] = &trussc::Mesh::rotateZ;
        t["scale"] = sol::overload([](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.scale(x, y, z); }, [](trussc::Mesh& self, float s) -> decltype(auto) { return self.scale(s); }, [](trussc::Mesh& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scale(s); });
        t["setMode"] = &trussc::Mesh::setMode;
        t["setNormal"] = &trussc::Mesh::setNormal;
        t["transform"] = &trussc::Mesh::transform;
        t["translate"] = sol::overload([](trussc::Mesh& self, float x, float y, float z) -> decltype(auto) { return self.translate(x, y, z); }, [](trussc::Mesh& self, const trussc::Vec3 & offset) -> decltype(auto) { return self.translate(offset); });
        t["uploadPointsToGpu"] = &trussc::Mesh::uploadPointsToGpu;
        t["uploadToGpu"] = &trussc::Mesh::uploadToGpu;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::ScreenRecorder
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::ScreenRecorder()
#endif
        >();
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::ScreenRecorder::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::ScreenRecorder::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stop"] = &trussc::ScreenRecorder::stop;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["writer"] = &trussc::ScreenRecorder::writer;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::Quaternion
            , trussc::Quaternion()
            , trussc::Quaternion(float, float, float, float)
        >();
        sol::usertype<trussc::Quaternion> t = lua->new_usertype<trussc::Quaternion>("Quaternion", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; };
        t[sol::meta_function::equal_to] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; };
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["identity"] = &trussc::Quaternion::identity;
        t["slerp"] = &trussc::Quaternion::slerp;
    }
    {
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::BuildInfo> t = lua->new_usertype<trussc::BuildInfo>("BuildInfo");
        t["date"] = &trussc::BuildInfo::date;
        t["dateTime"] = &trussc::BuildInfo::dateTime;
        t["day"] = &trussc::BuildInfo::day;
        t["hour"] = &trussc::BuildInfo::hour;
        t["minute"] = &trussc::BuildInfo::minute;
        t["month"] = &trussc::BuildInfo::month;
        t["second"] = &trussc::BuildInfo::second;
        t["time"] = &trussc::BuildInfo::time;
        t["timestamp"] = &trussc::BuildInfo::timestamp;
        t["year"] = &trussc::BuildInfo::year;
    }
    {
        sol::usertype<trussc::GraphicsBackend> t = lua->new_usertype<trussc::GraphicsBackend>("GraphicsBackend");
        t["isD3D11"] = &trussc::GraphicsBackend::isD3D11;
        t["isMetal"] = &trussc::GraphicsBackend::isMetal;
        t["isOpenGL"] = &trussc::GraphicsBackend::isOpenGL;
        t["isVulkan"] = &trussc::GraphicsBackend::isVulkan;
        t["isWebGL2"] = &trussc::GraphicsBackend::isWebGL2;
        t["isWebGPU"] = &trussc::GraphicsBackend::isWebGPU;
        t["name"] = &trussc::GraphicsBackend::name;
    }
    lua->new_usertype<trussc::SaveError>("SaveError",
        sol::meta_function::equal_to, [](trussc::SaveError a, trussc::SaveError b){ return a == b; },
        "None", sol::var(trussc::SaveError::None),
        "AccessDenied", sol::var(trussc::SaveError::AccessDenied),
        "NoSpace", sol::var(trussc::SaveError::NoSpace),
        "WriteFailed", sol::var(trussc::SaveError::WriteFailed),
        "Unknown", sol::var(trussc::SaveError::Unknown));
    {
        sol::usertype<trussc::Node::HitResult> t = lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["node"] = &trussc::Node::HitResult::node;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    lua->new_usertype<trussc::ImageType>("ImageType",
        sol::meta_function::equal_to, [](trussc::ImageType a, trussc::ImageType b){ return a == b; },
        "Color", sol::var(trussc::ImageType::Color),
        "Grayscale", sol::var(trussc::ImageType::Grayscale));
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
        t["message"] = &trussc::UdpErrorEventArgs::message;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
