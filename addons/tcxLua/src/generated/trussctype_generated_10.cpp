// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_10(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
    {
        sol::usertype<trussc::VideoGrabber> t = lua->new_usertype<trussc::VideoGrabber>("VideoGrabber",
            sol::constructors<trussc::VideoGrabber()>(),
            sol::call_constructor, sol::constructors<trussc::VideoGrabber()>());
        t["listDevices"] = &trussc::VideoGrabber::listDevices;
        t["setDeviceID"] = &trussc::VideoGrabber::setDeviceID;
        t["getDeviceID"] = &trussc::VideoGrabber::getDeviceID;
        t["setDesiredFrameRate"] = &trussc::VideoGrabber::setDesiredFrameRate;
        t["getDesiredFrameRate"] = &trussc::VideoGrabber::getDesiredFrameRate;
        t["setVerbose"] = &trussc::VideoGrabber::setVerbose;
        t["isVerbose"] = &trussc::VideoGrabber::isVerbose;
        t["setup"] = sol::overload([](trussc::VideoGrabber& self) { return self.setup(); }, [](trussc::VideoGrabber& self, int width) { return self.setup(width); }, [](trussc::VideoGrabber& self, int width, int height) { return self.setup(width, height); });
        t["close"] = &trussc::VideoGrabber::close;
        t["update"] = &trussc::VideoGrabber::update;
        t["isFrameNew"] = &trussc::VideoGrabber::isFrameNew;
        t["isInitialized"] = &trussc::VideoGrabber::isInitialized;
        t["isPendingPermission"] = &trussc::VideoGrabber::isPendingPermission;
        t["getWidth"] = &trussc::VideoGrabber::getWidth;
        t["getHeight"] = &trussc::VideoGrabber::getHeight;
        t["getDeviceName"] = &trussc::VideoGrabber::getDeviceName;
        t["getPixels"] = [](trussc::VideoGrabber& self) { return self.getPixels(); };
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFrameQueueSize"] = &trussc::VideoGrabber::setFrameQueueSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getFrameQueueSize"] = &trussc::VideoGrabber::getFrameQueueSize;
#endif
        t["copyToImage"] = &trussc::VideoGrabber::copyToImage;
        t["getTexture"] = [](trussc::VideoGrabber& self) -> decltype(auto) { return self.getTexture(); };
        t["checkCameraPermission"] = &trussc::VideoGrabber::checkCameraPermission;
        t["requestCameraPermission"] = &trussc::VideoGrabber::requestCameraPermission;
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
        sol::usertype<trussc::HasTexture> t = lua->new_usertype<trussc::HasTexture>("HasTexture");
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["save"] = &trussc::HasTexture::save;
    }
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
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    {
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream",
            sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>(),
            sol::call_constructor, sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>());
    }
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
