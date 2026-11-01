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
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::VideoWriter
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::VideoWriter()
#endif
        >();
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::VideoWriter::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFps"] = &trussc::VideoWriter::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getHeight"] = &trussc::VideoWriter::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::VideoWriter::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::VideoWriter::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSettings"] = &trussc::VideoWriter::getSettings;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getWidth"] = &trussc::VideoWriter::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isOpen"] = &trussc::VideoWriter::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
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
        auto constructors = tcxLuaConstructors<trussc::StrokeMesh
            , trussc::StrokeMesh()
            , trussc::StrokeMesh(const trussc::Path &)
        >();
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh", constructors, sol::call_constructor, constructors);
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["clear"] = &trussc::StrokeMesh::clear;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["update"] = &trussc::StrokeMesh::update;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLCH
            , trussc::ColorOKLCH()
            , trussc::ColorOKLCH(float, float, float)
            , trussc::ColorOKLCH(float, float, float, float)
        >();
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH", constructors, sol::call_constructor, constructors);
        t["C"] = &trussc::ColorOKLCH::C;
        t["H"] = &trussc::ColorOKLCH::H;
        t["L"] = &trussc::ColorOKLCH::L;
        t["alpha"] = &trussc::ColorOKLCH::alpha;
        t["lerp"] = sol::overload([](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t) { return self.lerp(target, t); }, [](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
        t["toHSB"] = &trussc::ColorOKLCH::toHSB;
        t["toLinear"] = &trussc::ColorOKLCH::toLinear;
        t["toOKLab"] = &trussc::ColorOKLCH::toOKLab;
        t["toRGB"] = &trussc::ColorOKLCH::toRGB;
    }
    lua->new_usertype<trussc::TextureFormat>("TextureFormat",
        sol::meta_function::equal_to, [](trussc::TextureFormat a, trussc::TextureFormat b){ return a == b; },
        "RGBA8", sol::var(trussc::TextureFormat::RGBA8),
        "RGBA16F", sol::var(trussc::TextureFormat::RGBA16F),
        "RGBA32F", sol::var(trussc::TextureFormat::RGBA32F),
        "R8", sol::var(trussc::TextureFormat::R8),
        "R16F", sol::var(trussc::TextureFormat::R16F),
        "R32F", sol::var(trussc::TextureFormat::R32F),
        "RG8", sol::var(trussc::TextureFormat::RG8),
        "RG16F", sol::var(trussc::TextureFormat::RG16F),
        "RG32F", sol::var(trussc::TextureFormat::RG32F),
        "BGRA8", sol::var(trussc::TextureFormat::BGRA8),
        "RGBA16", sol::var(trussc::TextureFormat::RGBA16));
    lua->new_usertype<trussc::LoadError>("LoadError",
        sol::meta_function::equal_to, [](trussc::LoadError a, trussc::LoadError b){ return a == b; },
        "None", sol::var(trussc::LoadError::None),
        "FileNotFound", sol::var(trussc::LoadError::FileNotFound),
        "UnsupportedFormat", sol::var(trussc::LoadError::UnsupportedFormat),
        "DecodeFailed", sol::var(trussc::LoadError::DecodeFailed),
        "Unknown", sol::var(trussc::LoadError::Unknown),
        "TooLarge", sol::var(trussc::LoadError::TooLarge),
        "Cancelled", sol::var(trussc::LoadError::Cancelled));
    {
        sol::usertype<trussc::AudioSettings> t = lua->new_usertype<trussc::AudioSettings>("AudioSettings");
        t["backend"] = &trussc::AudioSettings::backend;
        t["bufferSize"] = &trussc::AudioSettings::bufferSize;
        t["channels"] = &trussc::AudioSettings::channels;
        t["deviceName"] = &trussc::AudioSettings::deviceName;
        t["maxPolyphony"] = &trussc::AudioSettings::maxPolyphony;
        t["sampleRate"] = &trussc::AudioSettings::sampleRate;
    }
    lua->new_usertype<trussc::VideoCodec>("VideoCodec",
        sol::meta_function::equal_to, [](trussc::VideoCodec a, trussc::VideoCodec b){ return a == b; },
        "H264", sol::var(trussc::VideoCodec::H264),
        "HEVC", sol::var(trussc::VideoCodec::HEVC),
        "ProRes422", sol::var(trussc::VideoCodec::ProRes422),
        "ProRes4444", sol::var(trussc::VideoCodec::ProRes4444));
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["changed"] = &trussc::TouchPoint::changed;
        t["id"] = &trussc::TouchPoint::id;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
    }
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
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
