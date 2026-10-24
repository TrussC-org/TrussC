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
void tcxLuaGenShard_02(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::UdpSocket
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::UdpSocket()
#endif
        >();
        sol::usertype<trussc::UdpSocket> t = lua->new_usertype<trussc::UdpSocket>("UdpSocket", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onError"] = &trussc::UdpSocket::onError;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onReceive"] = &trussc::UdpSocket::onReceive;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["bind"] = sol::overload([](trussc::UdpSocket& self, int port) { return self.bind(port); }, [](trussc::UdpSocket& self, int port, bool startReceiving) { return self.bind(port, startReceiving); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::UdpSocket::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["connect"] = &trussc::UdpSocket::connect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["create"] = &trussc::UdpSocket::create;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedHost"] = &trussc::UdpSocket::getConnectedHost;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedPort"] = &trussc::UdpSocket::getConnectedPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getLocalPort"] = &trussc::UdpSocket::getLocalPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isReceiving"] = &trussc::UdpSocket::isReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isValid"] = &trussc::UdpSocket::isValid;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["joinMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.joinMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.joinMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["leaveMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.leaveMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.leaveMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["processNetwork"] = &trussc::UdpSocket::processNetwork;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["send"] = [](trussc::UdpSocket& self, const std::string & message) { return self.send(message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["sendTo"] = [](trussc::UdpSocket& self, const std::string & host, int port, const std::string & message) { return self.sendTo(host, port, message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setBroadcast"] = &trussc::UdpSocket::setBroadcast;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastInterface"] = &trussc::UdpSocket::setMulticastInterface;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastLoopback"] = &trussc::UdpSocket::setMulticastLoopback;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastTTL"] = &trussc::UdpSocket::setMulticastTTL;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setNonBlocking"] = &trussc::UdpSocket::setNonBlocking;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveBufferSize"] = &trussc::UdpSocket::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveTimeout"] = &trussc::UdpSocket::setReceiveTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReuseAddress"] = &trussc::UdpSocket::setReuseAddress;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReusePort"] = &trussc::UdpSocket::setReusePort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setSendBufferSize"] = &trussc::UdpSocket::setSendBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setUseThread"] = &trussc::UdpSocket::setUseThread;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["startReceiving"] = &trussc::UdpSocket::startReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stopReceiving"] = &trussc::UdpSocket::stopReceiving;
#endif
    }
#endif
    {
        sol::usertype<trussc::AudioStats> t = lua->new_usertype<trussc::AudioStats>("AudioStats");
        t["clippedSamples"] = &trussc::AudioStats::clippedSamples;
        t["cpuUsage"] = &trussc::AudioStats::cpuUsage;
        t["cpuUsagePeak"] = &trussc::AudioStats::cpuUsagePeak;
        t["droppedDecoderError"] = &trussc::AudioStats::droppedDecoderError;
        t["droppedNotRunning"] = &trussc::AudioStats::droppedNotRunning;
        t["droppedPlays"] = &trussc::AudioStats::droppedPlays;
        t["droppedPolyphonyLimit"] = &trussc::AudioStats::droppedPolyphonyLimit;
        t["droppedStreamLimit"] = &trussc::AudioStats::droppedStreamLimit;
        t["initFailure"] = &trussc::AudioStats::initFailure;
        t["initFailureResult"] = &trussc::AudioStats::initFailureResult;
        t["peak"] = &trussc::AudioStats::peak;
        t["rms"] = &trussc::AudioStats::rms;
        t["stalled"] = &trussc::AudioStats::stalled;
        t["underrunFrames"] = &trussc::AudioStats::underrunFrames;
        t["voicesStoppedByReinit"] = &trussc::AudioStats::voicesStoppedByReinit;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::SoundStream
            , trussc::SoundStream()
        >();
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream", constructors, sol::call_constructor, constructors);
        t["channels"] = &trussc::SoundStream::channels;
        t["sampleRate"] = &trussc::SoundStream::sampleRate;
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["kind"] = &trussc::SoundStream::kind;
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
        t["success"] = &trussc::LoadResult::success;
    }
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["resolution"] = &trussc::CurveStyle::resolution;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
    }
    {
        sol::usertype<trussc::HeadlessSettings> t = lua->new_usertype<trussc::HeadlessSettings>("HeadlessSettings");
        t["targetFps"] = &trussc::HeadlessSettings::targetFps;
        t["setFps"] = &trussc::HeadlessSettings::setFps;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
