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
        t["onDisconnect"] = &trussc::Serial::onDisconnect;
        t["getDeviceList"] = &trussc::Serial::getDeviceList;
        t["setup"] = sol::overload([](trussc::Serial& self, const std::string & portName, int baudRate) { return self.setup(portName, baudRate); }, [](trussc::Serial& self, int deviceIndex, int baudRate) { return self.setup(deviceIndex, baudRate); });
        t["close"] = &trussc::Serial::close;
        t["isConnected"] = &trussc::Serial::isConnected;
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
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger",
            sol::constructors<trussc::Logger()>(),
            sol::call_constructor, sol::constructors<trussc::Logger()>());
        t["onLog"] = &trussc::Logger::onLog;
        t["log"] = &trussc::Logger::log;
        t["setLogLevel"] = &trussc::Logger::setLogLevel;
        t["setConsoleLogLevel"] = &trussc::Logger::setConsoleLogLevel;
        t["getConsoleLogLevel"] = &trussc::Logger::getConsoleLogLevel;
        t["setLogFile"] = &trussc::Logger::setLogFile;
        t["closeFile"] = &trussc::Logger::closeFile;
        t["setFileLogLevel"] = &trussc::Logger::setFileLogLevel;
        t["getFileLogLevel"] = &trussc::Logger::getFileLogLevel;
        t["setSystemLogLevel"] = &trussc::Logger::setSystemLogLevel;
        t["getSystemLogLevel"] = &trussc::Logger::getSystemLogLevel;
        t["getLogFilePath"] = &trussc::Logger::getLogFilePath;
        t["isFileOpen"] = &trussc::Logger::isFileOpen;
    }
    {
        sol::usertype<trussc::IesProfile> t = lua->new_usertype<trussc::IesProfile>("IesProfile",
            sol::constructors<trussc::IesProfile()>(),
            sol::call_constructor, sol::constructors<trussc::IesProfile()>());
        t["load"] = &trussc::IesProfile::load;
        t["loadFromString"] = &trussc::IesProfile::loadFromString;
        t["isLoaded"] = &trussc::IesProfile::isLoaded;
        t["getMaxVerticalAngle"] = &trussc::IesProfile::getMaxVerticalAngle;
        t["getMaxCandela"] = &trussc::IesProfile::getMaxCandela;
        t["getTextureWidth"] = &trussc::IesProfile::getTextureWidth;
        t["getView"] = &trussc::IesProfile::getView;
        t["getSampler"] = &trussc::IesProfile::getSampler;
    }
    {
        sol::usertype<trussc::Font::PlacedGlyph> t = lua->new_usertype<trussc::Font::PlacedGlyph>("FontPlacedGlyph");
        t["codepoint"] = &trussc::Font::PlacedGlyph::codepoint;
        t["drawX"] = &trussc::Font::PlacedGlyph::drawX;
        t["baselineY"] = &trussc::Font::PlacedGlyph::baselineY;
        t["rotationCw"] = &trussc::Font::PlacedGlyph::rotationCw;
        t["pivotX"] = &trussc::Font::PlacedGlyph::pivotX;
        t["pivotY"] = &trussc::Font::PlacedGlyph::pivotY;
        t["scaleX"] = &trussc::Font::PlacedGlyph::scaleX;
    }
    lua->new_usertype<trussc::LoadError>("LoadError",
        sol::meta_function::equal_to, [](trussc::LoadError a, trussc::LoadError b){ return a == b; },
        "None", sol::var(trussc::LoadError::None),
        "FileNotFound", sol::var(trussc::LoadError::FileNotFound),
        "UnsupportedFormat", sol::var(trussc::LoadError::UnsupportedFormat),
        "DecodeFailed", sol::var(trussc::LoadError::DecodeFailed),
        "Unknown", sol::var(trussc::LoadError::Unknown));
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
