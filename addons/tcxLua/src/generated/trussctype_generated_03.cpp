// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_03(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::TweenMod> t = lua->new_usertype<trussc::TweenMod>("TweenMod",
            sol::constructors<trussc::TweenMod()>(),
            sol::call_constructor, sol::constructors<trussc::TweenMod()>());
        t["complete"] = &trussc::TweenMod::complete;
        t["moveTo"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveTo(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveTo(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveTo(pos); }, [](trussc::TweenMod& self, const trussc::Vec2 & pos) -> decltype(auto) { return self.moveTo(pos); });
        t["moveBy"] = sol::overload([](trussc::TweenMod& self, float dx, float dy) -> decltype(auto) { return self.moveBy(dx, dy); }, [](trussc::TweenMod& self, float dx, float dy, float dz) -> decltype(auto) { return self.moveBy(dx, dy, dz); }, [](trussc::TweenMod& self, const trussc::Vec3 & delta) -> decltype(auto) { return self.moveBy(delta); }, [](trussc::TweenMod& self, const trussc::Vec2 & delta) -> decltype(auto) { return self.moveBy(delta); });
        t["moveFrom"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveFrom(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveFrom(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveFrom(pos); });
        t["scaleTo"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleTo(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleTo(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleTo(sx, sy, sz); }, [](trussc::TweenMod& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scaleTo(s); });
        t["scaleBy"] = sol::overload([](trussc::TweenMod& self, float factor) -> decltype(auto) { return self.scaleBy(factor); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleBy(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleBy(sx, sy, sz); });
        t["scaleFrom"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleFrom(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleFrom(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleFrom(sx, sy, sz); });
        t["rotateTo"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateTo(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateTo(q); });
        t["rotateBy"] = &trussc::TweenMod::rotateBy;
        t["rotateFrom"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateFrom(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateFrom(q); });
        t["rotateXTo"] = &trussc::TweenMod::rotateXTo;
        t["rotateXBy"] = &trussc::TweenMod::rotateXBy;
        t["rotateYTo"] = &trussc::TweenMod::rotateYTo;
        t["rotateYBy"] = &trussc::TweenMod::rotateYBy;
        t["rotateZTo"] = &trussc::TweenMod::rotateZTo;
        t["rotateZBy"] = &trussc::TweenMod::rotateZBy;
        t["rotateXFrom"] = &trussc::TweenMod::rotateXFrom;
        t["rotateYFrom"] = &trussc::TweenMod::rotateYFrom;
        t["duration"] = &trussc::TweenMod::duration;
        t["ease"] = sol::overload([](trussc::TweenMod& self, trussc::EaseType type) -> decltype(auto) { return self.ease(type); }, [](trussc::TweenMod& self, trussc::EaseType type, trussc::EaseMode mode) -> decltype(auto) { return self.ease(type, mode); }, [](trussc::TweenMod& self, trussc::EaseFunction fn) -> decltype(auto) { return self.ease(fn); }, [](trussc::TweenMod& self, trussc::EaseFunction fn, trussc::EaseMode mode) -> decltype(auto) { return self.ease(fn, mode); });
        t["delay"] = &trussc::TweenMod::delay;
        t["start"] = &trussc::TweenMod::start;
        t["pause"] = &trussc::TweenMod::pause;
        t["resume"] = &trussc::TweenMod::resume;
        t["reset"] = &trussc::TweenMod::reset;
        t["isPlaying"] = &trussc::TweenMod::isPlaying;
        t["isComplete"] = &trussc::TweenMod::isComplete;
        t["getProgress"] = &trussc::TweenMod::getProgress;
        t["getDuration"] = &trussc::TweenMod::getDuration;
        t["getDelay"] = &trussc::TweenMod::getDelay;
        t["getEaseType"] = &trussc::TweenMod::getEaseType;
        t["getEaseMode"] = &trussc::TweenMod::getEaseMode;
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
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["updateFps"] = &trussc::FpsSettings::updateFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["synced"] = &trussc::FpsSettings::synced;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
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
