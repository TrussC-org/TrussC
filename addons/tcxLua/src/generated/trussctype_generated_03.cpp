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
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
        t["duration"] = &trussc::VideoRecordSettings::duration;
        t["audio"] = &trussc::VideoRecordSettings::audio;
        t["audioBitrate"] = &trussc::VideoRecordSettings::audioBitrate;
        t["audioSampleRate"] = &trussc::VideoRecordSettings::audioSampleRate;
        t["audioChannels"] = &trussc::VideoRecordSettings::audioChannels;
    }
    {
        sol::usertype<trussc::ScrollBar> t = lua->new_usertype<trussc::ScrollBar>("ScrollBar");
        t["getBarColor"] = &trussc::ScrollBar::getBarColor;
        t["setBarColor"] = &trussc::ScrollBar::setBarColor;
        t["getBarWidth"] = &trussc::ScrollBar::getBarWidth;
        t["setBarWidth"] = &trussc::ScrollBar::setBarWidth;
        t["getMargin"] = &trussc::ScrollBar::getMargin;
        t["setMargin"] = &trussc::ScrollBar::setMargin;
        t["getOffset"] = &trussc::ScrollBar::getOffset;
        t["updateFromContainer"] = &trussc::ScrollBar::updateFromContainer;
    }
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    {
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream",
            sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>(),
            sol::call_constructor, sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>());
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
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
