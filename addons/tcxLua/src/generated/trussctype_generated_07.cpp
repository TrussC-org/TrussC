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
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["name"] = &trussc::NetworkInterface::name;
        t["address"] = &trussc::NetworkInterface::address;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
    }
    {
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader",
            sol::constructors<trussc::FileReader()>(),
            sol::call_constructor, sol::constructors<trussc::FileReader()>());
        t["open"] = &trussc::FileReader::open;
        t["close"] = &trussc::FileReader::close;
        t["isOpen"] = &trussc::FileReader::isOpen;
        t["eof"] = &trussc::FileReader::eof;
        t["readLine"] = [](trussc::FileReader& self) { return self.readLine(); };
        t["readChar"] = &trussc::FileReader::readChar;
        t["seek"] = &trussc::FileReader::seek;
        t["tell"] = &trussc::FileReader::tell;
        t["remaining"] = &trussc::FileReader::remaining;
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
        sol::usertype<trussc::AudioSettings> t = lua->new_usertype<trussc::AudioSettings>("AudioSettings");
        t["sampleRate"] = &trussc::AudioSettings::sampleRate;
        t["channels"] = &trussc::AudioSettings::channels;
        t["bufferSize"] = &trussc::AudioSettings::bufferSize;
        t["maxPolyphony"] = &trussc::AudioSettings::maxPolyphony;
        t["deviceName"] = &trussc::AudioSettings::deviceName;
    }
    {
        sol::usertype<trussc::TcpClientConnectEventArgs> t = lua->new_usertype<trussc::TcpClientConnectEventArgs>("TcpClientConnectEventArgs");
        t["clientId"] = &trussc::TcpClientConnectEventArgs::clientId;
        t["host"] = &trussc::TcpClientConnectEventArgs::host;
        t["port"] = &trussc::TcpClientConnectEventArgs::port;
    }
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["name"] = &trussc::AudioDeviceInfo::name;
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
