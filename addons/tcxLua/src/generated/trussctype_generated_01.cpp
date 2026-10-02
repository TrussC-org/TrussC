// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_01(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Node> t = lua->new_usertype<trussc::Node>("Node",
            sol::constructors<trussc::Node()>(),
            sol::call_constructor, sol::constructors<trussc::Node()>());
        t["localMatrixChanged"] = &trussc::Node::localMatrixChanged;
        t["setup"] = &trussc::Node::setup;
        t["update"] = &trussc::Node::update;
        t["draw"] = &trussc::Node::draw;
        t["cleanup"] = &trussc::Node::cleanup;
        t["addChild"] = sol::overload([](trussc::Node& self, trussc::Node::Ptr child) { return self.addChild(child); }, [](trussc::Node& self, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::Node& self, size_t index, trussc::Node::Ptr child) { return self.insertChild(index, child); }, [](trussc::Node& self, size_t index, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::Node::removeChild;
        t["removeAllChildren"] = &trussc::Node::removeAllChildren;
        t["onChildAdded"] = &trussc::Node::onChildAdded;
        t["onChildRemoved"] = &trussc::Node::onChildRemoved;
        t["getParent"] = &trussc::Node::getParent;
        t["getChildren"] = &trussc::Node::getChildren;
        t["getChildCount"] = &trussc::Node::getChildCount;
        t["getChildIndex"] = &trussc::Node::getChildIndex;
        t["moveToFront"] = &trussc::Node::moveToFront;
        t["moveToBack"] = &trussc::Node::moveToBack;
        t["isActive"] = &trussc::Node::isActive;
        t["setActive"] = &trussc::Node::setActive;
        t["isVisible"] = &trussc::Node::isVisible;
        t["setVisible"] = &trussc::Node::setVisible;
        t["getActive"] = &trussc::Node::getActive;
        t["getVisible"] = &trussc::Node::getVisible;
        t["setIsActive"] = &trussc::Node::setIsActive;
        t["setIsVisible"] = &trussc::Node::setIsVisible;
        t["destroy"] = &trussc::Node::destroy;
        t["isDead"] = &trussc::Node::isDead;
        t["enableEvents"] = &trussc::Node::enableEvents;
        t["disableEvents"] = &trussc::Node::disableEvents;
        t["isEventsEnabled"] = &trussc::Node::isEventsEnabled;
        t["isMouseOver"] = &trussc::Node::isMouseOver;
        t["setName"] = &trussc::Node::setName;
        t["getName"] = &trussc::Node::getName;
        t["hasName"] = &trussc::Node::hasName;
        t["getTypeName"] = &trussc::Node::getTypeName;
        t["getDisplayName"] = &trussc::Node::getDisplayName;
        t["getInstanceId"] = &trussc::Node::getInstanceId;
        t["findByInstanceId"] = &trussc::Node::findByInstanceId;
        t["getPos"] = &trussc::Node::getPos;
        t["getX"] = &trussc::Node::getX;
        t["getY"] = &trussc::Node::getY;
        t["getZ"] = &trussc::Node::getZ;
        t["setPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::Node& self, float x, float y) { return self.setPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::Node::setX;
        t["setY"] = &trussc::Node::setY;
        t["setZ"] = &trussc::Node::setZ;
        t["getQuaternion"] = &trussc::Node::getQuaternion;
        t["setQuaternion"] = &trussc::Node::setQuaternion;
        t["getEuler"] = &trussc::Node::getEuler;
        t["setEuler"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::Node& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::Node::getEulerDeg;
        t["setEulerDeg"] = &trussc::Node::setEulerDeg;
        t["getRot"] = &trussc::Node::getRot;
        t["setRot"] = &trussc::Node::setRot;
        t["getRotDeg"] = &trussc::Node::getRotDeg;
        t["setRotDeg"] = &trussc::Node::setRotDeg;
        t["getScale"] = &trussc::Node::getScale;
        t["getScaleX"] = &trussc::Node::getScaleX;
        t["getScaleY"] = &trussc::Node::getScaleY;
        t["getScaleZ"] = &trussc::Node::getScaleZ;
        t["setScale"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::Node& self, float uniform) { return self.setScale(uniform); }, [](trussc::Node& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::Node& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::Node::setScaleX;
        t["setScaleY"] = &trussc::Node::setScaleY;
        t["setScaleZ"] = &trussc::Node::setScaleZ;
        t["getLocalMatrix"] = &trussc::Node::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::Node::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::Node::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::Node& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::Node::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::Node& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::Node& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::Node::getMouseX;
        t["getMouseY"] = &trussc::Node::getMouseY;
        t["getPMouseX"] = &trussc::Node::getPMouseX;
        t["getPMouseY"] = &trussc::Node::getPMouseY;
        t["findHitNode"] = &trussc::Node::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::Node::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::Node::getCameraContext;
        t["setCameraContext"] = &trussc::Node::setCameraContext;
        t["getModTypeNames"] = &trussc::Node::getModTypeNames;
        t["getMods"] = &trussc::Node::getMods;
        t["getModByTypeName"] = &trussc::Node::getModByTypeName;
        t["callAfter"] = &trussc::Node::callAfter;
        t["callEvery"] = &trussc::Node::callEvery;
        t["callEveryCatchUp"] = &trussc::Node::callEveryCatchUp;
        t["cancelTimer"] = &trussc::Node::cancelTimer;
        t["cancelAllTimers"] = &trussc::Node::cancelAllTimers;
    }
    {
        sol::usertype<trussc::BuildInfo> t = lua->new_usertype<trussc::BuildInfo>("BuildInfo");
        t["date"] = &trussc::BuildInfo::date;
        t["time"] = &trussc::BuildInfo::time;
        t["dateTime"] = &trussc::BuildInfo::dateTime;
        t["timestamp"] = &trussc::BuildInfo::timestamp;
        t["year"] = &trussc::BuildInfo::year;
        t["month"] = &trussc::BuildInfo::month;
        t["day"] = &trussc::BuildInfo::day;
        t["hour"] = &trussc::BuildInfo::hour;
        t["minute"] = &trussc::BuildInfo::minute;
        t["second"] = &trussc::BuildInfo::second;
    }
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::ThermalState>("ThermalState",
        sol::meta_function::equal_to, [](trussc::ThermalState a, trussc::ThermalState b){ return a == b; },
        "Nominal", sol::var(trussc::ThermalState::Nominal),
        "Fair", sol::var(trussc::ThermalState::Fair),
        "Serious", sol::var(trussc::ThermalState::Serious),
        "Critical", sol::var(trussc::ThermalState::Critical));
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    {
        sol::usertype<trussc::ChipSoundBundle::Entry> t = lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
