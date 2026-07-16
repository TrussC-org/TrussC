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
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter",
            sol::constructors<trussc::FileWriter()>(),
            sol::call_constructor, sol::constructors<trussc::FileWriter()>());
        t["open"] = sol::overload([](trussc::FileWriter& self, const std::string & path) { return self.open(path); }, [](trussc::FileWriter& self, const std::string & path, bool append) { return self.open(path, append); });
        t["close"] = &trussc::FileWriter::close;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
        t["flush"] = &trussc::FileWriter::flush;
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::JsonReadReflector> t = lua->new_usertype<trussc::JsonReadReflector>("JsonReadReflector",
            sol::constructors<trussc::JsonReadReflector(trussc::Json)>(),
            sol::call_constructor, sol::constructors<trussc::JsonReadReflector(trussc::Json)>());
        t["applied"] = &trussc::JsonReadReflector::applied;
        t["skipped"] = &trussc::JsonReadReflector::skipped;
        t["readOnly"] = &trussc::JsonReadReflector::readOnly;
        t["unknownKeys"] = &trussc::JsonReadReflector::unknownKeys;
        t["endGroup"] = &trussc::JsonReadReflector::endGroup;
    }
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["key"] = &trussc::KeyEventArgs::key;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["super"] = &trussc::KeyEventArgs::super;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
    }
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::TcpErrorEventArgs> t = lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        t["message"] = &trussc::TcpErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
