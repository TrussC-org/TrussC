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
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder",
            sol::constructors<trussc::ScreenRecorder()>(),
            sol::call_constructor, sol::constructors<trussc::ScreenRecorder()>());
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
        t["stop"] = &trussc::ScreenRecorder::stop;
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
        t["getPath"] = &trussc::ScreenRecorder::getPath;
        t["writer"] = &trussc::ScreenRecorder::writer;
    }
#endif
    {
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH",
            sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLCH::L;
        t["C"] = &trussc::ColorOKLCH::C;
        t["H"] = &trussc::ColorOKLCH::H;
        t["alpha"] = &trussc::ColorOKLCH::alpha;
        t["toOKLab"] = &trussc::ColorOKLCH::toOKLab;
        t["toLinear"] = &trussc::ColorOKLCH::toLinear;
        t["toRGB"] = &trussc::ColorOKLCH::toRGB;
        t["toHSB"] = &trussc::ColorOKLCH::toHSB;
        t["lerp"] = sol::overload([](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t) { return self.lerp(target, t); }, [](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
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
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isApple"] = &trussc::Platform::isApple;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["name"] = &trussc::Platform::name;
    }
    {
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs",
            sol::constructors<trussc::LogEventArgs(trussc::LogLevel, const std::string &)>(),
            sol::call_constructor, sol::constructors<trussc::LogEventArgs(trussc::LogLevel, const std::string &)>());
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    lua->new_usertype<trussc::ThermalState>("ThermalState",
        sol::meta_function::equal_to, [](trussc::ThermalState a, trussc::ThermalState b){ return a == b; },
        "Nominal", sol::var(trussc::ThermalState::Nominal),
        "Fair", sol::var(trussc::ThermalState::Fair),
        "Serious", sol::var(trussc::ThermalState::Serious),
        "Critical", sol::var(trussc::ThermalState::Critical));
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::SoundSource::Kind>("SoundSourceKind",
        sol::meta_function::equal_to, [](trussc::SoundSource::Kind a, trussc::SoundSource::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundSource::Kind::Eager),
        "Stream", sol::var(trussc::SoundSource::Kind::Stream));
    {
        sol::usertype<trussc::SendResult> t = lua->new_usertype<trussc::SendResult>("SendResult");
        t["error"] = &trussc::SendResult::error;
        t["id"] = &trussc::SendResult::id;
        t["ok"] = &trussc::SendResult::ok;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
