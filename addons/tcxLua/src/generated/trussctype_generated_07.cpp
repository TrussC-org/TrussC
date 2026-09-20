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
        sol::usertype<trussc::IesProfile> t = lua->new_usertype<trussc::IesProfile>("IesProfile",
            sol::constructors<trussc::IesProfile()>(),
            sol::call_constructor, sol::constructors<trussc::IesProfile()>());
        t["load"] = &trussc::IesProfile::load;
        t["loadFromString"] = &trussc::IesProfile::loadFromString;
        t["isLoaded"] = &trussc::IesProfile::isLoaded;
        t["getMaxVerticalAngle"] = &trussc::IesProfile::getMaxVerticalAngle;
        t["getMaxCandela"] = &trussc::IesProfile::getMaxCandela;
        t["getTextureWidth"] = &trussc::IesProfile::getTextureWidth;
        t["getView"] = &trussc::IesProfile::getView;
        t["getSampler"] = &trussc::IesProfile::getSampler;
    }
    {
        sol::usertype<trussc::AudioDeviceChangedArgs> t = lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
    }
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["key"] = &trussc::KeyEventArgs::key;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["super"] = &trussc::KeyEventArgs::super;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
