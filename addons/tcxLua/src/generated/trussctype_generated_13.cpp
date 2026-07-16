// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Vec4> t = lua->new_usertype<trussc::Vec4>("Vec4",
            sol::constructors<trussc::Vec4(), trussc::Vec4(float, float, float, float), trussc::Vec4(float), trussc::Vec4(const trussc::Vec3 &), trussc::Vec4(const trussc::Vec3 &, float), trussc::Vec4(const trussc::Vec2 &), trussc::Vec4(const trussc::Vec2 &, float), trussc::Vec4(const trussc::Vec2 &, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec4(), trussc::Vec4(float, float, float, float), trussc::Vec4(float), trussc::Vec4(const trussc::Vec3 &), trussc::Vec4(const trussc::Vec3 &, float), trussc::Vec4(const trussc::Vec2 &), trussc::Vec4(const trussc::Vec2 &, float), trussc::Vec4(const trussc::Vec2 &, float, float)>(),
            sol::meta_function::index, [](const trussc::Vec4& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec4& a){ return -a; },
            sol::meta_function::multiplication, [](const trussc::Vec4& a, float b){ return a * b; },
            sol::meta_function::division, [](const trussc::Vec4& a, float b){ return a / b; },
            sol::meta_function::equal_to, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a == b; });
        t["x"] = &trussc::Vec4::x;
        t["y"] = &trussc::Vec4::y;
        t["z"] = &trussc::Vec4::z;
        t["w"] = &trussc::Vec4::w;
        t["set"] = sol::overload([](trussc::Vec4& self, float x_, float y_, float z_, float w_) -> decltype(auto) { return self.set(x_, y_, z_, w_); }, [](trussc::Vec4& self, const trussc::Vec4 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec4::length;
        t["lengthSquared"] = &trussc::Vec4::lengthSquared;
        t["normalized"] = &trussc::Vec4::normalized;
        t["normalize"] = &trussc::Vec4::normalize;
        t["dot"] = &trussc::Vec4::dot;
        t["lerp"] = &trussc::Vec4::lerp;
        t["xy"] = &trussc::Vec4::xy;
        t["xyz"] = &trussc::Vec4::xyz;
    }
    {
        sol::usertype<trussc::ColorLinear> t = lua->new_usertype<trussc::ColorLinear>("ColorLinear",
            sol::constructors<trussc::ColorLinear(), trussc::ColorLinear(float, float, float), trussc::ColorLinear(float, float, float, float), trussc::ColorLinear(float), trussc::ColorLinear(float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorLinear(), trussc::ColorLinear(float, float, float), trussc::ColorLinear(float, float, float, float), trussc::ColorLinear(float), trussc::ColorLinear(float, float)>(),
            sol::meta_function::addition, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a - b; },
            sol::meta_function::multiplication, sol::overload([](const trussc::ColorLinear& a, float b){ return a * b; }, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a * b; }),
            sol::meta_function::division, [](const trussc::ColorLinear& a, float b){ return a / b; },
            sol::meta_function::equal_to, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a == b; });
        t["r"] = &trussc::ColorLinear::r;
        t["g"] = &trussc::ColorLinear::g;
        t["b"] = &trussc::ColorLinear::b;
        t["a"] = &trussc::ColorLinear::a;
        t["toSRGB"] = &trussc::ColorLinear::toSRGB;
        t["toHSB"] = &trussc::ColorLinear::toHSB;
        t["toOKLab"] = &trussc::ColorLinear::toOKLab;
        t["toOKLCH"] = &trussc::ColorLinear::toOKLCH;
        t["clamped"] = &trussc::ColorLinear::clamped;
        t["clampedLDR"] = &trussc::ColorLinear::clampedLDR;
        t["lerp"] = &trussc::ColorLinear::lerp;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    {
        sol::usertype<trussc::Window> t = lua->new_usertype<trussc::Window>("Window",
            sol::constructors<trussc::Window()>(),
            sol::call_constructor, sol::constructors<trussc::Window()>());
        t["setApp"] = &trussc::Window::setApp;
        t["getApp"] = &trussc::Window::getApp;
        t["events"] = &trussc::Window::events;
        t["close"] = &trussc::Window::close;
        t["isOpen"] = &trussc::Window::isOpen;
        t["setTitle"] = &trussc::Window::setTitle;
        t["getWidth"] = &trussc::Window::getWidth;
        t["getHeight"] = &trussc::Window::getHeight;
        t["setClearColor"] = &trussc::Window::setClearColor;
        t["dispatchMousePressToTree"] = &trussc::Window::dispatchMousePressToTree;
        t["dispatchMouseReleaseToTree"] = &trussc::Window::dispatchMouseReleaseToTree;
        t["tickTree"] = &trussc::Window::tickTree;
        t["drawTreeNow"] = &trussc::Window::drawTreeNow;
        t["syncRootSize"] = &trussc::Window::syncRootSize;
    }
#endif
    {
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger",
            sol::constructors<trussc::Logger()>(),
            sol::call_constructor, sol::constructors<trussc::Logger()>());
        t["onLog"] = &trussc::Logger::onLog;
        t["log"] = &trussc::Logger::log;
        t["setConsoleLogLevel"] = &trussc::Logger::setConsoleLogLevel;
        t["getConsoleLogLevel"] = &trussc::Logger::getConsoleLogLevel;
        t["setLogFile"] = &trussc::Logger::setLogFile;
        t["closeFile"] = &trussc::Logger::closeFile;
        t["setFileLogLevel"] = &trussc::Logger::setFileLogLevel;
        t["getFileLogLevel"] = &trussc::Logger::getFileLogLevel;
        t["getLogFilePath"] = &trussc::Logger::getLogFilePath;
        t["isFileOpen"] = &trussc::Logger::isFileOpen;
    }
    {
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["volume"] = &trussc::PlayingSound::volume;
        t["pan"] = &trussc::PlayingSound::pan;
        t["speed"] = &trussc::PlayingSound::speed;
        t["loop"] = &trussc::PlayingSound::loop;
        t["playing"] = &trussc::PlayingSound::playing;
        t["paused"] = &trussc::PlayingSound::paused;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
    }
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
    }
    lua->new_usertype<trussc::MouseButton>("MouseButton",
        sol::meta_function::equal_to, [](trussc::MouseButton a, trussc::MouseButton b){ return a == b; },
        "Left", sol::var(trussc::MouseButton::Left),
        "Right", sol::var(trussc::MouseButton::Right),
        "Middle", sol::var(trussc::MouseButton::Middle),
        "None", sol::var(trussc::MouseButton::None));
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::ConsoleEventArgs> t = lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        t["raw"] = &trussc::ConsoleEventArgs::raw;
        t["args"] = &trussc::ConsoleEventArgs::args;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
