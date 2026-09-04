// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_10(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        sol::usertype<trussc::TcpServer> t = lua->new_usertype<trussc::TcpServer>("TcpServer",
            sol::constructors<trussc::TcpServer()>(),
            sol::call_constructor, sol::constructors<trussc::TcpServer()>());
        t["onClientConnect"] = &trussc::TcpServer::onClientConnect;
        t["onReceive"] = &trussc::TcpServer::onReceive;
        t["onClientDisconnect"] = &trussc::TcpServer::onClientDisconnect;
        t["onError"] = &trussc::TcpServer::onError;
        t["onSendComplete"] = &trussc::TcpServer::onSendComplete;
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
        t["sendAsync"] = [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.sendAsync(clientId, message); };
        t["broadcastAsync"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcastAsync(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcastAsync(message); });
        t["setReceiveBufferSize"] = &trussc::TcpServer::setReceiveBufferSize;
        t["setSendTimeout"] = &trussc::TcpServer::setSendTimeout;
        t["setSendAsyncBufferSize"] = &trussc::TcpServer::setSendAsyncBufferSize;
        t["getSendAsyncBufferSize"] = &trussc::TcpServer::getSendAsyncBufferSize;
        t["getSendAsyncPendingBytes"] = &trussc::TcpServer::getSendAsyncPendingBytes;
        t["getPort"] = &trussc::TcpServer::getPort;
    }
#endif
    lua->new_usertype<trussc::Cursor>("Cursor",
        sol::meta_function::equal_to, [](trussc::Cursor a, trussc::Cursor b){ return a == b; },
        "Default", sol::var(trussc::Cursor::Default),
        "Arrow", sol::var(trussc::Cursor::Arrow),
        "IBeam", sol::var(trussc::Cursor::IBeam),
        "Crosshair", sol::var(trussc::Cursor::Crosshair),
        "Hand", sol::var(trussc::Cursor::Hand),
        "ResizeEW", sol::var(trussc::Cursor::ResizeEW),
        "ResizeNS", sol::var(trussc::Cursor::ResizeNS),
        "ResizeNWSE", sol::var(trussc::Cursor::ResizeNWSE),
        "ResizeNESW", sol::var(trussc::Cursor::ResizeNESW),
        "ResizeAll", sol::var(trussc::Cursor::ResizeAll),
        "NotAllowed", sol::var(trussc::Cursor::NotAllowed),
        "Custom0", sol::var(trussc::Cursor::Custom0),
        "Custom1", sol::var(trussc::Cursor::Custom1),
        "Custom2", sol::var(trussc::Cursor::Custom2),
        "Custom3", sol::var(trussc::Cursor::Custom3),
        "Custom4", sol::var(trussc::Cursor::Custom4),
        "Custom5", sol::var(trussc::Cursor::Custom5),
        "Custom6", sol::var(trussc::Cursor::Custom6),
        "Custom7", sol::var(trussc::Cursor::Custom7),
        "Custom8", sol::var(trussc::Cursor::Custom8),
        "Custom9", sol::var(trussc::Cursor::Custom9),
        "Custom10", sol::var(trussc::Cursor::Custom10),
        "Custom11", sol::var(trussc::Cursor::Custom11),
        "Custom12", sol::var(trussc::Cursor::Custom12),
        "Custom13", sol::var(trussc::Cursor::Custom13),
        "Custom14", sol::var(trussc::Cursor::Custom14),
        "Custom15", sol::var(trussc::Cursor::Custom15));
    {
        sol::usertype<trussc::HasTexture> t = lua->new_usertype<trussc::HasTexture>("HasTexture");
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["save"] = &trussc::HasTexture::save;
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
    {
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream",
            sol::constructors<trussc::SoundStream()>(),
            sol::call_constructor, sol::constructors<trussc::SoundStream()>());
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
    }
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    {
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener",
            sol::constructors<trussc::EventListener()>(),
            sol::call_constructor, sol::constructors<trussc::EventListener()>());
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    lua->new_usertype<trussc::LayoutDirection>("LayoutDirection",
        sol::meta_function::equal_to, [](trussc::LayoutDirection a, trussc::LayoutDirection b){ return a == b; },
        "Vertical", sol::var(trussc::LayoutDirection::Vertical),
        "Horizontal", sol::var(trussc::LayoutDirection::Horizontal));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
