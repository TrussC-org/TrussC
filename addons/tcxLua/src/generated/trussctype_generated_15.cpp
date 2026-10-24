// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
// Abstract bases (e.g. VideoPlayerBase) expose members but cannot be constructed.
// An empty guarded constructor list must not enable sol2's automatic default ctor.
namespace {
template<typename Owner, typename... Signatures>
auto tcxLuaConstructors() {
    if constexpr (std::is_abstract_v<Owner> || sizeof...(Signatures) == 0) return sol::no_constructor;
    else return sol::constructors<Signatures...>();
}
}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Font
            , trussc::Font()
        >();
        sol::usertype<trussc::Font> t = lua->new_usertype<trussc::Font>("Font", constructors, sol::call_constructor, constructors);
        t["onLoad"] = &trussc::Font::onLoad;
        t["clearAtlas"] = &trussc::Font::clearAtlas;
        t["drawString"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y) { return self.drawString(text, x, y); }, [](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.drawString(text, x, y, h, v); });
        t["enableWrap"] = &trussc::Font::enableWrap;
        t["forEachGlyph"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, h, v, visitor); }, [](trussc::Font& self, const std::string & text, float x, float y, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, visitor); });
        t["getAlignH"] = &trussc::Font::getAlignH;
        t["getAlignV"] = &trussc::Font::getAlignV;
        t["getAscent"] = &trussc::Font::getAscent;
        t["getAtlasCount"] = &trussc::Font::getAtlasCount;
        t["getAtlasMemoryUsage"] = &trussc::Font::getAtlasMemoryUsage;
        t["getBBox"] = &trussc::Font::getBBox;
        t["getDefaultLineHeight"] = &trussc::Font::getDefaultLineHeight;
        t["getDescent"] = &trussc::Font::getDescent;
        t["getGlyphPath"] = &trussc::Font::getGlyphPath;
        t["getGridFit"] = &trussc::Font::getGridFit;
        t["getHangingPunctuation"] = &trussc::Font::getHangingPunctuation;
        t["getHeight"] = &trussc::Font::getHeight;
        t["getKinsoku"] = &trussc::Font::getKinsoku;
        t["getLatinHyphenation"] = &trussc::Font::getLatinHyphenation;
        t["getLineHeight"] = &trussc::Font::getLineHeight;
        t["getLoadedGlyphCount"] = &trussc::Font::getLoadedGlyphCount;
        t["getMaxLineLength"] = &trussc::Font::getMaxLineLength;
        t["getMemoryUsage"] = &trussc::Font::getMemoryUsage;
        t["getMipmaps"] = &trussc::Font::getMipmaps;
        t["getOversampling"] = &trussc::Font::getOversampling;
        t["getSampler"] = &trussc::Font::getSampler;
        t["getSize"] = &trussc::Font::getSize;
        t["getStringPath"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.getStringPath(text, x, y, h, v); }, [](trussc::Font& self, const std::string & text, float x, float y) { return self.getStringPath(text, x, y); });
        t["getTcyDigitMax"] = &trussc::Font::getTcyDigitMax;
        t["getTcyLatinMode"] = &trussc::Font::getTcyLatinMode;
        t["getWidth"] = &trussc::Font::getWidth;
        t["getWritingMode"] = &trussc::Font::getWritingMode;
        t["isLoaded"] = &trussc::Font::isLoaded;
        t["isLoading"] = &trussc::Font::isLoading;
        t["isWrapEnabled"] = &trussc::Font::isWrapEnabled;
        t["load"] = sol::overload([](trussc::Font& self, const fs::path & nameOrPath, int size) { return self.load(nameOrPath, size); }, [](trussc::Font& self, const fs::path & nameOrPath, int size, int faceIndex) { return self.load(nameOrPath, size, faceIndex); });
        t["loadAsync"] = sol::overload([](trussc::Font& self, const fs::path & nameOrPath, int size) { return self.loadAsync(nameOrPath, size); }, [](trussc::Font& self, const fs::path & nameOrPath, int size, int faceIndex) { return self.loadAsync(nameOrPath, size, faceIndex); });
        t["resetLineHeight"] = &trussc::Font::resetLineHeight;
        t["setAlign"] = sol::overload([](trussc::Font& self, trussc::Direction h, trussc::Direction v) { return self.setAlign(h, v); }, [](trussc::Font& self, trussc::Direction h) { return self.setAlign(h); });
        t["setGridFit"] = &trussc::Font::setGridFit;
        t["setHangingPunctuation"] = &trussc::Font::setHangingPunctuation;
        t["setKinsoku"] = &trussc::Font::setKinsoku;
        t["setLatinHyphenation"] = &trussc::Font::setLatinHyphenation;
        t["setLineHeight"] = &trussc::Font::setLineHeight;
        t["setLineHeightEm"] = &trussc::Font::setLineHeightEm;
        t["setMaxLineLength"] = &trussc::Font::setMaxLineLength;
        t["setMipmaps"] = &trussc::Font::setMipmaps;
        t["setOversampling"] = &trussc::Font::setOversampling;
        t["setTcyDigits"] = &trussc::Font::setTcyDigits;
        t["setTcyLatin"] = &trussc::Font::setTcyLatin;
        t["setWritingMode"] = &trussc::Font::setWritingMode;
        t["stringWidth"] = &trussc::Font::stringWidth;
        t["getDefaultOversampling"] = &trussc::Font::getDefaultOversampling;
        t["getTotalCacheMemoryUsage"] = &trussc::Font::getTotalCacheMemoryUsage;
        t["setDefaultOversampling"] = &trussc::Font::setDefaultOversampling;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::TweenMod
            , trussc::TweenMod()
        >();
        sol::usertype<trussc::TweenMod> t = lua->new_usertype<trussc::TweenMod>("TweenMod", constructors, sol::call_constructor, constructors);
        t["complete"] = &trussc::TweenMod::complete;
        t["delay"] = &trussc::TweenMod::delay;
        t["duration"] = &trussc::TweenMod::duration;
        t["ease"] = sol::overload([](trussc::TweenMod& self, trussc::EaseType type) -> decltype(auto) { return self.ease(type); }, [](trussc::TweenMod& self, trussc::EaseType type, trussc::EaseMode mode) -> decltype(auto) { return self.ease(type, mode); }, [](trussc::TweenMod& self, trussc::EaseFunction fn) -> decltype(auto) { return self.ease(fn); }, [](trussc::TweenMod& self, trussc::EaseFunction fn, trussc::EaseMode mode) -> decltype(auto) { return self.ease(fn, mode); });
        t["getDelay"] = &trussc::TweenMod::getDelay;
        t["getDuration"] = &trussc::TweenMod::getDuration;
        t["getEaseMode"] = &trussc::TweenMod::getEaseMode;
        t["getEaseType"] = &trussc::TweenMod::getEaseType;
        t["getOwner"] = [](trussc::TweenMod& self) { return self.getOwner(); };
        t["getProgress"] = &trussc::TweenMod::getProgress;
        t["isComplete"] = &trussc::TweenMod::isComplete;
        t["isPlaying"] = &trussc::TweenMod::isPlaying;
        t["moveBy"] = sol::overload([](trussc::TweenMod& self, float dx, float dy) -> decltype(auto) { return self.moveBy(dx, dy); }, [](trussc::TweenMod& self, float dx, float dy, float dz) -> decltype(auto) { return self.moveBy(dx, dy, dz); }, [](trussc::TweenMod& self, const trussc::Vec3 & delta) -> decltype(auto) { return self.moveBy(delta); }, [](trussc::TweenMod& self, const trussc::Vec2 & delta) -> decltype(auto) { return self.moveBy(delta); });
        t["moveFrom"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveFrom(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveFrom(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveFrom(pos); });
        t["moveTo"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveTo(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveTo(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveTo(pos); }, [](trussc::TweenMod& self, const trussc::Vec2 & pos) -> decltype(auto) { return self.moveTo(pos); });
        t["pause"] = &trussc::TweenMod::pause;
        t["reset"] = &trussc::TweenMod::reset;
        t["resume"] = &trussc::TweenMod::resume;
        t["rotateBy"] = &trussc::TweenMod::rotateBy;
        t["rotateFrom"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateFrom(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateFrom(q); });
        t["rotateTo"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateTo(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateTo(q); });
        t["rotateXBy"] = &trussc::TweenMod::rotateXBy;
        t["rotateXFrom"] = &trussc::TweenMod::rotateXFrom;
        t["rotateXTo"] = &trussc::TweenMod::rotateXTo;
        t["rotateYBy"] = &trussc::TweenMod::rotateYBy;
        t["rotateYFrom"] = &trussc::TweenMod::rotateYFrom;
        t["rotateYTo"] = &trussc::TweenMod::rotateYTo;
        t["rotateZBy"] = &trussc::TweenMod::rotateZBy;
        t["rotateZTo"] = &trussc::TweenMod::rotateZTo;
        t["scaleBy"] = sol::overload([](trussc::TweenMod& self, float factor) -> decltype(auto) { return self.scaleBy(factor); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleBy(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleBy(sx, sy, sz); });
        t["scaleFrom"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleFrom(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleFrom(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleFrom(sx, sy, sz); });
        t["scaleTo"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleTo(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleTo(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleTo(sx, sy, sz); }, [](trussc::TweenMod& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scaleTo(s); });
        t["start"] = &trussc::TweenMod::start;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Quaternion
            , trussc::Quaternion()
            , trussc::Quaternion(float, float, float, float)
        >();
        sol::usertype<trussc::Quaternion> t = lua->new_usertype<trussc::Quaternion>("Quaternion", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; };
        t[sol::meta_function::equal_to] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; };
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["identity"] = &trussc::Quaternion::identity;
        t["slerp"] = &trussc::Quaternion::slerp;
    }
    {
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["build"] = &trussc::ChipSoundBundle::build;
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
    }
    lua->new_usertype<trussc::ChipSoundNote::Wave>("ChipSoundNoteWave",
        sol::meta_function::equal_to, [](trussc::ChipSoundNote::Wave a, trussc::ChipSoundNote::Wave b){ return a == b; },
        "Sin", sol::var(trussc::ChipSoundNote::Wave::Sin),
        "Square", sol::var(trussc::ChipSoundNote::Wave::Square),
        "Triangle", sol::var(trussc::ChipSoundNote::Wave::Triangle),
        "Sawtooth", sol::var(trussc::ChipSoundNote::Wave::Sawtooth),
        "Noise", sol::var(trussc::ChipSoundNote::Wave::Noise),
        "PinkNoise", sol::var(trussc::ChipSoundNote::Wave::PinkNoise),
        "Silent", sol::var(trussc::ChipSoundNote::Wave::Silent));
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["changed"] = &trussc::TouchPoint::changed;
        t["id"] = &trussc::TouchPoint::id;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
    }
    lua->new_usertype<trussc::SoundSource::Kind>("SoundSourceKind",
        sol::meta_function::equal_to, [](trussc::SoundSource::Kind a, trussc::SoundSource::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundSource::Kind::Eager),
        "Stream", sol::var(trussc::SoundSource::Kind::Stream));
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::ResizeEventArgs> t = lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        t["height"] = &trussc::ResizeEventArgs::height;
        t["width"] = &trussc::ResizeEventArgs::width;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
