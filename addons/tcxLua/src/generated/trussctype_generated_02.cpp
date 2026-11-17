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
        t["onError"] = tcx_lua_detail::eventProperty<trussc::UdpSocket>(&trussc::UdpSocket::onError);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onReceive"] = tcx_lua_detail::eventProperty<trussc::UdpSocket>(&trussc::UdpSocket::onReceive);
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
        auto constructors = tcxLuaConstructors<trussc::IVec3
            , trussc::IVec3()
            , trussc::IVec3(int, int, int)
            , trussc::IVec3(int)
            , trussc::IVec3(const trussc::IVec2 &)
            , trussc::IVec3(const trussc::IVec2 &, int)
        >();
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec3& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec3& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; };
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::IVec2
            , trussc::IVec2()
            , trussc::IVec2(int, int)
            , trussc::IVec2(int)
        >();
        sol::usertype<trussc::IVec2> t = lua->new_usertype<trussc::IVec2>("IVec2", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec2& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec2& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; };
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
    }
    {
        sol::usertype<trussc::PlayingSoundInfo> t = lua->new_usertype<trussc::PlayingSoundInfo>("PlayingSoundInfo");
        t["duration"] = &trussc::PlayingSoundInfo::duration;
        t["level"] = &trussc::PlayingSoundInfo::level;
        t["loop"] = &trussc::PlayingSoundInfo::loop;
        t["pan"] = &trussc::PlayingSoundInfo::pan;
        t["path"] = &trussc::PlayingSoundInfo::path;
        t["paused"] = &trussc::PlayingSoundInfo::paused;
        t["position"] = &trussc::PlayingSoundInfo::position;
        t["slot"] = &trussc::PlayingSoundInfo::slot;
        t["speed"] = &trussc::PlayingSoundInfo::speed;
        t["streaming"] = &trussc::PlayingSoundInfo::streaming;
        t["volume"] = &trussc::PlayingSoundInfo::volume;
    }
    lua->new_usertype<trussc::PrimitiveType>("PrimitiveType",
        sol::meta_function::equal_to, [](trussc::PrimitiveType a, trussc::PrimitiveType b){ return a == b; },
        "Points", sol::var(trussc::PrimitiveType::Points),
        "Lines", sol::var(trussc::PrimitiveType::Lines),
        "LineStrip", sol::var(trussc::PrimitiveType::LineStrip),
        "Triangles", sol::var(trussc::PrimitiveType::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveType::TriangleStrip),
        "Quads", sol::var(trussc::PrimitiveType::Quads));
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
        t["success"] = &trussc::LoadResult::success;
    }
    lua->new_usertype<trussc::VideoCodec>("VideoCodec",
        sol::meta_function::equal_to, [](trussc::VideoCodec a, trussc::VideoCodec b){ return a == b; },
        "H264", sol::var(trussc::VideoCodec::H264),
        "HEVC", sol::var(trussc::VideoCodec::HEVC),
        "ProRes422", sol::var(trussc::VideoCodec::ProRes422),
        "ProRes4444", sol::var(trussc::VideoCodec::ProRes4444));
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["changed"] = &trussc::TouchPoint::changed;
        t["id"] = &trussc::TouchPoint::id;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
    }
    {
        sol::usertype<trussc::TcpServerClient> t = lua->new_usertype<trussc::TcpServerClient>("TcpServerClient");
        t["getHost"] = &trussc::TcpServerClient::getHost;
        t["getId"] = &trussc::TcpServerClient::getId;
        t["getPort"] = &trussc::TcpServerClient::getPort;
    }
    {
        sol::usertype<trussc::VideoErrorEventArgs> t = lua->new_usertype<trussc::VideoErrorEventArgs>("VideoErrorEventArgs");
        t["errorCode"] = &trussc::VideoErrorEventArgs::errorCode;
        t["message"] = &trussc::VideoErrorEventArgs::message;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
    {
        sol::usertype<trussc::FocusEventArgs> t = lua->new_usertype<trussc::FocusEventArgs>("FocusEventArgs");
        t["focused"] = &trussc::FocusEventArgs::focused;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
