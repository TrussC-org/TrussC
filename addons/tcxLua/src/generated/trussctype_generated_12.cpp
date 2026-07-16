// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_12(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Vec3> t = lua->new_usertype<trussc::Vec3>("Vec3",
            sol::constructors<trussc::Vec3(), trussc::Vec3(float, float, float), trussc::Vec3(float), trussc::Vec3(const trussc::Vec2 &), trussc::Vec3(const trussc::Vec2 &, float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec3(), trussc::Vec3(float, float, float), trussc::Vec3(float), trussc::Vec3(const trussc::Vec2 &), trussc::Vec3(const trussc::Vec2 &, float)>(),
            sol::meta_function::index, [](const trussc::Vec3& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec3& a){ return -a; },
            sol::meta_function::multiplication, sol::overload([](const trussc::Vec3& a, float b){ return a * b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a * b; }),
            sol::meta_function::division, sol::overload([](const trussc::Vec3& a, float b){ return a / b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a / b; }),
            sol::meta_function::equal_to, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a == b; });
        t["x"] = &trussc::Vec3::x;
        t["y"] = &trussc::Vec3::y;
        t["z"] = &trussc::Vec3::z;
        t["set"] = sol::overload([](trussc::Vec3& self, float x_, float y_, float z_) -> decltype(auto) { return self.set(x_, y_, z_); }, [](trussc::Vec3& self, const trussc::Vec3 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec3::length;
        t["lengthSquared"] = &trussc::Vec3::lengthSquared;
        t["normalized"] = &trussc::Vec3::normalized;
        t["normalize"] = &trussc::Vec3::normalize;
        t["limit"] = &trussc::Vec3::limit;
        t["dot"] = &trussc::Vec3::dot;
        t["cross"] = &trussc::Vec3::cross;
        t["distance"] = &trussc::Vec3::distance;
        t["distanceSquared"] = &trussc::Vec3::distanceSquared;
        t["lerp"] = &trussc::Vec3::lerp;
        t["reflected"] = &trussc::Vec3::reflected;
        t["xy"] = &trussc::Vec3::xy;
    }
    {
        sol::usertype<trussc::ScrollContainer> t = lua->new_usertype<trussc::ScrollContainer>("ScrollContainer",
            sol::constructors<trussc::ScrollContainer()>(),
            sol::call_constructor, sol::constructors<trussc::ScrollContainer()>());
        t["setContent"] = &trussc::ScrollContainer::setContent;
        t["getContent"] = &trussc::ScrollContainer::getContent;
        t["getContentRect"] = &trussc::ScrollContainer::getContentRect;
        t["getScrollX"] = &trussc::ScrollContainer::getScrollX;
        t["getScrollY"] = &trussc::ScrollContainer::getScrollY;
        t["getScroll"] = &trussc::ScrollContainer::getScroll;
        t["setScrollX"] = &trussc::ScrollContainer::setScrollX;
        t["setScrollY"] = &trussc::ScrollContainer::setScrollY;
        t["setScroll"] = sol::overload([](trussc::ScrollContainer& self, float x, float y) { return self.setScroll(x, y); }, [](trussc::ScrollContainer& self, trussc::Vec2 pos) { return self.setScroll(pos); });
        t["getMaxScrollX"] = &trussc::ScrollContainer::getMaxScrollX;
        t["getMaxScrollY"] = &trussc::ScrollContainer::getMaxScrollY;
        t["updateScrollBounds"] = &trussc::ScrollContainer::updateScrollBounds;
        t["isHorizontalScrollEnabled"] = &trussc::ScrollContainer::isHorizontalScrollEnabled;
        t["isVerticalScrollEnabled"] = &trussc::ScrollContainer::isVerticalScrollEnabled;
        t["setHorizontalScrollEnabled"] = &trussc::ScrollContainer::setHorizontalScrollEnabled;
        t["setVerticalScrollEnabled"] = &trussc::ScrollContainer::setVerticalScrollEnabled;
        t["getScrollSpeed"] = &trussc::ScrollContainer::getScrollSpeed;
        t["setScrollSpeed"] = &trussc::ScrollContainer::setScrollSpeed;
    }
    {
        sol::usertype<trussc::ChipSoundNote> t = lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote",
            sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>());
        t["wave"] = &trussc::ChipSoundNote::wave;
        t["hz"] = &trussc::ChipSoundNote::hz;
        t["volume"] = &trussc::ChipSoundNote::volume;
        t["duration"] = &trussc::ChipSoundNote::duration;
        t["attack"] = &trussc::ChipSoundNote::attack;
        t["decay"] = &trussc::ChipSoundNote::decay;
        t["sustain"] = &trussc::ChipSoundNote::sustain;
        t["release"] = &trussc::ChipSoundNote::release;
        t["build"] = &trussc::ChipSoundNote::build;
        t["generateBuffer"] = &trussc::ChipSoundNote::generateBuffer;
        t["getTotalDuration"] = &trussc::ChipSoundNote::getTotalDuration;
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
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
        t["altitude"] = &trussc::Location::altitude;
        t["accuracy"] = &trussc::Location::accuracy;
    }
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
