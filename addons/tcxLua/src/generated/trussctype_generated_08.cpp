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
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter",
            sol::constructors<trussc::FileWriter()>(),
            sol::call_constructor, sol::constructors<trussc::FileWriter()>());
        t["open"] = sol::overload([](trussc::FileWriter& self, const fs::path & path) { return self.open(path); }, [](trussc::FileWriter& self, const fs::path & path, bool append) { return self.open(path, append); });
        t["close"] = &trussc::FileWriter::close;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
        t["flush"] = &trussc::FileWriter::flush;
    }
    lua->new_usertype<trussc::TextureFormat>("TextureFormat",
        sol::meta_function::equal_to, [](trussc::TextureFormat a, trussc::TextureFormat b){ return a == b; },
        "RGBA8", sol::var(trussc::TextureFormat::RGBA8),
        "RGBA16F", sol::var(trussc::TextureFormat::RGBA16F),
        "RGBA32F", sol::var(trussc::TextureFormat::RGBA32F),
        "R8", sol::var(trussc::TextureFormat::R8),
        "R16F", sol::var(trussc::TextureFormat::R16F),
        "R32F", sol::var(trussc::TextureFormat::R32F),
        "RG8", sol::var(trussc::TextureFormat::RG8),
        "RG16F", sol::var(trussc::TextureFormat::RG16F),
        "RG32F", sol::var(trussc::TextureFormat::RG32F),
        "BGRA8", sol::var(trussc::TextureFormat::BGRA8),
        "RGBA16", sol::var(trussc::TextureFormat::RGBA16));
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["isDerived"] = &trussc::Reflector::isDerived;
        t["pushDerived"] = &trussc::Reflector::pushDerived;
        t["popDerived"] = &trussc::Reflector::popDerived;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    lua->new_usertype<trussc::StrokeMesh::JoinType>("StrokeMeshJoinType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::JoinType a, trussc::StrokeMesh::JoinType b){ return a == b; },
        "JOIN_MITER", sol::var(trussc::StrokeMesh::JoinType::JOIN_MITER),
        "JOIN_ROUND", sol::var(trussc::StrokeMesh::JoinType::JOIN_ROUND),
        "JOIN_BEVEL", sol::var(trussc::StrokeMesh::JoinType::JOIN_BEVEL));
    {
        sol::usertype<trussc::Node::HitResult> t = lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        t["node"] = &trussc::Node::HitResult::node;
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    lua->new_usertype<trussc::LayoutDirection>("LayoutDirection",
        sol::meta_function::equal_to, [](trussc::LayoutDirection a, trussc::LayoutDirection b){ return a == b; },
        "Vertical", sol::var(trussc::LayoutDirection::Vertical),
        "Horizontal", sol::var(trussc::LayoutDirection::Horizontal));
    {
        sol::usertype<trussc::AudioRecordSettings> t = lua->new_usertype<trussc::AudioRecordSettings>("AudioRecordSettings");
        t["format"] = &trussc::AudioRecordSettings::format;
        t["channelMap"] = &trussc::AudioRecordSettings::channelMap;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
