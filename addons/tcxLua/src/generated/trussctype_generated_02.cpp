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
        t["setBindAddress"] = &trussc::UdpSocket::setBindAddress;
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
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["source"] = &trussc::MouseDragEventArgs::source;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["derived"] = &trussc::JsonWriteReflector::derived;
        t["includeDerived"] = &trussc::JsonWriteReflector::includeDerived;
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
        t["isDerived"] = &trussc::JsonWriteReflector::isDerived;
        t["isReadOnly"] = &trussc::JsonWriteReflector::isReadOnly;
        t["popDerived"] = &trussc::JsonWriteReflector::popDerived;
        t["popReadOnly"] = &trussc::JsonWriteReflector::popReadOnly;
        t["pushDerived"] = &trussc::JsonWriteReflector::pushDerived;
        t["pushReadOnly"] = &trussc::JsonWriteReflector::pushReadOnly;
    }
    {
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isApple"] = &trussc::Platform::isApple;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["name"] = &trussc::Platform::name;
    }
    {
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["a"] = &trussc::ShaderVertex::a;
        t["b"] = &trussc::ShaderVertex::b;
        t["g"] = &trussc::ShaderVertex::g;
        t["r"] = &trussc::ShaderVertex::r;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
    }
    lua->new_usertype<trussc::StrokeMesh::JoinType>("StrokeMeshJoinType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::JoinType a, trussc::StrokeMesh::JoinType b){ return a == b; },
        "JOIN_MITER", sol::var(trussc::StrokeMesh::JoinType::JOIN_MITER),
        "JOIN_ROUND", sol::var(trussc::StrokeMesh::JoinType::JOIN_ROUND),
        "JOIN_BEVEL", sol::var(trussc::StrokeMesh::JoinType::JOIN_BEVEL));
    {
        sol::usertype<trussc::Node::HitResult> t = lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["node"] = &trussc::Node::HitResult::node;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    lua->new_usertype<trussc::TcyMode>("TcyMode",
        sol::meta_function::equal_to, [](trussc::TcyMode a, trussc::TcyMode b){ return a == b; },
        "Rotate", sol::var(trussc::TcyMode::Rotate),
        "Upright", sol::var(trussc::TcyMode::Upright),
        "Combine", sol::var(trussc::TcyMode::Combine));
    {
        sol::usertype<trussc::ScreenshotSaveEventArgs> t = lua->new_usertype<trussc::ScreenshotSaveEventArgs>("ScreenshotSaveEventArgs");
        t["path"] = &trussc::ScreenshotSaveEventArgs::path;
        t["result"] = &trussc::ScreenshotSaveEventArgs::result;
    }
    {
        sol::usertype<trussc::ConsoleEventArgs> t = lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        t["args"] = &trussc::ConsoleEventArgs::args;
        t["raw"] = &trussc::ConsoleEventArgs::raw;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
