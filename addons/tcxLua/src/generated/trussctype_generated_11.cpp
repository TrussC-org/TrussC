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
void tcxLuaGenShard_11(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Node
            , trussc::Node()
        >();
        sol::usertype<trussc::Node> t = lua->new_usertype<trussc::Node>("Node", constructors, sol::call_constructor, constructors);
        t["localMatrixChanged"] = &trussc::Node::localMatrixChanged;
        t["addChild"] = sol::overload([](trussc::Node& self, trussc::Node::Ptr child) { return self.addChild(child); }, [](trussc::Node& self, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::Node::callAfter;
        t["callEvery"] = &trussc::Node::callEvery;
        t["callEveryCatchUp"] = &trussc::Node::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::Node::cancelAllTimers;
        t["cancelTimer"] = &trussc::Node::cancelTimer;
        t["cleanup"] = &trussc::Node::cleanup;
        t["destroy"] = &trussc::Node::destroy;
        t["disableEvents"] = &trussc::Node::disableEvents;
        t["draw"] = &trussc::Node::draw;
        t["enableEvents"] = &trussc::Node::enableEvents;
        t["findByInstanceId"] = &trussc::Node::findByInstanceId;
        t["findHitNode"] = &trussc::Node::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::Node::findHitNodeFromScreen;
        t["getActive"] = &trussc::Node::getActive;
        t["getCameraContext"] = &trussc::Node::getCameraContext;
        t["getChildCount"] = &trussc::Node::getChildCount;
        t["getChildIndex"] = &trussc::Node::getChildIndex;
        t["getChildren"] = &trussc::Node::getChildren;
        t["getDisplayName"] = &trussc::Node::getDisplayName;
        t["getEuler"] = &trussc::Node::getEuler;
        t["getEulerDeg"] = &trussc::Node::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::Node::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::Node::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::Node::getGlobalPos;
        t["getInstanceId"] = &trussc::Node::getInstanceId;
        t["getLocalMatrix"] = &trussc::Node::getLocalMatrix;
        t["getModByTypeName"] = &trussc::Node::getModByTypeName;
        t["getModTypeNames"] = &trussc::Node::getModTypeNames;
        t["getMods"] = &trussc::Node::getMods;
        t["getMouseX"] = &trussc::Node::getMouseX;
        t["getMouseY"] = &trussc::Node::getMouseY;
        t["getName"] = &trussc::Node::getName;
        t["getPMouseX"] = &trussc::Node::getPMouseX;
        t["getPMouseY"] = &trussc::Node::getPMouseY;
        t["getParent"] = &trussc::Node::getParent;
        t["getPos"] = &trussc::Node::getPos;
        t["getQuaternion"] = &trussc::Node::getQuaternion;
        t["getRot"] = &trussc::Node::getRot;
        t["getRotDeg"] = &trussc::Node::getRotDeg;
        t["getScale"] = &trussc::Node::getScale;
        t["getScaleX"] = &trussc::Node::getScaleX;
        t["getScaleY"] = &trussc::Node::getScaleY;
        t["getScaleZ"] = &trussc::Node::getScaleZ;
        t["getTypeName"] = &trussc::Node::getTypeName;
        t["getVisible"] = &trussc::Node::getVisible;
        t["getX"] = &trussc::Node::getX;
        t["getY"] = &trussc::Node::getY;
        t["getZ"] = &trussc::Node::getZ;
        t["globalToLocal"] = [](trussc::Node& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::Node::hasName;
        t["insertChild"] = sol::overload([](trussc::Node& self, size_t index, trussc::Node::Ptr child) { return self.insertChild(index, child); }, [](trussc::Node& self, size_t index, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::Node::isActive;
        t["isDead"] = &trussc::Node::isDead;
        t["isEventsEnabled"] = &trussc::Node::isEventsEnabled;
        t["isMouseOver"] = &trussc::Node::isMouseOver;
        t["isVisible"] = &trussc::Node::isVisible;
        t["localToGlobal"] = [](trussc::Node& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::Node::moveToBack;
        t["moveToFront"] = &trussc::Node::moveToFront;
        t["onChildAdded"] = &trussc::Node::onChildAdded;
        t["onChildRemoved"] = &trussc::Node::onChildRemoved;
        t["removeAllChildren"] = &trussc::Node::removeAllChildren;
        t["removeChild"] = &trussc::Node::removeChild;
        t["setActive"] = &trussc::Node::setActive;
        t["setCameraContext"] = &trussc::Node::setCameraContext;
        t["setEuler"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::Node& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::Node::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::Node& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setIsActive"] = &trussc::Node::setIsActive;
        t["setIsVisible"] = &trussc::Node::setIsVisible;
        t["setName"] = &trussc::Node::setName;
        t["setPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::Node& self, float x, float y) { return self.setPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::Node::setQuaternion;
        t["setRot"] = &trussc::Node::setRot;
        t["setRotDeg"] = &trussc::Node::setRotDeg;
        t["setScale"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::Node& self, float uniform) { return self.setScale(uniform); }, [](trussc::Node& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::Node& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::Node::setScaleX;
        t["setScaleY"] = &trussc::Node::setScaleY;
        t["setScaleZ"] = &trussc::Node::setScaleZ;
        t["setVisible"] = &trussc::Node::setVisible;
        t["setX"] = &trussc::Node::setX;
        t["setY"] = &trussc::Node::setY;
        t["setZ"] = &trussc::Node::setZ;
        t["setup"] = &trussc::Node::setup;
        t["update"] = &trussc::Node::update;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::ScreenRecorder
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::ScreenRecorder()
#endif
        >();
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::ScreenRecorder::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stop"] = &trussc::ScreenRecorder::stop;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["writer"] = &trussc::ScreenRecorder::writer;
#endif
    }
#endif
    lua->new_usertype<trussc::Cursor>("Cursor",
        sol::meta_function::equal_to, [](trussc::Cursor a, trussc::Cursor b){ return a == b; },
        "Default", sol::var(trussc::Cursor::Default),
        "Arrow", sol::var(trussc::Cursor::Arrow),
        "IBeam", sol::var(trussc::Cursor::IBeam),
        "Crosshair", sol::var(trussc::Cursor::Crosshair),
        "Hand", sol::var(trussc::Cursor::Hand),
        "ResizeEW", sol::var(trussc::Cursor::ResizeEW),
        "ResizeNS", sol::var(trussc::Cursor::ResizeNS),
        "ResizeNWSE", sol::var(trussc::Cursor::ResizeNWSE),
        "ResizeNESW", sol::var(trussc::Cursor::ResizeNESW),
        "ResizeAll", sol::var(trussc::Cursor::ResizeAll),
        "NotAllowed", sol::var(trussc::Cursor::NotAllowed),
        "Custom0", sol::var(trussc::Cursor::Custom0),
        "Custom1", sol::var(trussc::Cursor::Custom1),
        "Custom2", sol::var(trussc::Cursor::Custom2),
        "Custom3", sol::var(trussc::Cursor::Custom3),
        "Custom4", sol::var(trussc::Cursor::Custom4),
        "Custom5", sol::var(trussc::Cursor::Custom5),
        "Custom6", sol::var(trussc::Cursor::Custom6),
        "Custom7", sol::var(trussc::Cursor::Custom7),
        "Custom8", sol::var(trussc::Cursor::Custom8),
        "Custom9", sol::var(trussc::Cursor::Custom9),
        "Custom10", sol::var(trussc::Cursor::Custom10),
        "Custom11", sol::var(trussc::Cursor::Custom11),
        "Custom12", sol::var(trussc::Cursor::Custom12),
        "Custom13", sol::var(trussc::Cursor::Custom13),
        "Custom14", sol::var(trussc::Cursor::Custom14),
        "Custom15", sol::var(trussc::Cursor::Custom15));
    {
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["address"] = &trussc::NetworkInterface::address;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["name"] = &trussc::NetworkInterface::name;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    {
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["synced"] = &trussc::FpsSettings::synced;
        t["updateFps"] = &trussc::FpsSettings::updateFps;
    }
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::ChipSoundBundle::Entry> t = lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
