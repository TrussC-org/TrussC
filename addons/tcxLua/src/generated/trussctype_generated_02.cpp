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
void tcxLuaGenShard_02(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::UdpSocket
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::UdpSocket()
#endif
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::UdpSocket>::user_metatable()];
        sol::usertype<trussc::UdpSocket> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::UdpSocket>>()
            : lua->new_usertype<trussc::UdpSocket>("UdpSocket", constructors, sol::call_constructor, constructors);
        (*lua)["UdpSocket"] = t;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onReceive"] = &trussc::UdpSocket::onReceive;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onError"] = &trussc::UdpSocket::onError;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["create"] = &trussc::UdpSocket::create;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["bind"] = sol::overload([](trussc::UdpSocket& self, int port) { return self.bind(port); }, [](trussc::UdpSocket& self, int port, bool startReceiving) { return self.bind(port, startReceiving); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["connect"] = &trussc::UdpSocket::connect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::UdpSocket::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["sendTo"] = [](trussc::UdpSocket& self, const std::string & host, int port, const std::string & message) { return self.sendTo(host, port, message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["send"] = [](trussc::UdpSocket& self, const std::string & message) { return self.send(message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["startReceiving"] = &trussc::UdpSocket::startReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stopReceiving"] = &trussc::UdpSocket::stopReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isReceiving"] = &trussc::UdpSocket::isReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setNonBlocking"] = &trussc::UdpSocket::setNonBlocking;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setBroadcast"] = &trussc::UdpSocket::setBroadcast;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReuseAddress"] = &trussc::UdpSocket::setReuseAddress;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReusePort"] = &trussc::UdpSocket::setReusePort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["joinMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.joinMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.joinMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["leaveMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.leaveMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.leaveMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastTTL"] = &trussc::UdpSocket::setMulticastTTL;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastLoopback"] = &trussc::UdpSocket::setMulticastLoopback;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastInterface"] = &trussc::UdpSocket::setMulticastInterface;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveBufferSize"] = &trussc::UdpSocket::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setSendBufferSize"] = &trussc::UdpSocket::setSendBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveTimeout"] = &trussc::UdpSocket::setReceiveTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setUseThread"] = &trussc::UdpSocket::setUseThread;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getLocalPort"] = &trussc::UdpSocket::getLocalPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["processNetwork"] = &trussc::UdpSocket::processNetwork;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isValid"] = &trussc::UdpSocket::isValid;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedHost"] = &trussc::UdpSocket::getConnectedHost;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedPort"] = &trussc::UdpSocket::getConnectedPort;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::IVec3>::user_metatable()];
        sol::usertype<trussc::IVec3> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::IVec3>>()
            : lua->new_usertype<trussc::IVec3>("IVec3", constructors, sol::call_constructor, constructors);
        (*lua)["IVec3"] = t;
        t[sol::meta_function::addition] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec3& a){ return -a; };
        t[sol::meta_function::multiplication] = [](const trussc::IVec3& a, int b){ return a * b; };
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::IVec2>::user_metatable()];
        sol::usertype<trussc::IVec2> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::IVec2>>()
            : lua->new_usertype<trussc::IVec2>("IVec2", constructors, sol::call_constructor, constructors);
        (*lua)["IVec2"] = t;
        t[sol::meta_function::addition] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec2& a){ return -a; };
        t[sol::meta_function::multiplication] = [](const trussc::IVec2& a, int b){ return a * b; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; };
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::CameraContext>::user_metatable()];
        sol::usertype<trussc::CameraContext> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::CameraContext>>()
            : lua->new_usertype<trussc::CameraContext>("CameraContext");
        (*lua)["CameraContext"] = t;
        t["view"] = &trussc::CameraContext::view;
        t["projection"] = &trussc::CameraContext::projection;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["pickable"] = &trussc::CameraContext::pickable;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpSendCompleteEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpSendCompleteEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpSendCompleteEventArgs>>()
            : lua->new_usertype<trussc::TcpSendCompleteEventArgs>("TcpSendCompleteEventArgs");
        (*lua)["TcpSendCompleteEventArgs"] = t;
        t["clientId"] = &trussc::TcpSendCompleteEventArgs::clientId;
        t["sendId"] = &trussc::TcpSendCompleteEventArgs::sendId;
        t["error"] = &trussc::TcpSendCompleteEventArgs::error;
        t["bytesSent"] = &trussc::TcpSendCompleteEventArgs::bytesSent;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Node::HitResult>::user_metatable()];
        sol::usertype<trussc::Node::HitResult> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Node::HitResult>>()
            : lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        (*lua)["NodeHitResult"] = t;
        t["node"] = &trussc::Node::HitResult::node;
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    lua->new_usertype<trussc::PrimitiveType>("PrimitiveType",
        sol::meta_function::equal_to, [](trussc::PrimitiveType a, trussc::PrimitiveType b){ return a == b; },
        "Points", sol::var(trussc::PrimitiveType::Points),
        "Lines", sol::var(trussc::PrimitiveType::Lines),
        "LineStrip", sol::var(trussc::PrimitiveType::LineStrip),
        "Triangles", sol::var(trussc::PrimitiveType::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveType::TriangleStrip),
        "Quads", sol::var(trussc::PrimitiveType::Quads));
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::AudioRecordSettings::SampleFormat>("AudioRecordSettingsSampleFormat",
        sol::meta_function::equal_to, [](trussc::AudioRecordSettings::SampleFormat a, trussc::AudioRecordSettings::SampleFormat b){ return a == b; },
        "S16", sol::var(trussc::AudioRecordSettings::SampleFormat::S16),
        "F32", sol::var(trussc::AudioRecordSettings::SampleFormat::F32));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
