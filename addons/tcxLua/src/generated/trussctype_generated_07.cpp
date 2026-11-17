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
void tcxLuaGenShard_07(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::ScrollBar> t = lua->new_usertype<trussc::ScrollBar>("ScrollBar");
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::localMatrixChanged);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseDragged);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseScrolled);
        t["addChild"] = sol::overload([](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child) { return self.addChild(child); }, [](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::ScrollBar::callAfter;
        t["callEvery"] = &trussc::ScrollBar::callEvery;
        t["callEveryCatchUp"] = &trussc::ScrollBar::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::ScrollBar::cancelAllTimers;
        t["cancelTimer"] = &trussc::ScrollBar::cancelTimer;
        t["cleanup"] = &trussc::ScrollBar::cleanup;
        t["destroy"] = &trussc::ScrollBar::destroy;
        t["disableEvents"] = &trussc::ScrollBar::disableEvents;
        t["enableEvents"] = &trussc::ScrollBar::enableEvents;
        t["findByInstanceId"] = &trussc::ScrollBar::findByInstanceId;
        t["findHitNode"] = &trussc::ScrollBar::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::ScrollBar::findHitNodeFromScreen;
        t["getActive"] = &trussc::ScrollBar::getActive;
        t["getBarColor"] = &trussc::ScrollBar::getBarColor;
        t["getBarWidth"] = &trussc::ScrollBar::getBarWidth;
        t["getBottom"] = &trussc::ScrollBar::getBottom;
        t["getCameraContext"] = &trussc::ScrollBar::getCameraContext;
        t["getChildCount"] = &trussc::ScrollBar::getChildCount;
        t["getChildIndex"] = &trussc::ScrollBar::getChildIndex;
        t["getChildren"] = &trussc::ScrollBar::getChildren;
        t["getDisplayName"] = &trussc::ScrollBar::getDisplayName;
        t["getEuler"] = &trussc::ScrollBar::getEuler;
        t["getEulerDeg"] = &trussc::ScrollBar::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::ScrollBar::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::ScrollBar::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::ScrollBar::getGlobalPos;
        t["getHeight"] = &trussc::ScrollBar::getHeight;
        t["getInstanceId"] = &trussc::ScrollBar::getInstanceId;
        t["getLeft"] = &trussc::ScrollBar::getLeft;
        t["getLocalMatrix"] = &trussc::ScrollBar::getLocalMatrix;
        t["getMargin"] = &trussc::ScrollBar::getMargin;
        t["getModByTypeName"] = &trussc::ScrollBar::getModByTypeName;
        t["getModTypeNames"] = &trussc::ScrollBar::getModTypeNames;
        t["getMods"] = &trussc::ScrollBar::getMods;
        t["getMouseX"] = &trussc::ScrollBar::getMouseX;
        t["getMouseY"] = &trussc::ScrollBar::getMouseY;
        t["getName"] = &trussc::ScrollBar::getName;
        t["getOffset"] = &trussc::ScrollBar::getOffset;
        t["getPMouseX"] = &trussc::ScrollBar::getPMouseX;
        t["getPMouseY"] = &trussc::ScrollBar::getPMouseY;
        t["getParent"] = &trussc::ScrollBar::getParent;
        t["getPos"] = &trussc::ScrollBar::getPos;
        t["getQuaternion"] = &trussc::ScrollBar::getQuaternion;
        t["getRight"] = &trussc::ScrollBar::getRight;
        t["getRot"] = &trussc::ScrollBar::getRot;
        t["getRotDeg"] = &trussc::ScrollBar::getRotDeg;
        t["getScale"] = &trussc::ScrollBar::getScale;
        t["getScaleX"] = &trussc::ScrollBar::getScaleX;
        t["getScaleY"] = &trussc::ScrollBar::getScaleY;
        t["getScaleZ"] = &trussc::ScrollBar::getScaleZ;
        t["getSize"] = &trussc::ScrollBar::getSize;
        t["getTop"] = &trussc::ScrollBar::getTop;
        t["getTypeName"] = &trussc::ScrollBar::getTypeName;
        t["getVisible"] = &trussc::ScrollBar::getVisible;
        t["getWidth"] = &trussc::ScrollBar::getWidth;
        t["getX"] = &trussc::ScrollBar::getX;
        t["getY"] = &trussc::ScrollBar::getY;
        t["getZ"] = &trussc::ScrollBar::getZ;
        t["globalToLocal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::ScrollBar::hasName;
        t["hitTest"] = [](trussc::ScrollBar& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child) { return self.insertChild(index, child); }, [](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::ScrollBar::isActive;
        t["isClipping"] = &trussc::ScrollBar::isClipping;
        t["isDead"] = &trussc::ScrollBar::isDead;
        t["isEventsEnabled"] = &trussc::ScrollBar::isEventsEnabled;
        t["isMouseOver"] = &trussc::ScrollBar::isMouseOver;
        t["isVisible"] = &trussc::ScrollBar::isVisible;
        t["localToGlobal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::ScrollBar::moveToBack;
        t["moveToFront"] = &trussc::ScrollBar::moveToFront;
        t["onChildAdded"] = &trussc::ScrollBar::onChildAdded;
        t["onChildRemoved"] = &trussc::ScrollBar::onChildRemoved;
        t["removeAllChildren"] = &trussc::ScrollBar::removeAllChildren;
        t["removeChild"] = &trussc::ScrollBar::removeChild;
        t["setActive"] = &trussc::ScrollBar::setActive;
        t["setBarColor"] = &trussc::ScrollBar::setBarColor;
        t["setBarWidth"] = &trussc::ScrollBar::setBarWidth;
        t["setCameraContext"] = &trussc::ScrollBar::setCameraContext;
        t["setClipping"] = &trussc::ScrollBar::setClipping;
        t["setEuler"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::ScrollBar& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::ScrollBar::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::ScrollBar& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::ScrollBar::setHeight;
        t["setIsActive"] = &trussc::ScrollBar::setIsActive;
        t["setIsVisible"] = &trussc::ScrollBar::setIsVisible;
        t["setMargin"] = &trussc::ScrollBar::setMargin;
        t["setName"] = &trussc::ScrollBar::setName;
        t["setPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::ScrollBar& self, float x, float y) { return self.setPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::ScrollBar::setQuaternion;
        t["setRect"] = &trussc::ScrollBar::setRect;
        t["setRot"] = &trussc::ScrollBar::setRot;
        t["setRotDeg"] = &trussc::ScrollBar::setRotDeg;
        t["setScale"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::ScrollBar& self, float uniform) { return self.setScale(uniform); }, [](trussc::ScrollBar& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::ScrollBar& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::ScrollBar::setScaleX;
        t["setScaleY"] = &trussc::ScrollBar::setScaleY;
        t["setScaleZ"] = &trussc::ScrollBar::setScaleZ;
        t["setSize"] = sol::overload([](trussc::ScrollBar& self, float w, float h) { return self.setSize(w, h); }, [](trussc::ScrollBar& self, float size) { return self.setSize(size); }, [](trussc::ScrollBar& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVisible"] = &trussc::ScrollBar::setVisible;
        t["setWidth"] = &trussc::ScrollBar::setWidth;
        t["setX"] = &trussc::ScrollBar::setX;
        t["setY"] = &trussc::ScrollBar::setY;
        t["setZ"] = &trussc::ScrollBar::setZ;
        t["setup"] = &trussc::ScrollBar::setup;
        t["update"] = &trussc::ScrollBar::update;
        t["updateFromContainer"] = &trussc::ScrollBar::updateFromContainer;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Window
            , trussc::Window()
        >();
        sol::usertype<trussc::Window> t = lua->new_usertype<trussc::Window>("Window", constructors, sol::call_constructor, constructors);
        t["bringToFront"] = &trussc::Window::bringToFront;
        t["close"] = &trussc::Window::close;
        t["destroyApp"] = &trussc::Window::destroyApp;
        t["events"] = &trussc::Window::events;
        t["getApp"] = &trussc::Window::getApp;
        t["getFps"] = &trussc::Window::getFps;
        t["getHeight"] = &trussc::Window::getHeight;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getPosition"] = &trussc::Window::getPosition;
#endif
        t["getTitle"] = &trussc::Window::getTitle;
        t["getWidth"] = &trussc::Window::getWidth;
        t["injectInput"] = sol::overload([](trussc::Window& self, const trussc::InputEvent & event) { return self.injectInput(event); }, [](trussc::Window& self, const trussc::InputEvent & event, const std::string & tag) { return self.injectInput(event, tag); });
        t["isFocused"] = &trussc::Window::isFocused;
        t["isFullscreen"] = &trussc::Window::isFullscreen;
        t["isOccluded"] = &trussc::Window::isOccluded;
        t["isOpen"] = &trussc::Window::isOpen;
        t["isVisible"] = &trussc::Window::isVisible;
        t["setClearColor"] = &trussc::Window::setClearColor;
        t["setDrawWhileHidden"] = &trussc::Window::setDrawWhileHidden;
        t["setFps"] = &trussc::Window::setFps;
        t["setFullscreen"] = &trussc::Window::setFullscreen;
        t["setIndependentFps"] = &trussc::Window::setIndependentFps;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setPosition"] = &trussc::Window::setPosition;
#endif
        t["setQuitOnClose"] = &trussc::Window::setQuitOnClose;
        t["setSize"] = &trussc::Window::setSize;
        t["setTitle"] = &trussc::Window::setTitle;
        t["setVisible"] = &trussc::Window::setVisible;
        t["toggleFullscreen"] = &trussc::Window::toggleFullscreen;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::IVec3
            , trussc::IVec3()
            , trussc::IVec3(int, int, int)
            , trussc::IVec3(int)
            , trussc::IVec3(const trussc::IVec2 &)
            , trussc::IVec3(const trussc::IVec2 &, int)
        >();
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec3& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec3& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; };
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
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
    {
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["audio"] = &trussc::VideoRecordSettings::audio;
        t["audioBitrate"] = &trussc::VideoRecordSettings::audioBitrate;
        t["audioChannels"] = &trussc::VideoRecordSettings::audioChannels;
        t["audioSampleRate"] = &trussc::VideoRecordSettings::audioSampleRate;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["duration"] = &trussc::VideoRecordSettings::duration;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
    }
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled),
        "Custom", sol::var(trussc::BlendMode::Custom));
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["synced"] = &trussc::FpsSettings::synced;
        t["updateFps"] = &trussc::FpsSettings::updateFps;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    {
        sol::usertype<trussc::VideoErrorEventArgs> t = lua->new_usertype<trussc::VideoErrorEventArgs>("VideoErrorEventArgs");
        t["errorCode"] = &trussc::VideoErrorEventArgs::errorCode;
        t["message"] = &trussc::VideoErrorEventArgs::message;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
    {
        sol::usertype<trussc::BackPressedEventArgs> t = lua->new_usertype<trussc::BackPressedEventArgs>("BackPressedEventArgs");
        t["cancel"] = &trussc::BackPressedEventArgs::cancel;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
