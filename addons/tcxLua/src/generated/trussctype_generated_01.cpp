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
void tcxLuaGenShard_01(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
    {
        auto constructors = tcxLuaConstructors<trussc::VideoGrabber
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
            , trussc::VideoGrabber()
#endif
        >();
        sol::usertype<trussc::VideoGrabber> t = lua->new_usertype<trussc::VideoGrabber>("VideoGrabber", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["onDisconnect"] = &trussc::VideoGrabber::onDisconnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["close"] = &trussc::VideoGrabber::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["copyToImage"] = &trussc::VideoGrabber::copyToImage;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["draw"] = sol::overload([](trussc::VideoGrabber& self, float x, float y) { return self.draw(x, y); }, [](trussc::VideoGrabber& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getDesiredFrameRate"] = &trussc::VideoGrabber::getDesiredFrameRate;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getDeviceID"] = &trussc::VideoGrabber::getDeviceID;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getDeviceName"] = &trussc::VideoGrabber::getDeviceName;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameQueueSize"] = &trussc::VideoGrabber::getFrameQueueSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getHeight"] = &trussc::VideoGrabber::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getMagFilter"] = &trussc::VideoGrabber::getMagFilter;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getMinFilter"] = &trussc::VideoGrabber::getMinFilter;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getPixels"] = [](trussc::VideoGrabber& self) { return self.getPixels(); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getTexture"] = [](trussc::VideoGrabber& self) -> decltype(auto) { return self.getTexture(); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getWidth"] = &trussc::VideoGrabber::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getWrapU"] = &trussc::VideoGrabber::getWrapU;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["getWrapV"] = &trussc::VideoGrabber::getWrapV;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["hasTexture"] = &trussc::VideoGrabber::hasTexture;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["isConnected"] = &trussc::VideoGrabber::isConnected;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["isFrameNew"] = &trussc::VideoGrabber::isFrameNew;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["isInitialized"] = &trussc::VideoGrabber::isInitialized;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["isPendingPermission"] = &trussc::VideoGrabber::isPendingPermission;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["isVerbose"] = &trussc::VideoGrabber::isVerbose;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["listDevices"] = &trussc::VideoGrabber::listDevices;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["save"] = &trussc::VideoGrabber::save;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setDesiredFrameRate"] = &trussc::VideoGrabber::setDesiredFrameRate;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setDeviceID"] = &trussc::VideoGrabber::setDeviceID;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setDeviceUniqueID"] = &trussc::VideoGrabber::setDeviceUniqueID;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setFilter"] = &trussc::VideoGrabber::setFilter;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setFrameQueueSize"] = &trussc::VideoGrabber::setFrameQueueSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setMagFilter"] = &trussc::VideoGrabber::setMagFilter;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setMinFilter"] = &trussc::VideoGrabber::setMinFilter;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setVerbose"] = &trussc::VideoGrabber::setVerbose;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setWrap"] = &trussc::VideoGrabber::setWrap;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setWrapU"] = &trussc::VideoGrabber::setWrapU;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setWrapV"] = &trussc::VideoGrabber::setWrapV;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["setup"] = sol::overload([](trussc::VideoGrabber& self, int width, int height) { return self.setup(width, height); }, [](trussc::VideoGrabber& self) { return self.setup(); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["update"] = &trussc::VideoGrabber::update;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["checkCameraPermission"] = &trussc::VideoGrabber::checkCameraPermission;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
        t["requestCameraPermission"] = &trussc::VideoGrabber::requestCameraPermission;
#endif
    }
#endif
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
        auto constructors = tcxLuaConstructors<trussc::EventListener
            , trussc::EventListener()
        >();
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener", constructors, sol::call_constructor, constructors);
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::SoundSource::Kind>("SoundSourceKind",
        sol::meta_function::equal_to, [](trussc::SoundSource::Kind a, trussc::SoundSource::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundSource::Kind::Eager),
        "Stream", sol::var(trussc::SoundSource::Kind::Stream));
    {
        sol::usertype<trussc::AudioRecordSettings> t = lua->new_usertype<trussc::AudioRecordSettings>("AudioRecordSettings");
        t["channelMap"] = &trussc::AudioRecordSettings::channelMap;
        t["format"] = &trussc::AudioRecordSettings::format;
    }
    {
        sol::usertype<trussc::MicStartResult> t = lua->new_usertype<trussc::MicStartResult>("MicStartResult");
        t["message"] = &trussc::MicStartResult::message;
        t["success"] = &trussc::MicStartResult::success;
    }
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
