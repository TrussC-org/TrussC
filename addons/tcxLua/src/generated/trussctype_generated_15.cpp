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
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::VideoWriter
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::VideoWriter()
#endif
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::VideoWriter>::user_metatable()];
        sol::usertype<trussc::VideoWriter> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::VideoWriter>>()
            : lua->new_usertype<trussc::VideoWriter>("VideoWriter", constructors, sol::call_constructor, constructors);
        (*lua)["VideoWriter"] = t;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::VideoWriter::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isOpen"] = &trussc::VideoWriter::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getWidth"] = &trussc::VideoWriter::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getHeight"] = &trussc::VideoWriter::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFps"] = &trussc::VideoWriter::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::VideoWriter::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSettings"] = &trussc::VideoWriter::getSettings;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::TweenMod
            , trussc::TweenMod()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TweenMod>::user_metatable()];
        sol::usertype<trussc::TweenMod> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TweenMod>>()
            : lua->new_usertype<trussc::TweenMod>("TweenMod", constructors, sol::call_constructor, constructors);
        (*lua)["TweenMod"] = t;
        t["complete"] = &trussc::TweenMod::complete;
        t["moveTo"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveTo(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveTo(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveTo(pos); }, [](trussc::TweenMod& self, const trussc::Vec2 & pos) -> decltype(auto) { return self.moveTo(pos); });
        t["moveBy"] = sol::overload([](trussc::TweenMod& self, float dx, float dy) -> decltype(auto) { return self.moveBy(dx, dy); }, [](trussc::TweenMod& self, float dx, float dy, float dz) -> decltype(auto) { return self.moveBy(dx, dy, dz); }, [](trussc::TweenMod& self, const trussc::Vec3 & delta) -> decltype(auto) { return self.moveBy(delta); }, [](trussc::TweenMod& self, const trussc::Vec2 & delta) -> decltype(auto) { return self.moveBy(delta); });
        t["moveFrom"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveFrom(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveFrom(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveFrom(pos); });
        t["scaleTo"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleTo(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleTo(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleTo(sx, sy, sz); }, [](trussc::TweenMod& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scaleTo(s); });
        t["scaleBy"] = sol::overload([](trussc::TweenMod& self, float factor) -> decltype(auto) { return self.scaleBy(factor); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleBy(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleBy(sx, sy, sz); });
        t["scaleFrom"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleFrom(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleFrom(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleFrom(sx, sy, sz); });
        t["rotateTo"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateTo(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateTo(q); });
        t["rotateBy"] = &trussc::TweenMod::rotateBy;
        t["rotateFrom"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateFrom(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateFrom(q); });
        t["rotateXTo"] = &trussc::TweenMod::rotateXTo;
        t["rotateXBy"] = &trussc::TweenMod::rotateXBy;
        t["rotateYTo"] = &trussc::TweenMod::rotateYTo;
        t["rotateYBy"] = &trussc::TweenMod::rotateYBy;
        t["rotateZTo"] = &trussc::TweenMod::rotateZTo;
        t["rotateZBy"] = &trussc::TweenMod::rotateZBy;
        t["rotateXFrom"] = &trussc::TweenMod::rotateXFrom;
        t["rotateYFrom"] = &trussc::TweenMod::rotateYFrom;
        t["duration"] = &trussc::TweenMod::duration;
        t["ease"] = sol::overload([](trussc::TweenMod& self, trussc::EaseType type) -> decltype(auto) { return self.ease(type); }, [](trussc::TweenMod& self, trussc::EaseType type, trussc::EaseMode mode) -> decltype(auto) { return self.ease(type, mode); }, [](trussc::TweenMod& self, trussc::EaseFunction fn) -> decltype(auto) { return self.ease(fn); }, [](trussc::TweenMod& self, trussc::EaseFunction fn, trussc::EaseMode mode) -> decltype(auto) { return self.ease(fn, mode); });
        t["delay"] = &trussc::TweenMod::delay;
        t["start"] = &trussc::TweenMod::start;
        t["pause"] = &trussc::TweenMod::pause;
        t["resume"] = &trussc::TweenMod::resume;
        t["reset"] = &trussc::TweenMod::reset;
        t["isPlaying"] = &trussc::TweenMod::isPlaying;
        t["isComplete"] = &trussc::TweenMod::isComplete;
        t["getProgress"] = &trussc::TweenMod::getProgress;
        t["getDuration"] = &trussc::TweenMod::getDuration;
        t["getDelay"] = &trussc::TweenMod::getDelay;
        t["getEaseType"] = &trussc::TweenMod::getEaseType;
        t["getEaseMode"] = &trussc::TweenMod::getEaseMode;
        t["getOwner"] = [](trussc::TweenMod& self) { return self.getOwner(); };
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::WindowSettings>::user_metatable()];
        sol::usertype<trussc::WindowSettings> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::WindowSettings>>()
            : lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        (*lua)["WindowSettings"] = t;
        t["width"] = &trussc::WindowSettings::width;
        t["height"] = &trussc::WindowSettings::height;
        t["title"] = &trussc::WindowSettings::title;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["clipboardSize"] = &trussc::WindowSettings::clipboardSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["uniformBufferReserve"] = &trussc::WindowSettings::uniformBufferReserve;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setClipboardSize"] = &trussc::WindowSettings::setClipboardSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["reserveUniformBuffer"] = &trussc::WindowSettings::reserveUniformBuffer;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::NetworkInterface>::user_metatable()];
        sol::usertype<trussc::NetworkInterface> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::NetworkInterface>>()
            : lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        (*lua)["NetworkInterface"] = t;
        t["name"] = &trussc::NetworkInterface::name;
        t["address"] = &trussc::NetworkInterface::address;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::AudioDeviceChangedArgs>::user_metatable()];
        sol::usertype<trussc::AudioDeviceChangedArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::AudioDeviceChangedArgs>>()
            : lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        (*lua)["AudioDeviceChangedArgs"] = t;
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::KeyEventArgs>::user_metatable()];
        sol::usertype<trussc::KeyEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::KeyEventArgs>>()
            : lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        (*lua)["KeyEventArgs"] = t;
        t["key"] = &trussc::KeyEventArgs::key;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["super"] = &trussc::KeyEventArgs::super;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::UdpReceiveEventArgs>::user_metatable()];
        sol::usertype<trussc::UdpReceiveEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::UdpReceiveEventArgs>>()
            : lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        (*lua)["UdpReceiveEventArgs"] = t;
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::DeviceLostEventArgs>::user_metatable()];
        sol::usertype<trussc::DeviceLostEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::DeviceLostEventArgs>>()
            : lua->new_usertype<trussc::DeviceLostEventArgs>("DeviceLostEventArgs");
        (*lua)["DeviceLostEventArgs"] = t;
        t["reason"] = &trussc::DeviceLostEventArgs::reason;
        t["cancel"] = &trussc::DeviceLostEventArgs::cancel;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpReceiveEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpReceiveEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpReceiveEventArgs>>()
            : lua->new_usertype<trussc::TcpReceiveEventArgs>("TcpReceiveEventArgs");
        (*lua)["TcpReceiveEventArgs"] = t;
        t["data"] = &trussc::TcpReceiveEventArgs::data;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Mod>::user_metatable()];
        sol::usertype<trussc::Mod> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Mod>>()
            : lua->new_usertype<trussc::Mod>("Mod");
        (*lua)["Mod"] = t;
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
    lua->new_usertype<trussc::StrokeCap>("StrokeCap",
        sol::meta_function::equal_to, [](trussc::StrokeCap a, trussc::StrokeCap b){ return a == b; },
        "Butt", sol::var(trussc::StrokeCap::Butt),
        "Round", sol::var(trussc::StrokeCap::Round),
        "Square", sol::var(trussc::StrokeCap::Square));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
