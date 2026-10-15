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
void tcxLuaGenShard_03(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::ScrollContainer
            , trussc::ScrollContainer()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ScrollContainer>::user_metatable()];
        sol::usertype<trussc::ScrollContainer> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ScrollContainer>>()
            : lua->new_usertype<trussc::ScrollContainer>("ScrollContainer", constructors, sol::call_constructor, constructors);
        (*lua)["ScrollContainer"] = t;
        t["mousePressed"] = &trussc::ScrollContainer::mousePressed;
        t["mouseReleased"] = &trussc::ScrollContainer::mouseReleased;
        t["mouseDragged"] = &trussc::ScrollContainer::mouseDragged;
        t["mouseScrolled"] = &trussc::ScrollContainer::mouseScrolled;
        t["localMatrixChanged"] = &trussc::ScrollContainer::localMatrixChanged;
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
        t["getWidth"] = &trussc::ScrollContainer::getWidth;
        t["getHeight"] = &trussc::ScrollContainer::getHeight;
        t["getSize"] = &trussc::ScrollContainer::getSize;
        t["setWidth"] = &trussc::ScrollContainer::setWidth;
        t["setHeight"] = &trussc::ScrollContainer::setHeight;
        t["setSize"] = sol::overload([](trussc::ScrollContainer& self, float w, float h) { return self.setSize(w, h); }, [](trussc::ScrollContainer& self, float size) { return self.setSize(size); }, [](trussc::ScrollContainer& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::ScrollContainer::setRect;
        t["setClipping"] = &trussc::ScrollContainer::setClipping;
        t["isClipping"] = &trussc::ScrollContainer::isClipping;
        t["getLeft"] = &trussc::ScrollContainer::getLeft;
        t["getRight"] = &trussc::ScrollContainer::getRight;
        t["getTop"] = &trussc::ScrollContainer::getTop;
        t["getBottom"] = &trussc::ScrollContainer::getBottom;
        t["hitTest"] = [](trussc::ScrollContainer& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["draw"] = &trussc::ScrollContainer::draw;
        t["setup"] = &trussc::ScrollContainer::setup;
        t["cleanup"] = &trussc::ScrollContainer::cleanup;
        t["addChild"] = sol::overload([](trussc::ScrollContainer& self, trussc::ScrollContainer::Ptr child) { return self.addChild(child); }, [](trussc::ScrollContainer& self, trussc::ScrollContainer::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::ScrollContainer& self, size_t index, trussc::ScrollContainer::Ptr child) { return self.insertChild(index, child); }, [](trussc::ScrollContainer& self, size_t index, trussc::ScrollContainer::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::ScrollContainer::removeChild;
        t["removeAllChildren"] = &trussc::ScrollContainer::removeAllChildren;
        t["onChildAdded"] = &trussc::ScrollContainer::onChildAdded;
        t["onChildRemoved"] = &trussc::ScrollContainer::onChildRemoved;
        t["getParent"] = &trussc::ScrollContainer::getParent;
        t["getChildren"] = &trussc::ScrollContainer::getChildren;
        t["getChildCount"] = &trussc::ScrollContainer::getChildCount;
        t["getChildIndex"] = &trussc::ScrollContainer::getChildIndex;
        t["moveToFront"] = &trussc::ScrollContainer::moveToFront;
        t["moveToBack"] = &trussc::ScrollContainer::moveToBack;
        t["isActive"] = &trussc::ScrollContainer::isActive;
        t["setActive"] = &trussc::ScrollContainer::setActive;
        t["isVisible"] = &trussc::ScrollContainer::isVisible;
        t["setVisible"] = &trussc::ScrollContainer::setVisible;
        t["getActive"] = &trussc::ScrollContainer::getActive;
        t["getVisible"] = &trussc::ScrollContainer::getVisible;
        t["setIsActive"] = &trussc::ScrollContainer::setIsActive;
        t["setIsVisible"] = &trussc::ScrollContainer::setIsVisible;
        t["destroy"] = &trussc::ScrollContainer::destroy;
        t["isDead"] = &trussc::ScrollContainer::isDead;
        t["enableEvents"] = &trussc::ScrollContainer::enableEvents;
        t["disableEvents"] = &trussc::ScrollContainer::disableEvents;
        t["isEventsEnabled"] = &trussc::ScrollContainer::isEventsEnabled;
        t["isMouseOver"] = &trussc::ScrollContainer::isMouseOver;
        t["setName"] = &trussc::ScrollContainer::setName;
        t["getName"] = &trussc::ScrollContainer::getName;
        t["hasName"] = &trussc::ScrollContainer::hasName;
        t["getTypeName"] = &trussc::ScrollContainer::getTypeName;
        t["getDisplayName"] = &trussc::ScrollContainer::getDisplayName;
        t["getInstanceId"] = &trussc::ScrollContainer::getInstanceId;
        t["findByInstanceId"] = &trussc::ScrollContainer::findByInstanceId;
        t["getPos"] = &trussc::ScrollContainer::getPos;
        t["getX"] = &trussc::ScrollContainer::getX;
        t["getY"] = &trussc::ScrollContainer::getY;
        t["getZ"] = &trussc::ScrollContainer::getZ;
        t["setPos"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::ScrollContainer& self, float x, float y) { return self.setPos(x, y); }, [](trussc::ScrollContainer& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::ScrollContainer::setX;
        t["setY"] = &trussc::ScrollContainer::setY;
        t["setZ"] = &trussc::ScrollContainer::setZ;
        t["getQuaternion"] = &trussc::ScrollContainer::getQuaternion;
        t["setQuaternion"] = &trussc::ScrollContainer::setQuaternion;
        t["getEuler"] = &trussc::ScrollContainer::getEuler;
        t["setEuler"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::ScrollContainer& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::ScrollContainer::getEulerDeg;
        t["setEulerDeg"] = &trussc::ScrollContainer::setEulerDeg;
        t["getRot"] = &trussc::ScrollContainer::getRot;
        t["setRot"] = &trussc::ScrollContainer::setRot;
        t["getRotDeg"] = &trussc::ScrollContainer::getRotDeg;
        t["setRotDeg"] = &trussc::ScrollContainer::setRotDeg;
        t["getScale"] = &trussc::ScrollContainer::getScale;
        t["getScaleX"] = &trussc::ScrollContainer::getScaleX;
        t["getScaleY"] = &trussc::ScrollContainer::getScaleY;
        t["getScaleZ"] = &trussc::ScrollContainer::getScaleZ;
        t["setScale"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::ScrollContainer& self, float uniform) { return self.setScale(uniform); }, [](trussc::ScrollContainer& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::ScrollContainer& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::ScrollContainer::setScaleX;
        t["setScaleY"] = &trussc::ScrollContainer::setScaleY;
        t["setScaleZ"] = &trussc::ScrollContainer::setScaleZ;
        t["getLocalMatrix"] = &trussc::ScrollContainer::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::ScrollContainer::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::ScrollContainer::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::ScrollContainer& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::ScrollContainer::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::ScrollContainer& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::ScrollContainer& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::ScrollContainer& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::ScrollContainer::getMouseX;
        t["getMouseY"] = &trussc::ScrollContainer::getMouseY;
        t["getPMouseX"] = &trussc::ScrollContainer::getPMouseX;
        t["getPMouseY"] = &trussc::ScrollContainer::getPMouseY;
        t["findHitNode"] = &trussc::ScrollContainer::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::ScrollContainer::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::ScrollContainer::getCameraContext;
        t["setCameraContext"] = &trussc::ScrollContainer::setCameraContext;
        t["getModTypeNames"] = &trussc::ScrollContainer::getModTypeNames;
        t["getMods"] = &trussc::ScrollContainer::getMods;
        t["getModByTypeName"] = &trussc::ScrollContainer::getModByTypeName;
        t["callAfter"] = &trussc::ScrollContainer::callAfter;
        t["callEvery"] = &trussc::ScrollContainer::callEvery;
        t["callEveryCatchUp"] = &trussc::ScrollContainer::callEveryCatchUp;
        t["cancelTimer"] = &trussc::ScrollContainer::cancelTimer;
        t["cancelAllTimers"] = &trussc::ScrollContainer::cancelAllTimers;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Rect
            , trussc::Rect()
            , trussc::Rect(float, float, float, float)
            , trussc::Rect(const trussc::Vec2 &, float, float)
            , trussc::Rect(const trussc::Vec3 &, float, float)
            , trussc::Rect(float, float, float, float, float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Rect>::user_metatable()];
        sol::usertype<trussc::Rect> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Rect>>()
            : lua->new_usertype<trussc::Rect>("Rect", constructors, sol::call_constructor, constructors);
        (*lua)["Rect"] = t;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::MouseMoveEventArgs>::user_metatable()];
        sol::usertype<trussc::MouseMoveEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::MouseMoveEventArgs>>()
            : lua->new_usertype<trussc::MouseMoveEventArgs>("MouseMoveEventArgs");
        (*lua)["MouseMoveEventArgs"] = t;
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["deltaX"] = &trussc::MouseMoveEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseMoveEventArgs::deltaY;
        t["shift"] = &trussc::MouseMoveEventArgs::shift;
        t["ctrl"] = &trussc::MouseMoveEventArgs::ctrl;
        t["alt"] = &trussc::MouseMoveEventArgs::alt;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["pos"] = &trussc::MouseMoveEventArgs::pos;
        t["globalPos"] = &trussc::MouseMoveEventArgs::globalPos;
        t["delta"] = &trussc::MouseMoveEventArgs::delta;
        t["globalDelta"] = &trussc::MouseMoveEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseMoveEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Platform>::user_metatable()];
        sol::usertype<trussc::Platform> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Platform>>()
            : lua->new_usertype<trussc::Platform>("Platform");
        (*lua)["Platform"] = t;
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::AudioSettings>::user_metatable()];
        sol::usertype<trussc::AudioSettings> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::AudioSettings>>()
            : lua->new_usertype<trussc::AudioSettings>("AudioSettings");
        (*lua)["AudioSettings"] = t;
        t["sampleRate"] = &trussc::AudioSettings::sampleRate;
        t["channels"] = &trussc::AudioSettings::channels;
        t["bufferSize"] = &trussc::AudioSettings::bufferSize;
        t["maxPolyphony"] = &trussc::AudioSettings::maxPolyphony;
        t["deviceName"] = &trussc::AudioSettings::deviceName;
        t["backend"] = &trussc::AudioSettings::backend;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TouchPoint>::user_metatable()];
        sol::usertype<trussc::TouchPoint> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TouchPoint>>()
            : lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        (*lua)["TouchPoint"] = t;
        t["id"] = &trussc::TouchPoint::id;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["changed"] = &trussc::TouchPoint::changed;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ExitRequestEventArgs>::user_metatable()];
        sol::usertype<trussc::ExitRequestEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ExitRequestEventArgs>>()
            : lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        (*lua)["ExitRequestEventArgs"] = t;
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
        t["reason"] = &trussc::ExitRequestEventArgs::reason;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Ray::Hit>::user_metatable()];
        sol::usertype<trussc::Ray::Hit> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Ray::Hit>>()
            : lua->new_usertype<trussc::Ray::Hit>("RayHit");
        (*lua)["RayHit"] = t;
        t["hit"] = &trussc::Ray::Hit::hit;
        t["t"] = &trussc::Ray::Hit::t;
        t["point"] = &trussc::Ray::Hit::point;
    }
    lua->new_usertype<trussc::ThermalState>("ThermalState",
        sol::meta_function::equal_to, [](trussc::ThermalState a, trussc::ThermalState b){ return a == b; },
        "Nominal", sol::var(trussc::ThermalState::Nominal),
        "Fair", sol::var(trussc::ThermalState::Fair),
        "Serious", sol::var(trussc::ThermalState::Serious),
        "Critical", sol::var(trussc::ThermalState::Critical));
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
