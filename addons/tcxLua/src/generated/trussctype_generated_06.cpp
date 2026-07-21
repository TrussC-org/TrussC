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
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader",
            sol::constructors<trussc::FileReader()>(),
            sol::call_constructor, sol::constructors<trussc::FileReader()>());
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
        sol::usertype<trussc::AudioDeviceChangedArgs> t = lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
    }
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    {
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["updateFps"] = &trussc::FpsSettings::updateFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["synced"] = &trussc::FpsSettings::synced;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["name"] = &trussc::AudioDeviceInfo::name;
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
