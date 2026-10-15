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
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    {
        auto constructors = tcxLuaConstructors<trussc::Window
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
            , trussc::Window()
#endif
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Window>::user_metatable()];
        sol::usertype<trussc::Window> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Window>>()
            : lua->new_usertype<trussc::Window>("Window", constructors, sol::call_constructor, constructors);
        (*lua)["Window"] = t;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setApp"] = &trussc::Window::setApp;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getApp"] = &trussc::Window::getApp;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["events"] = &trussc::Window::events;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["close"] = &trussc::Window::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isOpen"] = &trussc::Window::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setTitle"] = &trussc::Window::setTitle;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getTitle"] = &trussc::Window::getTitle;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getWidth"] = &trussc::Window::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getHeight"] = &trussc::Window::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isOccluded"] = &trussc::Window::isOccluded;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setSize"] = &trussc::Window::setSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFullscreen"] = &trussc::Window::setFullscreen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isFullscreen"] = &trussc::Window::isFullscreen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["toggleFullscreen"] = &trussc::Window::toggleFullscreen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setClearColor"] = &trussc::Window::setClearColor;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFps"] = &trussc::Window::setFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getFps"] = &trussc::Window::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMousePressToTree"] = &trussc::Window::dispatchMousePressToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMouseReleaseToTree"] = &trussc::Window::dispatchMouseReleaseToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMouseScrollToTree"] = &trussc::Window::dispatchMouseScrollToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchKeyPressToTree"] = &trussc::Window::dispatchKeyPressToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchKeyReleaseToTree"] = &trussc::Window::dispatchKeyReleaseToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["tickTree"] = &trussc::Window::tickTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["drawTreeNow"] = &trussc::Window::drawTreeNow;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["syncRootSize"] = &trussc::Window::syncRootSize;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::VideoPlayerBase
            , trussc::VideoPlayerBase()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::VideoPlayerBase>::user_metatable()];
        sol::usertype<trussc::VideoPlayerBase> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::VideoPlayerBase>>()
            : lua->new_usertype<trussc::VideoPlayerBase>("VideoPlayerBase", constructors, sol::call_constructor, constructors);
        (*lua)["VideoPlayerBase"] = t;
        t["onError"] = &trussc::VideoPlayerBase::onError;
        t["load"] = &trussc::VideoPlayerBase::load;
        t["close"] = &trussc::VideoPlayerBase::close;
        t["isLoaded"] = &trussc::VideoPlayerBase::isLoaded;
        t["play"] = &trussc::VideoPlayerBase::play;
        t["stop"] = &trussc::VideoPlayerBase::stop;
        t["setPaused"] = &trussc::VideoPlayerBase::setPaused;
        t["togglePause"] = &trussc::VideoPlayerBase::togglePause;
        t["update"] = &trussc::VideoPlayerBase::update;
        t["hasError"] = &trussc::VideoPlayerBase::hasError;
        t["getErrorMessage"] = &trussc::VideoPlayerBase::getErrorMessage;
        t["isPlaying"] = &trussc::VideoPlayerBase::isPlaying;
        t["isPaused"] = &trussc::VideoPlayerBase::isPaused;
        t["isFrameNew"] = &trussc::VideoPlayerBase::isFrameNew;
        t["isReady"] = &trussc::VideoPlayerBase::isReady;
        t["isDone"] = &trussc::VideoPlayerBase::isDone;
        t["getWidth"] = &trussc::VideoPlayerBase::getWidth;
        t["getHeight"] = &trussc::VideoPlayerBase::getHeight;
        t["getDuration"] = &trussc::VideoPlayerBase::getDuration;
        t["getPosition"] = &trussc::VideoPlayerBase::getPosition;
        t["setPosition"] = &trussc::VideoPlayerBase::setPosition;
        t["getCurrentTime"] = &trussc::VideoPlayerBase::getCurrentTime;
        t["setCurrentTime"] = &trussc::VideoPlayerBase::setCurrentTime;
        t["setVolume"] = &trussc::VideoPlayerBase::setVolume;
        t["getVolume"] = &trussc::VideoPlayerBase::getVolume;
        t["setSpeed"] = &trussc::VideoPlayerBase::setSpeed;
        t["getSpeed"] = &trussc::VideoPlayerBase::getSpeed;
        t["setPan"] = &trussc::VideoPlayerBase::setPan;
        t["getPan"] = &trussc::VideoPlayerBase::getPan;
        t["setLoop"] = &trussc::VideoPlayerBase::setLoop;
        t["isLoop"] = &trussc::VideoPlayerBase::isLoop;
        t["getFrameRate"] = &trussc::VideoPlayerBase::getFrameRate;
        t["getCurrentFrame"] = &trussc::VideoPlayerBase::getCurrentFrame;
        t["getTotalFrames"] = &trussc::VideoPlayerBase::getTotalFrames;
        t["setFrame"] = &trussc::VideoPlayerBase::setFrame;
        t["nextFrame"] = &trussc::VideoPlayerBase::nextFrame;
        t["previousFrame"] = &trussc::VideoPlayerBase::previousFrame;
        t["firstFrame"] = &trussc::VideoPlayerBase::firstFrame;
        t["getPixels"] = [](trussc::VideoPlayerBase& self) { return self.getPixels(); };
        t["hasAudio"] = &trussc::VideoPlayerBase::hasAudio;
        t["getAudioCodec"] = &trussc::VideoPlayerBase::getAudioCodec;
        t["getAudioData"] = &trussc::VideoPlayerBase::getAudioData;
        t["getAudioSampleRate"] = &trussc::VideoPlayerBase::getAudioSampleRate;
        t["getAudioChannels"] = &trussc::VideoPlayerBase::getAudioChannels;
        t["isUsingHwAccel"] = &trussc::VideoPlayerBase::isUsingHwAccel;
        t["getHwAccelName"] = &trussc::VideoPlayerBase::getHwAccelName;
        t["setResyncThreshold"] = &trussc::VideoPlayerBase::setResyncThreshold;
        t["getResyncThreshold"] = &trussc::VideoPlayerBase::getResyncThreshold;
        t["getTexture"] = [](trussc::VideoPlayerBase& self) -> decltype(auto) { return self.getTexture(); };
        t["hasTexture"] = &trussc::VideoPlayerBase::hasTexture;
        t["draw"] = sol::overload([](trussc::VideoPlayerBase& self, float x, float y) { return self.draw(x, y); }, [](trussc::VideoPlayerBase& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["setMinFilter"] = &trussc::VideoPlayerBase::setMinFilter;
        t["setMagFilter"] = &trussc::VideoPlayerBase::setMagFilter;
        t["setFilter"] = &trussc::VideoPlayerBase::setFilter;
        t["getMinFilter"] = &trussc::VideoPlayerBase::getMinFilter;
        t["getMagFilter"] = &trussc::VideoPlayerBase::getMagFilter;
        t["setWrapU"] = &trussc::VideoPlayerBase::setWrapU;
        t["setWrapV"] = &trussc::VideoPlayerBase::setWrapV;
        t["setWrap"] = &trussc::VideoPlayerBase::setWrap;
        t["getWrapU"] = &trussc::VideoPlayerBase::getWrapU;
        t["getWrapV"] = &trussc::VideoPlayerBase::getWrapV;
        t["save"] = &trussc::VideoPlayerBase::save;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Ray
            , trussc::Ray()
            , trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Ray>::user_metatable()];
        sol::usertype<trussc::Ray> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Ray>>()
            : lua->new_usertype<trussc::Ray>("Ray", constructors, sol::call_constructor, constructors);
        (*lua)["Ray"] = t;
        t["origin"] = &trussc::Ray::origin;
        t["direction"] = &trussc::Ray::direction;
        t["at"] = &trussc::Ray::at;
        t["transformed"] = &trussc::Ray::transformed;
        t["intersectZPlane"] = [](trussc::Ray& self) { return self.intersectZPlane(); };
        t["intersectPlane"] = [](trussc::Ray& self, const trussc::Vec3 & planeNormal, float planeD) { return self.intersectPlane(planeNormal, planeD); };
        t["intersectSphere"] = [](trussc::Ray& self, float radius) { return self.intersectSphere(radius); };
        t["intersectAABB"] = [](trussc::Ray& self, const trussc::Vec3 & boxMin, const trussc::Vec3 & boxMax) { return self.intersectAABB(boxMin, boxMax); };
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
    }
    {
        auto constructors = tcxLuaConstructors<trussc::SoundStream
            , trussc::SoundStream()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::SoundStream>::user_metatable()];
        sol::usertype<trussc::SoundStream> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::SoundStream>>()
            : lua->new_usertype<trussc::SoundStream>("SoundStream", constructors, sol::call_constructor, constructors);
        (*lua)["SoundStream"] = t;
        t["channels"] = &trussc::SoundStream::channels;
        t["sampleRate"] = &trussc::SoundStream::sampleRate;
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
        t["kind"] = &trussc::SoundStream::kind;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::SerialDeviceInfo>::user_metatable()];
        sol::usertype<trussc::SerialDeviceInfo> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::SerialDeviceInfo>>()
            : lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        (*lua)["SerialDeviceInfo"] = t;
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LogEventArgs
            , trussc::LogEventArgs(trussc::LogLevel, const std::string &)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::LogEventArgs>::user_metatable()];
        sol::usertype<trussc::LogEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::LogEventArgs>>()
            : lua->new_usertype<trussc::LogEventArgs>("LogEventArgs", constructors, sol::call_constructor, constructors);
        (*lua)["LogEventArgs"] = t;
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpServerReceiveEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpServerReceiveEventArgs>>()
            : lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        (*lua)["TcpServerReceiveEventArgs"] = t;
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpServerClient>::user_metatable()];
        sol::usertype<trussc::TcpServerClient> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpServerClient>>()
            : lua->new_usertype<trussc::TcpServerClient>("TcpServerClient");
        (*lua)["TcpServerClient"] = t;
        t["getId"] = &trussc::TcpServerClient::getId;
        t["getHost"] = &trussc::TcpServerClient::getHost;
        t["getPort"] = &trussc::TcpServerClient::getPort;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::RectNodeButton::HitResult>::user_metatable()];
        sol::usertype<trussc::RectNodeButton::HitResult> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::RectNodeButton::HitResult>>()
            : lua->new_usertype<trussc::RectNodeButton::HitResult>("RectNodeButtonHitResult");
        (*lua)["RectNodeButtonHitResult"] = t;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ScrollBar::HitResult>::user_metatable()];
        sol::usertype<trussc::ScrollBar::HitResult> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ScrollBar::HitResult>>()
            : lua->new_usertype<trussc::ScrollBar::HitResult>("ScrollBarHitResult");
        (*lua)["ScrollBarHitResult"] = t;
    }
    lua->new_usertype<trussc::SoundStream::Kind>("SoundStreamKind",
        sol::meta_function::equal_to, [](trussc::SoundStream::Kind a, trussc::SoundStream::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundStream::Kind::Eager),
        "Stream", sol::var(trussc::SoundStream::Kind::Stream));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
