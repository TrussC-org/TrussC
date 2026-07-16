// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_14(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter",
            sol::constructors<trussc::VideoWriter()>(),
            sol::call_constructor, sol::constructors<trussc::VideoWriter()>());
        t["open"] = sol::overload([](trussc::VideoWriter& self, const std::string & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const std::string & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
        t["close"] = &trussc::VideoWriter::close;
        t["isOpen"] = &trussc::VideoWriter::isOpen;
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
        t["getWidth"] = &trussc::VideoWriter::getWidth;
        t["getHeight"] = &trussc::VideoWriter::getHeight;
        t["getFps"] = &trussc::VideoWriter::getFps;
        t["getPath"] = &trussc::VideoWriter::getPath;
        t["getSettings"] = &trussc::VideoWriter::getSettings;
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        sol::usertype<trussc::TcpServer> t = lua->new_usertype<trussc::TcpServer>("TcpServer",
            sol::constructors<trussc::TcpServer()>(),
            sol::call_constructor, sol::constructors<trussc::TcpServer()>());
        t["onClientConnect"] = &trussc::TcpServer::onClientConnect;
        t["onReceive"] = &trussc::TcpServer::onReceive;
        t["onClientDisconnect"] = &trussc::TcpServer::onClientDisconnect;
        t["onError"] = &trussc::TcpServer::onError;
        t["start"] = sol::overload([](trussc::TcpServer& self, int port) { return self.start(port); }, [](trussc::TcpServer& self, int port, int maxClients) { return self.start(port, maxClients); });
        t["stop"] = &trussc::TcpServer::stop;
        t["isRunning"] = &trussc::TcpServer::isRunning;
        t["disconnectClient"] = &trussc::TcpServer::disconnectClient;
        t["disconnectAllClients"] = &trussc::TcpServer::disconnectAllClients;
        t["getClientCount"] = &trussc::TcpServer::getClientCount;
        t["getClientIds"] = &trussc::TcpServer::getClientIds;
        t["getClient"] = &trussc::TcpServer::getClient;
        t["send"] = sol::overload([](trussc::TcpServer& self, int clientId, const std::vector<char> & data) { return self.send(clientId, data); }, [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.send(clientId, message); });
        t["broadcast"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcast(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcast(message); });
        t["setReceiveBufferSize"] = &trussc::TcpServer::setReceiveBufferSize;
        t["getPort"] = &trussc::TcpServer::getPort;
    }
#endif
    {
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3",
            sol::constructors<trussc::IVec3(), trussc::IVec3(int, int, int), trussc::IVec3(int), trussc::IVec3(const trussc::IVec2 &), trussc::IVec3(const trussc::IVec2 &, int)>(),
            sol::call_constructor, sol::constructors<trussc::IVec3(), trussc::IVec3(int, int, int), trussc::IVec3(int), trussc::IVec3(const trussc::IVec2 &), trussc::IVec3(const trussc::IVec2 &, int)>(),
            sol::meta_function::addition, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::IVec3& a){ return -a; },
            sol::meta_function::multiplication, [](const trussc::IVec3& a, int b){ return a * b; },
            sol::meta_function::equal_to, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; });
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
    }
    {
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab",
            sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["b"] = &trussc::ColorOKLab::b;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["lerp"] = &trussc::ColorOKLab::lerp;
    }
    {
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["volume"] = &trussc::PlayingSound::volume;
        t["pan"] = &trussc::PlayingSound::pan;
        t["speed"] = &trussc::PlayingSound::speed;
        t["loop"] = &trussc::PlayingSound::loop;
        t["playing"] = &trussc::PlayingSound::playing;
        t["paused"] = &trussc::PlayingSound::paused;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
    }
    {
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    {
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
        t["id"] = &trussc::TouchEventArgs::id;
    }
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
