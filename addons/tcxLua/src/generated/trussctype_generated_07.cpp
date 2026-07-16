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
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
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
        sol::usertype<trussc::IVec2> t = lua->new_usertype<trussc::IVec2>("IVec2",
            sol::constructors<trussc::IVec2(), trussc::IVec2(int, int), trussc::IVec2(int)>(),
            sol::call_constructor, sol::constructors<trussc::IVec2(), trussc::IVec2(int, int), trussc::IVec2(int)>(),
            sol::meta_function::addition, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::IVec2& a){ return -a; },
            sol::meta_function::multiplication, [](const trussc::IVec2& a, int b){ return a * b; },
            sol::meta_function::equal_to, [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; });
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
    }
    {
        sol::usertype<trussc::MouseEventArgs> t = lua->new_usertype<trussc::MouseEventArgs>("MouseEventArgs");
        t["x"] = &trussc::MouseEventArgs::x;
        t["y"] = &trussc::MouseEventArgs::y;
        t["button"] = &trussc::MouseEventArgs::button;
        t["shift"] = &trussc::MouseEventArgs::shift;
        t["ctrl"] = &trussc::MouseEventArgs::ctrl;
        t["alt"] = &trussc::MouseEventArgs::alt;
        t["super"] = &trussc::MouseEventArgs::super;
        t["pos"] = &trussc::MouseEventArgs::pos;
        t["globalPos"] = &trussc::MouseEventArgs::globalPos;
        t["consumed"] = &trussc::MouseEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["view"] = &trussc::CameraContext::view;
        t["projection"] = &trussc::CameraContext::projection;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["pickable"] = &trussc::CameraContext::pickable;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
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
