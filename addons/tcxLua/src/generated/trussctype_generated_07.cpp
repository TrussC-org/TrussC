// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_07(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::UdpSocket> t = lua->new_usertype<trussc::UdpSocket>("UdpSocket",
            sol::constructors<trussc::UdpSocket()>(),
            sol::call_constructor, sol::constructors<trussc::UdpSocket()>());
        t["onReceive"] = &trussc::UdpSocket::onReceive;
        t["onError"] = &trussc::UdpSocket::onError;
        t["create"] = &trussc::UdpSocket::create;
        t["bind"] = sol::overload([](trussc::UdpSocket& self, int port) { return self.bind(port); }, [](trussc::UdpSocket& self, int port, bool startReceiving) { return self.bind(port, startReceiving); });
        t["connect"] = &trussc::UdpSocket::connect;
        t["close"] = &trussc::UdpSocket::close;
        t["sendTo"] = [](trussc::UdpSocket& self, const std::string & host, int port, const std::string & message) { return self.sendTo(host, port, message); };
        t["send"] = [](trussc::UdpSocket& self, const std::string & message) { return self.send(message); };
        t["startReceiving"] = &trussc::UdpSocket::startReceiving;
        t["stopReceiving"] = &trussc::UdpSocket::stopReceiving;
        t["isReceiving"] = &trussc::UdpSocket::isReceiving;
        t["setNonBlocking"] = &trussc::UdpSocket::setNonBlocking;
        t["setBroadcast"] = &trussc::UdpSocket::setBroadcast;
        t["setReuseAddress"] = &trussc::UdpSocket::setReuseAddress;
        t["setReusePort"] = &trussc::UdpSocket::setReusePort;
        t["joinMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.joinMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.joinMulticastGroup(groupAddr, interfaceAddr); });
        t["leaveMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.leaveMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.leaveMulticastGroup(groupAddr, interfaceAddr); });
        t["setMulticastTTL"] = &trussc::UdpSocket::setMulticastTTL;
        t["setMulticastLoopback"] = &trussc::UdpSocket::setMulticastLoopback;
        t["setMulticastInterface"] = &trussc::UdpSocket::setMulticastInterface;
        t["setReceiveBufferSize"] = &trussc::UdpSocket::setReceiveBufferSize;
        t["setSendBufferSize"] = &trussc::UdpSocket::setSendBufferSize;
        t["setReceiveTimeout"] = &trussc::UdpSocket::setReceiveTimeout;
        t["setUseThread"] = &trussc::UdpSocket::setUseThread;
        t["getLocalPort"] = &trussc::UdpSocket::getLocalPort;
        t["processNetwork"] = &trussc::UdpSocket::processNetwork;
        t["isValid"] = &trussc::UdpSocket::isValid;
        t["getConnectedHost"] = &trussc::UdpSocket::getConnectedHost;
        t["getConnectedPort"] = &trussc::UdpSocket::getConnectedPort;
    }
#endif
    {
        sol::usertype<trussc::Quaternion> t = lua->new_usertype<trussc::Quaternion>("Quaternion",
            sol::constructors<trussc::Quaternion(), trussc::Quaternion(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Quaternion(), trussc::Quaternion(float, float, float, float)>(),
            sol::meta_function::equal_to, [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; },
            sol::meta_function::multiplication, [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; });
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["identity"] = &trussc::Quaternion::identity;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["slerp"] = &trussc::Quaternion::slerp;
    }
    {
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::BuildInfo> t = lua->new_usertype<trussc::BuildInfo>("BuildInfo");
        t["date"] = &trussc::BuildInfo::date;
        t["time"] = &trussc::BuildInfo::time;
        t["dateTime"] = &trussc::BuildInfo::dateTime;
        t["timestamp"] = &trussc::BuildInfo::timestamp;
        t["year"] = &trussc::BuildInfo::year;
        t["month"] = &trussc::BuildInfo::month;
        t["day"] = &trussc::BuildInfo::day;
        t["hour"] = &trussc::BuildInfo::hour;
        t["minute"] = &trussc::BuildInfo::minute;
        t["second"] = &trussc::BuildInfo::second;
    }
    lua->new_usertype<trussc::LoadError>("LoadError",
        sol::meta_function::equal_to, [](trussc::LoadError a, trussc::LoadError b){ return a == b; },
        "None", sol::var(trussc::LoadError::None),
        "FileNotFound", sol::var(trussc::LoadError::FileNotFound),
        "UnsupportedFormat", sol::var(trussc::LoadError::UnsupportedFormat),
        "DecodeFailed", sol::var(trussc::LoadError::DecodeFailed),
        "Unknown", sol::var(trussc::LoadError::Unknown));
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
    }
    lua->new_usertype<trussc::EaseMode>("EaseMode",
        sol::meta_function::equal_to, [](trussc::EaseMode a, trussc::EaseMode b){ return a == b; },
        "In", sol::var(trussc::EaseMode::In),
        "Out", sol::var(trussc::EaseMode::Out),
        "InOut", sol::var(trussc::EaseMode::InOut));
    lua->new_usertype<trussc::Deliver>("Deliver",
        sol::meta_function::equal_to, [](trussc::Deliver a, trussc::Deliver b){ return a == b; },
        "Inline", sol::var(trussc::Deliver::Inline),
        "Main", sol::var(trussc::Deliver::Main));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
