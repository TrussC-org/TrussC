// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_11(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Vec2> t = lua->new_usertype<trussc::Vec2>("Vec2",
            sol::constructors<trussc::Vec2(), trussc::Vec2(float, float), trussc::Vec2(float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec2(), trussc::Vec2(float, float), trussc::Vec2(float)>(),
            sol::meta_function::index, [](const trussc::Vec2& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec2& a){ return -a; },
            sol::meta_function::multiplication, sol::overload([](const trussc::Vec2& a, float b){ return a * b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a * b; }),
            sol::meta_function::division, sol::overload([](const trussc::Vec2& a, float b){ return a / b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a / b; }),
            sol::meta_function::equal_to, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a == b; });
        t["x"] = &trussc::Vec2::x;
        t["y"] = &trussc::Vec2::y;
        t["set"] = sol::overload([](trussc::Vec2& self, float x_, float y_) -> decltype(auto) { return self.set(x_, y_); }, [](trussc::Vec2& self, const trussc::Vec2 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec2::length;
        t["lengthSquared"] = &trussc::Vec2::lengthSquared;
        t["normalized"] = &trussc::Vec2::normalized;
        t["normalize"] = &trussc::Vec2::normalize;
        t["limit"] = &trussc::Vec2::limit;
        t["dot"] = &trussc::Vec2::dot;
        t["cross"] = &trussc::Vec2::cross;
        t["distance"] = &trussc::Vec2::distance;
        t["distanceSquared"] = &trussc::Vec2::distanceSquared;
        t["angle"] = sol::overload([](trussc::Vec2& self) { return self.angle(); }, [](trussc::Vec2& self, const trussc::Vec2 & v) { return self.angle(v); });
        t["rotated"] = &trussc::Vec2::rotated;
        t["rotate"] = &trussc::Vec2::rotate;
        t["lerp"] = &trussc::Vec2::lerp;
        t["perpendicular"] = &trussc::Vec2::perpendicular;
        t["reflected"] = &trussc::Vec2::reflected;
        t["fromAngle"] = sol::overload([](float radians) { return trussc::Vec2::fromAngle(radians); }, [](float radians, float length) { return trussc::Vec2::fromAngle(radians, length); });
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder",
            sol::constructors<trussc::ScreenRecorder()>(),
            sol::call_constructor, sol::constructors<trussc::ScreenRecorder()>());
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const std::string & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const std::string & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const std::string & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const std::string & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const std::string & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const std::string & path, float durationSec) { return self.start(fbo, path, durationSec); });
        t["stop"] = &trussc::ScreenRecorder::stop;
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
        t["getPath"] = &trussc::ScreenRecorder::getPath;
        t["writer"] = &trussc::ScreenRecorder::writer;
    }
#endif
    {
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH",
            sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLCH::L;
        t["C"] = &trussc::ColorOKLCH::C;
        t["H"] = &trussc::ColorOKLCH::H;
        t["alpha"] = &trussc::ColorOKLCH::alpha;
        t["toOKLab"] = &trussc::ColorOKLCH::toOKLab;
        t["toLinear"] = &trussc::ColorOKLCH::toLinear;
        t["toRGB"] = &trussc::ColorOKLCH::toRGB;
        t["toHSB"] = &trussc::ColorOKLCH::toHSB;
        t["lerp"] = sol::overload([](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t) { return self.lerp(target, t); }, [](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    {
        sol::usertype<trussc::Ray> t = lua->new_usertype<trussc::Ray>("Ray",
            sol::constructors<trussc::Ray(), trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)>(),
            sol::call_constructor, sol::constructors<trussc::Ray(), trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)>());
        t["origin"] = &trussc::Ray::origin;
        t["direction"] = &trussc::Ray::direction;
        t["at"] = &trussc::Ray::at;
        t["transformed"] = &trussc::Ray::transformed;
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
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
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
    lua->new_usertype<trussc::TextureWrap>("TextureWrap",
        sol::meta_function::equal_to, [](trussc::TextureWrap a, trussc::TextureWrap b){ return a == b; },
        "Repeat", sol::var(trussc::TextureWrap::Repeat),
        "ClampToEdge", sol::var(trussc::TextureWrap::ClampToEdge),
        "MirroredRepeat", sol::var(trussc::TextureWrap::MirroredRepeat));
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["message"] = &trussc::UdpErrorEventArgs::message;
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
