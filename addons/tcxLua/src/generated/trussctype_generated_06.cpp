// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_06(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
    {
        sol::usertype<trussc::VideoPlayer> t = lua->new_usertype<trussc::VideoPlayer>("VideoPlayer",
            sol::constructors<trussc::VideoPlayer()>(),
            sol::call_constructor, sol::constructors<trussc::VideoPlayer()>());
        t["load"] = &trussc::VideoPlayer::load;
        t["close"] = &trussc::VideoPlayer::close;
        t["update"] = &trussc::VideoPlayer::update;
        t["play"] = &trussc::VideoPlayer::play;
        t["setAutoPoster"] = &trussc::VideoPlayer::setAutoPoster;
        t["getAutoPoster"] = &trussc::VideoPlayer::getAutoPoster;
        t["draw"] = sol::overload([](trussc::VideoPlayer& self, float x, float y) { return self.draw(x, y); }, [](trussc::VideoPlayer& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["getDuration"] = &trussc::VideoPlayer::getDuration;
        t["getPosition"] = &trussc::VideoPlayer::getPosition;
        t["getCurrentFrame"] = &trussc::VideoPlayer::getCurrentFrame;
        t["getTotalFrames"] = &trussc::VideoPlayer::getTotalFrames;
        t["setFrame"] = &trussc::VideoPlayer::setFrame;
        t["nextFrame"] = &trussc::VideoPlayer::nextFrame;
        t["previousFrame"] = &trussc::VideoPlayer::previousFrame;
        t["setGammaCorrection"] = &trussc::VideoPlayer::setGammaCorrection;
        t["getGammaCorrection"] = &trussc::VideoPlayer::getGammaCorrection;
        t["setUseHwAccel"] = &trussc::VideoPlayer::setUseHwAccel;
        t["getUseHwAccel"] = &trussc::VideoPlayer::getUseHwAccel;
        t["getPixels"] = [](trussc::VideoPlayer& self) { return self.getPixels(); };
        t["getPixelsY"] = &trussc::VideoPlayer::getPixelsY;
        t["getPixelsUV"] = &trussc::VideoPlayer::getPixelsUV;
        t["hasAudio"] = &trussc::VideoPlayer::hasAudio;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getAudioCodec"] = &trussc::VideoPlayer::getAudioCodec;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getAudioData"] = &trussc::VideoPlayer::getAudioData;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getAudioSampleRate"] = &trussc::VideoPlayer::getAudioSampleRate;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getAudioChannels"] = &trussc::VideoPlayer::getAudioChannels;
#endif
        t["isUsingHwAccel"] = &trussc::VideoPlayer::isUsingHwAccel;
        t["getHwAccelName"] = &trussc::VideoPlayer::getHwAccelName;
        t["getPath"] = &trussc::VideoPlayer::getPath;
    }
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        sol::usertype<trussc::Serial> t = lua->new_usertype<trussc::Serial>("Serial",
            sol::constructors<trussc::Serial()>(),
            sol::call_constructor, sol::constructors<trussc::Serial()>());
        t["getDeviceList"] = &trussc::Serial::getDeviceList;
        t["setup"] = sol::overload([](trussc::Serial& self, const std::string & portName, int baudRate) { return self.setup(portName, baudRate); }, [](trussc::Serial& self, int deviceIndex, int baudRate) { return self.setup(deviceIndex, baudRate); });
        t["close"] = &trussc::Serial::close;
        t["isInitialized"] = &trussc::Serial::isInitialized;
        t["getDevicePath"] = &trussc::Serial::getDevicePath;
        t["available"] = &trussc::Serial::available;
        t["readByte"] = &trussc::Serial::readByte;
        t["writeBytes"] = [](trussc::Serial& self, const std::string & buffer) { return self.writeBytes(buffer); };
        t["writeByte"] = &trussc::Serial::writeByte;
        t["flushInput"] = &trussc::Serial::flushInput;
        t["flushOutput"] = &trussc::Serial::flushOutput;
        t["flush"] = &trussc::Serial::flush;
        t["drain"] = &trussc::Serial::drain;
        t["printDevices"] = &trussc::Serial::printDevices;
        t["listDevices"] = &trussc::Serial::listDevices;
    }
#endif
    {
        sol::usertype<trussc::IVec2> t = lua->new_usertype<trussc::IVec2>("IVec2",
            sol::constructors<trussc::IVec2(), trussc::IVec2(int, int), trussc::IVec2(int)>(),
            sol::call_constructor, sol::constructors<trussc::IVec2(), trussc::IVec2(int, int), trussc::IVec2(int)>(),
            sol::meta_function::addition, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::IVec2& a){ return -a; },
            sol::meta_function::multiplication, [](const trussc::IVec2& a, int b){ return a * b; },
            sol::meta_function::equal_to, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; });
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
    }
    {
        sol::usertype<trussc::MouseEventArgs> t = lua->new_usertype<trussc::MouseEventArgs>("MouseEventArgs");
        t["x"] = &trussc::MouseEventArgs::x;
        t["y"] = &trussc::MouseEventArgs::y;
        t["button"] = &trussc::MouseEventArgs::button;
        t["shift"] = &trussc::MouseEventArgs::shift;
        t["ctrl"] = &trussc::MouseEventArgs::ctrl;
        t["alt"] = &trussc::MouseEventArgs::alt;
        t["super"] = &trussc::MouseEventArgs::super;
        t["pos"] = &trussc::MouseEventArgs::pos;
        t["globalPos"] = &trussc::MouseEventArgs::globalPos;
        t["consumed"] = &trussc::MouseEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["view"] = &trussc::CameraContext::view;
        t["projection"] = &trussc::CameraContext::projection;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["pickable"] = &trussc::CameraContext::pickable;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    lua->new_usertype<trussc::ThermalState>("ThermalState",
        sol::meta_function::equal_to, [](trussc::ThermalState a, trussc::ThermalState b){ return a == b; },
        "Nominal", sol::var(trussc::ThermalState::Nominal),
        "Fair", sol::var(trussc::ThermalState::Fair),
        "Serious", sol::var(trussc::ThermalState::Serious),
        "Critical", sol::var(trussc::ThermalState::Critical));
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["id"] = &trussc::TouchPoint::id;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["changed"] = &trussc::TouchPoint::changed;
    }
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
    {
        sol::usertype<trussc::GrabberFrame> t = lua->new_usertype<trussc::GrabberFrame>("GrabberFrame");
        t["pixels"] = &trussc::GrabberFrame::pixels;
        t["timestampUs"] = &trussc::GrabberFrame::timestampUs;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
