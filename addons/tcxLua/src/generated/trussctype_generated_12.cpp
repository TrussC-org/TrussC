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
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::AudioRecordSettings::SampleFormat>("AudioRecordSettingsSampleFormat",
        sol::meta_function::equal_to, [](trussc::AudioRecordSettings::SampleFormat a, trussc::AudioRecordSettings::SampleFormat b){ return a == b; },
        "S16", sol::var(trussc::AudioRecordSettings::SampleFormat::S16),
        "F32", sol::var(trussc::AudioRecordSettings::SampleFormat::F32));
    lua->new_usertype<trussc::MouseButton>("MouseButton",
        sol::meta_function::equal_to, [](trussc::MouseButton a, trussc::MouseButton b){ return a == b; },
        "Left", sol::var(trussc::MouseButton::Left),
        "Right", sol::var(trussc::MouseButton::Right),
        "Middle", sol::var(trussc::MouseButton::Middle),
        "None", sol::var(trussc::MouseButton::None));
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::DeviceLostEventArgs> t = lua->new_usertype<trussc::DeviceLostEventArgs>("DeviceLostEventArgs");
        t["cancel"] = &trussc::DeviceLostEventArgs::cancel;
        t["reason"] = &trussc::DeviceLostEventArgs::reason;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
