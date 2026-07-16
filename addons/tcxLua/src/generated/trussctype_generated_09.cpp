// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_09(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::App> t = lua->new_usertype<trussc::App>("App",
            sol::constructors<trussc::App()>(),
            sol::call_constructor, sol::constructors<trussc::App()>());
        t["setSize"] = &trussc::App::setSize;
        t["requestExit"] = &trussc::App::requestExit;
        t["isExitRequested"] = &trussc::App::isExitRequested;
        t["keyPressed"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyPressed(e); }, [](trussc::App& self, int key) { return self.keyPressed(key); });
        t["keyReleased"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyReleased(e); }, [](trussc::App& self, int key) { return self.keyReleased(key); });
        t["mousePressed"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mousePressed(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mousePressed(pos, button); });
        t["mouseReleased"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mouseReleased(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseReleased(pos, button); });
        t["mouseMoved"] = sol::overload([](trussc::App& self, const trussc::MouseMoveEventArgs & e) { return self.mouseMoved(e); }, [](trussc::App& self, trussc::Vec2 pos) { return self.mouseMoved(pos); });
        t["mouseDragged"] = sol::overload([](trussc::App& self, const trussc::MouseDragEventArgs & e) { return self.mouseDragged(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseDragged(pos, button); });
        t["mouseScrolled"] = sol::overload([](trussc::App& self, const trussc::ScrollEventArgs & e) { return self.mouseScrolled(e); }, [](trussc::App& self, trussc::Vec2 delta) { return self.mouseScrolled(delta); });
        t["touchPressed"] = &trussc::App::touchPressed;
        t["touchMoved"] = &trussc::App::touchMoved;
        t["touchReleased"] = &trussc::App::touchReleased;
        t["windowResized"] = &trussc::App::windowResized;
        t["filesDropped"] = &trussc::App::filesDropped;
        t["exit"] = &trussc::App::exit;
        t["audioOut"] = &trussc::App::audioOut;
        t["audioIn"] = &trussc::App::audioIn;
        t["handleKeyPressed"] = &trussc::App::handleKeyPressed;
        t["handleKeyReleased"] = &trussc::App::handleKeyReleased;
        t["handleMousePressed"] = &trussc::App::handleMousePressed;
        t["handleMouseReleased"] = &trussc::App::handleMouseReleased;
        t["handleMouseScrolled"] = &trussc::App::handleMouseScrolled;
        t["handleWindowResized"] = &trussc::App::handleWindowResized;
        t["handleUpdate"] = &trussc::App::handleUpdate;
        t["handleDraw"] = &trussc::App::handleDraw;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::TcpClient> t = lua->new_usertype<trussc::TcpClient>("TcpClient",
            sol::constructors<trussc::TcpClient()>(),
            sol::call_constructor, sol::constructors<trussc::TcpClient()>());
        t["onConnect"] = &trussc::TcpClient::onConnect;
        t["onReceive"] = &trussc::TcpClient::onReceive;
        t["onDisconnect"] = &trussc::TcpClient::onDisconnect;
        t["onError"] = &trussc::TcpClient::onError;
        t["connect"] = &trussc::TcpClient::connect;
        t["connectAsync"] = &trussc::TcpClient::connectAsync;
        t["disconnect"] = &trussc::TcpClient::disconnect;
        t["isConnected"] = &trussc::TcpClient::isConnected;
        t["send"] = sol::overload([](trussc::TcpClient& self, const std::vector<char> & data) { return self.send(data); }, [](trussc::TcpClient& self, const std::string & message) { return self.send(message); });
        t["setReceiveBufferSize"] = &trussc::TcpClient::setReceiveBufferSize;
        t["setBlocking"] = &trussc::TcpClient::setBlocking;
        t["setUseThread"] = &trussc::TcpClient::setUseThread;
        t["isUsingThread"] = &trussc::TcpClient::isUsingThread;
        t["processNetwork"] = &trussc::TcpClient::processNetwork;
        t["getRemoteHost"] = &trussc::TcpClient::getRemoteHost;
        t["getRemotePort"] = &trussc::TcpClient::getRemotePort;
    }
#endif
    {
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter",
            sol::constructors<trussc::FileWriter()>(),
            sol::call_constructor, sol::constructors<trussc::FileWriter()>());
        t["open"] = sol::overload([](trussc::FileWriter& self, const fs::path & path) { return self.open(path); }, [](trussc::FileWriter& self, const fs::path & path, bool append) { return self.open(path, append); });
        t["close"] = &trussc::FileWriter::close;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
        t["flush"] = &trussc::FileWriter::flush;
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
        sol::usertype<trussc::JsonReadReflector> t = lua->new_usertype<trussc::JsonReadReflector>("JsonReadReflector",
            sol::constructors<trussc::JsonReadReflector(trussc::Json)>(),
            sol::call_constructor, sol::constructors<trussc::JsonReadReflector(trussc::Json)>());
        t["applied"] = &trussc::JsonReadReflector::applied;
        t["skipped"] = &trussc::JsonReadReflector::skipped;
        t["readOnly"] = &trussc::JsonReadReflector::readOnly;
        t["unknownKeys"] = &trussc::JsonReadReflector::unknownKeys;
        t["endGroup"] = &trussc::JsonReadReflector::endGroup;
    }
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
    }
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::HeadlessSettings> t = lua->new_usertype<trussc::HeadlessSettings>("HeadlessSettings");
        t["targetFps"] = &trussc::HeadlessSettings::targetFps;
        t["setFps"] = &trussc::HeadlessSettings::setFps;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
