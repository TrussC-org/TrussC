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
        sol::usertype<trussc::Rect> t = lua->new_usertype<trussc::Rect>("Rect",
            sol::constructors<trussc::Rect(), trussc::Rect(float, float, float, float), trussc::Rect(const trussc::Vec2 &, float, float), trussc::Rect(const trussc::Vec3 &, float, float), trussc::Rect(float, float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Rect(), trussc::Rect(float, float, float, float), trussc::Rect(const trussc::Vec2 &, float, float), trussc::Rect(const trussc::Vec3 &, float, float), trussc::Rect(float, float, float, float, float)>());
        t["x"] = &trussc::Rect::x;
        t["y"] = &trussc::Rect::y;
        t["width"] = &trussc::Rect::width;
        t["height"] = &trussc::Rect::height;
        t["set"] = sol::overload([](trussc::Rect& self, float x_, float y_, float w_, float h_) -> decltype(auto) { return self.set(x_, y_, w_, h_); }, [](trussc::Rect& self, const trussc::Vec2 & pos, float w_, float h_) -> decltype(auto) { return self.set(pos, w_, h_); });
        t["getRight"] = &trussc::Rect::getRight;
        t["getBottom"] = &trussc::Rect::getBottom;
        t["getCenter"] = &trussc::Rect::getCenter;
        t["getCenterX"] = &trussc::Rect::getCenterX;
        t["getCenterY"] = &trussc::Rect::getCenterY;
        t["contains"] = &trussc::Rect::contains;
        t["intersects"] = &trussc::Rect::intersects;
    }
    {
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger",
            sol::constructors<trussc::Logger()>(),
            sol::call_constructor, sol::constructors<trussc::Logger()>());
        t["onLog"] = &trussc::Logger::onLog;
        t["log"] = &trussc::Logger::log;
        t["setConsoleLogLevel"] = &trussc::Logger::setConsoleLogLevel;
        t["getConsoleLogLevel"] = &trussc::Logger::getConsoleLogLevel;
        t["setLogFile"] = &trussc::Logger::setLogFile;
        t["closeFile"] = &trussc::Logger::closeFile;
        t["setFileLogLevel"] = &trussc::Logger::setFileLogLevel;
        t["getFileLogLevel"] = &trussc::Logger::getFileLogLevel;
        t["getLogFilePath"] = &trussc::Logger::getLogFilePath;
        t["isFileOpen"] = &trussc::Logger::isFileOpen;
    }
    lua->new_usertype<trussc::PrimitiveMode>("PrimitiveMode",
        sol::meta_function::equal_to, [](trussc::PrimitiveMode a, trussc::PrimitiveMode b){ return a == b; },
        "Triangles", sol::var(trussc::PrimitiveMode::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveMode::TriangleStrip),
        "TriangleFan", sol::var(trussc::PrimitiveMode::TriangleFan),
        "Lines", sol::var(trussc::PrimitiveMode::Lines),
        "LineStrip", sol::var(trussc::PrimitiveMode::LineStrip),
        "LineLoop", sol::var(trussc::PrimitiveMode::LineLoop),
        "Points", sol::var(trussc::PrimitiveMode::Points));
    {
        sol::usertype<trussc::GraphicsBackend> t = lua->new_usertype<trussc::GraphicsBackend>("GraphicsBackend");
        t["isWebGPU"] = &trussc::GraphicsBackend::isWebGPU;
        t["isWebGL2"] = &trussc::GraphicsBackend::isWebGL2;
        t["isMetal"] = &trussc::GraphicsBackend::isMetal;
        t["isD3D11"] = &trussc::GraphicsBackend::isD3D11;
        t["isVulkan"] = &trussc::GraphicsBackend::isVulkan;
        t["isOpenGL"] = &trussc::GraphicsBackend::isOpenGL;
        t["name"] = &trussc::GraphicsBackend::name;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    lua->new_usertype<trussc::StrokeCap>("StrokeCap",
        sol::meta_function::equal_to, [](trussc::StrokeCap a, trussc::StrokeCap b){ return a == b; },
        "Butt", sol::var(trussc::StrokeCap::Butt),
        "Round", sol::var(trussc::StrokeCap::Round),
        "Square", sol::var(trussc::StrokeCap::Square));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
