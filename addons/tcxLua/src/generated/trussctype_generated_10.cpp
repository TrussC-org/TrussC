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
void tcxLuaGenShard_10(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        auto constructors = tcxLuaConstructors<trussc::TcpServer
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
            , trussc::TcpServer()
#endif
        >();
        sol::usertype<trussc::TcpServer> t = lua->new_usertype<trussc::TcpServer>("TcpServer", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onClientConnect"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onClientConnect);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onClientDisconnect"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onClientDisconnect);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onError"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onError);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onReceive"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onReceive);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onSendComplete"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onSendComplete);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["broadcast"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcast(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcast(message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["broadcastAsync"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcastAsync(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcastAsync(message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["disconnectAllClients"] = &trussc::TcpServer::disconnectAllClients;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["disconnectClient"] = &trussc::TcpServer::disconnectClient;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClient"] = &trussc::TcpServer::getClient;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClientCount"] = &trussc::TcpServer::getClientCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClientIds"] = &trussc::TcpServer::getClientIds;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getPort"] = &trussc::TcpServer::getPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getSendAsyncBufferSize"] = &trussc::TcpServer::getSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getSendAsyncPendingBytes"] = &trussc::TcpServer::getSendAsyncPendingBytes;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["isRunning"] = &trussc::TcpServer::isRunning;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["send"] = sol::overload([](trussc::TcpServer& self, int clientId, const std::vector<char> & data) { return self.send(clientId, data); }, [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.send(clientId, message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["sendAsync"] = [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.sendAsync(clientId, message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setBindAddress"] = &trussc::TcpServer::setBindAddress;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setReceiveBufferSize"] = &trussc::TcpServer::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setSendAsyncBufferSize"] = &trussc::TcpServer::setSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setSendTimeout"] = &trussc::TcpServer::setSendTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["start"] = sol::overload([](trussc::TcpServer& self, int port) { return self.start(port); }, [](trussc::TcpServer& self, int port, int maxClients) { return self.start(port, maxClients); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["stop"] = &trussc::TcpServer::stop;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::Sound
            , trussc::Sound()
        >();
        sol::usertype<trussc::Sound> t = lua->new_usertype<trussc::Sound>("Sound", constructors, sol::call_constructor, constructors);
        t["clearChannelGains"] = &trussc::Sound::clearChannelGains;
        t["clearChannelMap"] = &trussc::Sound::clearChannelMap;
        t["getChannelGains"] = &trussc::Sound::getChannelGains;
        t["getChannelMap"] = &trussc::Sound::getChannelMap;
        t["getDuration"] = &trussc::Sound::getDuration;
        t["getMixMode"] = &trussc::Sound::getMixMode;
        t["getPan"] = &trussc::Sound::getPan;
        t["getPosition"] = &trussc::Sound::getPosition;
        t["getSpeed"] = &trussc::Sound::getSpeed;
        t["getVolume"] = &trussc::Sound::getVolume;
        t["isLoaded"] = &trussc::Sound::isLoaded;
        t["isLoop"] = &trussc::Sound::isLoop;
        t["isPaused"] = &trussc::Sound::isPaused;
        t["isPlaying"] = &trussc::Sound::isPlaying;
        t["isStreaming"] = &trussc::Sound::isStreaming;
        t["load"] = &trussc::Sound::load;
        t["loadFromBuffer"] = sol::overload([](trussc::Sound& self, const trussc::SoundBuffer & buf) { return self.loadFromBuffer(buf); }, [](trussc::Sound& self, std::shared_ptr<trussc::SoundBuffer> buf) { return self.loadFromBuffer(buf); });
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["loadStream"] = sol::overload([](trussc::Sound& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::Sound& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
#endif
        t["loadTestTone"] = sol::overload([](trussc::Sound& self) { return self.loadTestTone(); }, [](trussc::Sound& self, float frequency) { return self.loadTestTone(frequency); }, [](trussc::Sound& self, float frequency, float duration) { return self.loadTestTone(frequency, duration); });
        t["pause"] = &trussc::Sound::pause;
        t["play"] = &trussc::Sound::play;
        t["resume"] = &trussc::Sound::resume;
        t["setChannelGains"] = &trussc::Sound::setChannelGains;
        t["setChannelMap"] = sol::overload([](trussc::Sound& self, const std::vector<int> & map) { return self.setChannelMap(map); }, [](trussc::Sound& self, std::vector<std::vector<int>> map) { return self.setChannelMap(map); });
        t["setLoop"] = &trussc::Sound::setLoop;
        t["setMixMode"] = &trussc::Sound::setMixMode;
        t["setPan"] = &trussc::Sound::setPan;
        t["setPosition"] = &trussc::Sound::setPosition;
        t["setSpeed"] = &trussc::Sound::setSpeed;
        t["setVolume"] = &trussc::Sound::setVolume;
        t["stop"] = &trussc::Sound::stop;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Quaternion
            , trussc::Quaternion()
            , trussc::Quaternion(float, float, float, float)
        >();
        sol::usertype<trussc::Quaternion> t = lua->new_usertype<trussc::Quaternion>("Quaternion", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; };
        t[sol::meta_function::equal_to] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; };
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["identity"] = &trussc::Quaternion::identity;
        t["slerp"] = &trussc::Quaternion::slerp;
    }
    {
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["address"] = &trussc::NetworkInterface::address;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["name"] = &trussc::NetworkInterface::name;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
    }
    {
        sol::usertype<trussc::RuntimeEvents> t = lua->new_usertype<trussc::RuntimeEvents>("RuntimeEvents");
        t["console"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::console);
        t["deviceLost"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::deviceLost);
        t["exitRequested"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::exitRequested);
        t["filesOpened"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::filesOpened);
        t["hotReloadUnload"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::hotReloadUnload);
    }
    {
        sol::usertype<trussc::AudioDeviceChangedArgs> t = lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
    }
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
    {
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["id"] = &trussc::TouchEventArgs::id;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    {
        sol::usertype<trussc::ChipSoundBundle::Entry> t = lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
    {
        sol::usertype<trussc::Ray::Hit> t = lua->new_usertype<trussc::Ray::Hit>("RayHit");
        t["hit"] = &trussc::Ray::Hit::hit;
        t["point"] = &trussc::Ray::Hit::point;
        t["t"] = &trussc::Ray::Hit::t;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
