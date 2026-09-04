// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_08(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Sound> t = lua->new_usertype<trussc::Sound>("Sound",
            sol::constructors<trussc::Sound()>(),
            sol::call_constructor, sol::constructors<trussc::Sound()>());
        t["load"] = &trussc::Sound::load;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["loadStream"] = sol::overload([](trussc::Sound& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::Sound& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
#endif
        t["loadTestTone"] = sol::overload([](trussc::Sound& self) { return self.loadTestTone(); }, [](trussc::Sound& self, float frequency) { return self.loadTestTone(frequency); }, [](trussc::Sound& self, float frequency, float duration) { return self.loadTestTone(frequency, duration); });
        t["loadFromBuffer"] = sol::overload([](trussc::Sound& self, const trussc::SoundBuffer & buf) { return self.loadFromBuffer(buf); }, [](trussc::Sound& self, std::shared_ptr<trussc::SoundBuffer> buf) { return self.loadFromBuffer(buf); });
        t["isLoaded"] = &trussc::Sound::isLoaded;
        t["isStreaming"] = &trussc::Sound::isStreaming;
        t["play"] = &trussc::Sound::play;
        t["stop"] = &trussc::Sound::stop;
        t["pause"] = &trussc::Sound::pause;
        t["resume"] = &trussc::Sound::resume;
        t["setVolume"] = &trussc::Sound::setVolume;
        t["getVolume"] = &trussc::Sound::getVolume;
        t["setLoop"] = &trussc::Sound::setLoop;
        t["isLoop"] = &trussc::Sound::isLoop;
        t["setPan"] = &trussc::Sound::setPan;
        t["getPan"] = &trussc::Sound::getPan;
        t["setSpeed"] = &trussc::Sound::setSpeed;
        t["getSpeed"] = &trussc::Sound::getSpeed;
        t["setMixMode"] = &trussc::Sound::setMixMode;
        t["getMixMode"] = &trussc::Sound::getMixMode;
        t["setChannelMap"] = sol::overload([](trussc::Sound& self, const std::vector<int> & map) { return self.setChannelMap(map); }, [](trussc::Sound& self, std::vector<std::vector<int>> map) { return self.setChannelMap(map); });
        t["getChannelMap"] = &trussc::Sound::getChannelMap;
        t["setChannelGains"] = &trussc::Sound::setChannelGains;
        t["getChannelGains"] = &trussc::Sound::getChannelGains;
        t["clearChannelMap"] = &trussc::Sound::clearChannelMap;
        t["clearChannelGains"] = &trussc::Sound::clearChannelGains;
        t["isPlaying"] = &trussc::Sound::isPlaying;
        t["isPaused"] = &trussc::Sound::isPaused;
        t["getPosition"] = &trussc::Sound::getPosition;
        t["setPosition"] = &trussc::Sound::setPosition;
        t["getDuration"] = &trussc::Sound::getDuration;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::TcpClient> t = lua->new_usertype<trussc::TcpClient>("TcpClient",
            sol::constructors<trussc::TcpClient()>(),
            sol::call_constructor, sol::constructors<trussc::TcpClient()>());
        t["onConnect"] = &trussc::TcpClient::onConnect;
        t["onReceive"] = &trussc::TcpClient::onReceive;
        t["onDisconnect"] = &trussc::TcpClient::onDisconnect;
        t["onError"] = &trussc::TcpClient::onError;
        t["connect"] = &trussc::TcpClient::connect;
        t["connectAsync"] = &trussc::TcpClient::connectAsync;
        t["disconnect"] = &trussc::TcpClient::disconnect;
        t["isConnected"] = &trussc::TcpClient::isConnected;
        t["send"] = sol::overload([](trussc::TcpClient& self, const std::vector<char> & data) { return self.send(data); }, [](trussc::TcpClient& self, const std::string & message) { return self.send(message); });
        t["setReceiveBufferSize"] = &trussc::TcpClient::setReceiveBufferSize;
        t["setBlocking"] = &trussc::TcpClient::setBlocking;
        t["setUseThread"] = &trussc::TcpClient::setUseThread;
        t["isUsingThread"] = &trussc::TcpClient::isUsingThread;
        t["processNetwork"] = &trussc::TcpClient::processNetwork;
        t["getRemoteHost"] = &trussc::TcpClient::getRemoteHost;
        t["getRemotePort"] = &trussc::TcpClient::getRemotePort;
    }
#endif
    {
        sol::usertype<trussc::ColorHSB> t = lua->new_usertype<trussc::ColorHSB>("ColorHSB",
            sol::constructors<trussc::ColorHSB(), trussc::ColorHSB(float, float, float), trussc::ColorHSB(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorHSB(), trussc::ColorHSB(float, float, float), trussc::ColorHSB(float, float, float, float)>());
        t["h"] = &trussc::ColorHSB::h;
        t["s"] = &trussc::ColorHSB::s;
        t["b"] = &trussc::ColorHSB::b;
        t["a"] = &trussc::ColorHSB::a;
        t["toRGB"] = &trussc::ColorHSB::toRGB;
        t["toLinear"] = &trussc::ColorHSB::toLinear;
        t["toOKLab"] = &trussc::ColorHSB::toOKLab;
        t["toOKLCH"] = &trussc::ColorHSB::toOKLCH;
        t["lerp"] = sol::overload([](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t) { return self.lerp(target, t); }, [](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec3>> t = lua->new_usertype<trussc::Tween<trussc::Vec3>>("Tween_Vec3",
            sol::constructors<trussc::Tween<trussc::Vec3>(), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float, trussc::EaseType), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<trussc::Vec3>(), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float, trussc::EaseType), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isApple"] = &trussc::Platform::isApple;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["name"] = &trussc::Platform::name;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
    }
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::ResizeEventArgs> t = lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        t["width"] = &trussc::ResizeEventArgs::width;
        t["height"] = &trussc::ResizeEventArgs::height;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
