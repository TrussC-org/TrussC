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
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_05(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::RectNodeButton
            , trussc::RectNodeButton()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::RectNodeButton>::user_metatable()];
        sol::usertype<trussc::RectNodeButton> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::RectNodeButton>>()
            : lua->new_usertype<trussc::RectNodeButton>("RectNodeButton", constructors, sol::call_constructor, constructors);
        (*lua)["RectNodeButton"] = t;
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["mousePressed"] = &trussc::RectNodeButton::mousePressed;
        t["mouseReleased"] = &trussc::RectNodeButton::mouseReleased;
        t["mouseDragged"] = &trussc::RectNodeButton::mouseDragged;
        t["mouseScrolled"] = &trussc::RectNodeButton::mouseScrolled;
        t["localMatrixChanged"] = &trussc::RectNodeButton::localMatrixChanged;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["draw"] = &trussc::RectNodeButton::draw;
        t["getWidth"] = &trussc::RectNodeButton::getWidth;
        t["getHeight"] = &trussc::RectNodeButton::getHeight;
        t["getSize"] = &trussc::RectNodeButton::getSize;
        t["setWidth"] = &trussc::RectNodeButton::setWidth;
        t["setHeight"] = &trussc::RectNodeButton::setHeight;
        t["setSize"] = sol::overload([](trussc::RectNodeButton& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNodeButton& self, float size) { return self.setSize(size); }, [](trussc::RectNodeButton& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::RectNodeButton::setRect;
        t["setClipping"] = &trussc::RectNodeButton::setClipping;
        t["isClipping"] = &trussc::RectNodeButton::isClipping;
        t["getLeft"] = &trussc::RectNodeButton::getLeft;
        t["getRight"] = &trussc::RectNodeButton::getRight;
        t["getTop"] = &trussc::RectNodeButton::getTop;
        t["getBottom"] = &trussc::RectNodeButton::getBottom;
        t["hitTest"] = [](trussc::RectNodeButton& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["setup"] = &trussc::RectNodeButton::setup;
        t["update"] = &trussc::RectNodeButton::update;
        t["cleanup"] = &trussc::RectNodeButton::cleanup;
        t["addChild"] = sol::overload([](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child) { return self.addChild(child); }, [](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child) { return self.insertChild(index, child); }, [](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::RectNodeButton::removeChild;
        t["removeAllChildren"] = &trussc::RectNodeButton::removeAllChildren;
        t["onChildAdded"] = &trussc::RectNodeButton::onChildAdded;
        t["onChildRemoved"] = &trussc::RectNodeButton::onChildRemoved;
        t["getParent"] = &trussc::RectNodeButton::getParent;
        t["getChildren"] = &trussc::RectNodeButton::getChildren;
        t["getChildCount"] = &trussc::RectNodeButton::getChildCount;
        t["getChildIndex"] = &trussc::RectNodeButton::getChildIndex;
        t["moveToFront"] = &trussc::RectNodeButton::moveToFront;
        t["moveToBack"] = &trussc::RectNodeButton::moveToBack;
        t["isActive"] = &trussc::RectNodeButton::isActive;
        t["setActive"] = &trussc::RectNodeButton::setActive;
        t["isVisible"] = &trussc::RectNodeButton::isVisible;
        t["setVisible"] = &trussc::RectNodeButton::setVisible;
        t["getActive"] = &trussc::RectNodeButton::getActive;
        t["getVisible"] = &trussc::RectNodeButton::getVisible;
        t["setIsActive"] = &trussc::RectNodeButton::setIsActive;
        t["setIsVisible"] = &trussc::RectNodeButton::setIsVisible;
        t["destroy"] = &trussc::RectNodeButton::destroy;
        t["isDead"] = &trussc::RectNodeButton::isDead;
        t["enableEvents"] = &trussc::RectNodeButton::enableEvents;
        t["disableEvents"] = &trussc::RectNodeButton::disableEvents;
        t["isEventsEnabled"] = &trussc::RectNodeButton::isEventsEnabled;
        t["isMouseOver"] = &trussc::RectNodeButton::isMouseOver;
        t["setName"] = &trussc::RectNodeButton::setName;
        t["getName"] = &trussc::RectNodeButton::getName;
        t["hasName"] = &trussc::RectNodeButton::hasName;
        t["getTypeName"] = &trussc::RectNodeButton::getTypeName;
        t["getDisplayName"] = &trussc::RectNodeButton::getDisplayName;
        t["getInstanceId"] = &trussc::RectNodeButton::getInstanceId;
        t["findByInstanceId"] = &trussc::RectNodeButton::findByInstanceId;
        t["getPos"] = &trussc::RectNodeButton::getPos;
        t["getX"] = &trussc::RectNodeButton::getX;
        t["getY"] = &trussc::RectNodeButton::getY;
        t["getZ"] = &trussc::RectNodeButton::getZ;
        t["setPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::RectNodeButton::setX;
        t["setY"] = &trussc::RectNodeButton::setY;
        t["setZ"] = &trussc::RectNodeButton::setZ;
        t["getQuaternion"] = &trussc::RectNodeButton::getQuaternion;
        t["setQuaternion"] = &trussc::RectNodeButton::setQuaternion;
        t["getEuler"] = &trussc::RectNodeButton::getEuler;
        t["setEuler"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::RectNodeButton& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::RectNodeButton::getEulerDeg;
        t["setEulerDeg"] = &trussc::RectNodeButton::setEulerDeg;
        t["getRot"] = &trussc::RectNodeButton::getRot;
        t["setRot"] = &trussc::RectNodeButton::setRot;
        t["getRotDeg"] = &trussc::RectNodeButton::getRotDeg;
        t["setRotDeg"] = &trussc::RectNodeButton::setRotDeg;
        t["getScale"] = &trussc::RectNodeButton::getScale;
        t["getScaleX"] = &trussc::RectNodeButton::getScaleX;
        t["getScaleY"] = &trussc::RectNodeButton::getScaleY;
        t["getScaleZ"] = &trussc::RectNodeButton::getScaleZ;
        t["setScale"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::RectNodeButton& self, float uniform) { return self.setScale(uniform); }, [](trussc::RectNodeButton& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::RectNodeButton& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::RectNodeButton::setScaleX;
        t["setScaleY"] = &trussc::RectNodeButton::setScaleY;
        t["setScaleZ"] = &trussc::RectNodeButton::setScaleZ;
        t["getLocalMatrix"] = &trussc::RectNodeButton::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::RectNodeButton::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::RectNodeButton::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::RectNodeButton::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::RectNodeButton::getMouseX;
        t["getMouseY"] = &trussc::RectNodeButton::getMouseY;
        t["getPMouseX"] = &trussc::RectNodeButton::getPMouseX;
        t["getPMouseY"] = &trussc::RectNodeButton::getPMouseY;
        t["findHitNode"] = &trussc::RectNodeButton::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::RectNodeButton::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::RectNodeButton::getCameraContext;
        t["setCameraContext"] = &trussc::RectNodeButton::setCameraContext;
        t["getModTypeNames"] = &trussc::RectNodeButton::getModTypeNames;
        t["getMods"] = &trussc::RectNodeButton::getMods;
        t["getModByTypeName"] = &trussc::RectNodeButton::getModByTypeName;
        t["callAfter"] = &trussc::RectNodeButton::callAfter;
        t["callEvery"] = &trussc::RectNodeButton::callEvery;
        t["callEveryCatchUp"] = &trussc::RectNodeButton::callEveryCatchUp;
        t["cancelTimer"] = &trussc::RectNodeButton::cancelTimer;
        t["cancelAllTimers"] = &trussc::RectNodeButton::cancelAllTimers;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LayoutMod
            , trussc::LayoutMod()
            , trussc::LayoutMod(trussc::LayoutDirection)
            , trussc::LayoutMod(trussc::LayoutDirection, float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::LayoutMod>::user_metatable()];
        sol::usertype<trussc::LayoutMod> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::LayoutMod>>()
            : lua->new_usertype<trussc::LayoutMod>("LayoutMod", constructors, sol::call_constructor, constructors);
        (*lua)["LayoutMod"] = t;
        t["getDirection"] = &trussc::LayoutMod::getDirection;
        t["setDirection"] = &trussc::LayoutMod::setDirection;
        t["getSpacing"] = &trussc::LayoutMod::getSpacing;
        t["setSpacing"] = &trussc::LayoutMod::setSpacing;
        t["getCrossAxis"] = &trussc::LayoutMod::getCrossAxis;
        t["setCrossAxis"] = &trussc::LayoutMod::setCrossAxis;
        t["getMainAxis"] = &trussc::LayoutMod::getMainAxis;
        t["setMainAxis"] = &trussc::LayoutMod::setMainAxis;
        t["getPaddingLeft"] = &trussc::LayoutMod::getPaddingLeft;
        t["getPaddingTop"] = &trussc::LayoutMod::getPaddingTop;
        t["getPaddingRight"] = &trussc::LayoutMod::getPaddingRight;
        t["getPaddingBottom"] = &trussc::LayoutMod::getPaddingBottom;
        t["setPadding"] = sol::overload([](trussc::LayoutMod& self, float padding) -> decltype(auto) { return self.setPadding(padding); }, [](trussc::LayoutMod& self, float vertical, float horizontal) -> decltype(auto) { return self.setPadding(vertical, horizontal); }, [](trussc::LayoutMod& self, float top, float right, float bottom, float left) -> decltype(auto) { return self.setPadding(top, right, bottom, left); });
        t["setPaddingLeft"] = &trussc::LayoutMod::setPaddingLeft;
        t["setPaddingTop"] = &trussc::LayoutMod::setPaddingTop;
        t["setPaddingRight"] = &trussc::LayoutMod::setPaddingRight;
        t["setPaddingBottom"] = &trussc::LayoutMod::setPaddingBottom;
        t["updateLayout"] = &trussc::LayoutMod::updateLayout;
        t["getOwner"] = [](trussc::LayoutMod& self) { return self.getOwner(); };
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ChipSoundNote
            , trussc::ChipSoundNote()
            , trussc::ChipSoundNote(trussc::Wave, float, float)
            , trussc::ChipSoundNote(trussc::Wave, float, float, float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ChipSoundNote>::user_metatable()];
        sol::usertype<trussc::ChipSoundNote> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ChipSoundNote>>()
            : lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote", constructors, sol::call_constructor, constructors);
        (*lua)["ChipSoundNote"] = t;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::VideoRecordSettings>::user_metatable()];
        sol::usertype<trussc::VideoRecordSettings> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::VideoRecordSettings>>()
            : lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        (*lua)["VideoRecordSettings"] = t;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Reflector>::user_metatable()];
        sol::usertype<trussc::Reflector> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Reflector>>()
            : lua->new_usertype<trussc::Reflector>("Reflector");
        (*lua)["Reflector"] = t;
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["isDerived"] = &trussc::Reflector::isDerived;
        t["pushDerived"] = &trussc::Reflector::pushDerived;
        t["popDerived"] = &trussc::Reflector::popDerived;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpServerErrorEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpServerErrorEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpServerErrorEventArgs>>()
            : lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        (*lua)["TcpServerErrorEventArgs"] = t;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TcpDisconnectEventArgs>::user_metatable()];
        sol::usertype<trussc::TcpDisconnectEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TcpDisconnectEventArgs>>()
            : lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        (*lua)["TcpDisconnectEventArgs"] = t;
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ClipboardPastedEventArgs>::user_metatable()];
        sol::usertype<trussc::ClipboardPastedEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ClipboardPastedEventArgs>>()
            : lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        (*lua)["ClipboardPastedEventArgs"] = t;
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
