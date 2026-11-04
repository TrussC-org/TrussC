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
        sol::usertype<trussc::MouseMoveEventArgs> t = lua->new_usertype<trussc::MouseMoveEventArgs>("MouseMoveEventArgs");
        t["alt"] = &trussc::MouseMoveEventArgs::alt;
        t["consumed"] = &trussc::MouseMoveEventArgs::consumed;
        t["ctrl"] = &trussc::MouseMoveEventArgs::ctrl;
        t["delta"] = &trussc::MouseMoveEventArgs::delta;
        t["deltaX"] = &trussc::MouseMoveEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseMoveEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseMoveEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseMoveEventArgs::globalPos;
        t["pos"] = &trussc::MouseMoveEventArgs::pos;
        t["shift"] = &trussc::MouseMoveEventArgs::shift;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["level"] = &trussc::PlayingSound::level;
        t["loop"] = &trussc::PlayingSound::loop;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["pan"] = &trussc::PlayingSound::pan;
        t["paused"] = &trussc::PlayingSound::paused;
        t["playing"] = &trussc::PlayingSound::playing;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
        t["speed"] = &trussc::PlayingSound::speed;
        t["volume"] = &trussc::PlayingSound::volume;
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
        auto constructors = tcxLuaConstructors<trussc::EventListener
            , trussc::EventListener()
        >();
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener", constructors, sol::call_constructor, constructors);
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    {
        sol::usertype<trussc::Node::HitResult> t = lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["node"] = &trussc::Node::HitResult::node;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    lua->new_usertype<trussc::AudioBackend>("AudioBackend",
        sol::meta_function::equal_to, [](trussc::AudioBackend a, trussc::AudioBackend b){ return a == b; },
        "Default", sol::var(trussc::AudioBackend::Default),
        "Null", sol::var(trussc::AudioBackend::Null));
    {
        sol::usertype<trussc::HeadlessSettings> t = lua->new_usertype<trussc::HeadlessSettings>("HeadlessSettings");
        t["targetFps"] = &trussc::HeadlessSettings::targetFps;
        t["setFps"] = &trussc::HeadlessSettings::setFps;
    }
    {
        sol::usertype<trussc::CloseRequestEventArgs> t = lua->new_usertype<trussc::CloseRequestEventArgs>("CloseRequestEventArgs");
        t["cancel"] = &trussc::CloseRequestEventArgs::cancel;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
