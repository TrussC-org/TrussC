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
        sol::usertype<trussc::MouseMoveEventArgs> t = lua->new_usertype<trussc::MouseMoveEventArgs>("MouseMoveEventArgs");
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["deltaX"] = &trussc::MouseMoveEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseMoveEventArgs::deltaY;
        t["shift"] = &trussc::MouseMoveEventArgs::shift;
        t["ctrl"] = &trussc::MouseMoveEventArgs::ctrl;
        t["alt"] = &trussc::MouseMoveEventArgs::alt;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["pos"] = &trussc::MouseMoveEventArgs::pos;
        t["globalPos"] = &trussc::MouseMoveEventArgs::globalPos;
        t["delta"] = &trussc::MouseMoveEventArgs::delta;
        t["globalDelta"] = &trussc::MouseMoveEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseMoveEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream",
            sol::constructors<trussc::SoundStream()>(),
            sol::call_constructor, sol::constructors<trussc::SoundStream()>());
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const std::string & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const std::string & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
    }
    {
        sol::usertype<trussc::GraphicsBackend> t = lua->new_usertype<trussc::GraphicsBackend>("GraphicsBackend");
        t["isWebGPU"] = &trussc::GraphicsBackend::isWebGPU;
        t["isWebGL2"] = &trussc::GraphicsBackend::isWebGL2;
        t["isMetal"] = &trussc::GraphicsBackend::isMetal;
        t["isD3D11"] = &trussc::GraphicsBackend::isD3D11;
        t["isVulkan"] = &trussc::GraphicsBackend::isVulkan;
        t["isOpenGL"] = &trussc::GraphicsBackend::isOpenGL;
        t["name"] = &trussc::GraphicsBackend::name;
    }
    {
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream",
            sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>(),
            sol::call_constructor, sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>());
    }
    {
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader",
            sol::constructors<trussc::FullscreenShader()>(),
            sol::call_constructor, sol::constructors<trussc::FullscreenShader()>());
        t["draw"] = &trussc::FullscreenShader::draw;
    }
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
