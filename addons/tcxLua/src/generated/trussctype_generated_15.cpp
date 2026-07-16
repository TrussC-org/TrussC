// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter",
            sol::constructors<trussc::VideoWriter()>(),
            sol::call_constructor, sol::constructors<trussc::VideoWriter()>());
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
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
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
        t["build"] = &trussc::ChipSoundBundle::build;
    }
    lua->new_usertype<trussc::Beep>("Beep",
        sol::meta_function::equal_to, [](trussc::Beep a, trussc::Beep b){ return a == b; },
        "ping", sol::var(trussc::Beep::ping),
        "success", sol::var(trussc::Beep::success),
        "complete", sol::var(trussc::Beep::complete),
        "coin", sol::var(trussc::Beep::coin),
        "error", sol::var(trussc::Beep::error),
        "warning", sol::var(trussc::Beep::warning),
        "cancel", sol::var(trussc::Beep::cancel),
        "click", sol::var(trussc::Beep::click),
        "typing", sol::var(trussc::Beep::typing),
        "notify", sol::var(trussc::Beep::notify),
        "sweep", sol::var(trussc::Beep::sweep));
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::MouseButton>("MouseButton",
        sol::meta_function::equal_to, [](trussc::MouseButton a, trussc::MouseButton b){ return a == b; },
        "Left", sol::var(trussc::MouseButton::Left),
        "Right", sol::var(trussc::MouseButton::Right),
        "Middle", sol::var(trussc::MouseButton::Middle),
        "None", sol::var(trussc::MouseButton::None));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::GrabberFrame> t = lua->new_usertype<trussc::GrabberFrame>("GrabberFrame");
        t["pixels"] = &trussc::GrabberFrame::pixels;
        t["timestampUs"] = &trussc::GrabberFrame::timestampUs;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
