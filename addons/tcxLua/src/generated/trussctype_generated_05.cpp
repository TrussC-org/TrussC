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
void tcxLuaGenShard_05(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::RectNodeButton
            , trussc::RectNodeButton()
        >();
        sol::usertype<trussc::RectNodeButton> t = lua->new_usertype<trussc::RectNodeButton>("RectNodeButton", constructors, sol::call_constructor, constructors);
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::RectNodeButton>(&trussc::RectNodeButton::localMatrixChanged);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::RectNodeButton>(&trussc::RectNodeButton::mouseDragged);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::RectNodeButton>(&trussc::RectNodeButton::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::RectNodeButton>(&trussc::RectNodeButton::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::RectNodeButton>(&trussc::RectNodeButton::mouseScrolled);
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["addChild"] = sol::overload([](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child) { return self.addChild(child); }, [](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::RectNodeButton::callAfter;
        t["callEvery"] = &trussc::RectNodeButton::callEvery;
        t["callEveryCatchUp"] = &trussc::RectNodeButton::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::RectNodeButton::cancelAllTimers;
        t["cancelTimer"] = &trussc::RectNodeButton::cancelTimer;
        t["cleanup"] = &trussc::RectNodeButton::cleanup;
        t["destroy"] = &trussc::RectNodeButton::destroy;
        t["disableEvents"] = &trussc::RectNodeButton::disableEvents;
        t["draw"] = &trussc::RectNodeButton::draw;
        t["enableEvents"] = &trussc::RectNodeButton::enableEvents;
        t["findByInstanceId"] = &trussc::RectNodeButton::findByInstanceId;
        t["findHitNode"] = &trussc::RectNodeButton::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::RectNodeButton::findHitNodeFromScreen;
        t["getActive"] = &trussc::RectNodeButton::getActive;
        t["getBottom"] = &trussc::RectNodeButton::getBottom;
        t["getCameraContext"] = &trussc::RectNodeButton::getCameraContext;
        t["getChildCount"] = &trussc::RectNodeButton::getChildCount;
        t["getChildIndex"] = &trussc::RectNodeButton::getChildIndex;
        t["getChildren"] = &trussc::RectNodeButton::getChildren;
        t["getDisplayName"] = &trussc::RectNodeButton::getDisplayName;
        t["getEuler"] = &trussc::RectNodeButton::getEuler;
        t["getEulerDeg"] = &trussc::RectNodeButton::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::RectNodeButton::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::RectNodeButton::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::RectNodeButton::getGlobalPos;
        t["getHeight"] = &trussc::RectNodeButton::getHeight;
        t["getInstanceId"] = &trussc::RectNodeButton::getInstanceId;
        t["getLeft"] = &trussc::RectNodeButton::getLeft;
        t["getLocalMatrix"] = &trussc::RectNodeButton::getLocalMatrix;
        t["getModByTypeName"] = &trussc::RectNodeButton::getModByTypeName;
        t["getModTypeNames"] = &trussc::RectNodeButton::getModTypeNames;
        t["getMods"] = &trussc::RectNodeButton::getMods;
        t["getMouseX"] = &trussc::RectNodeButton::getMouseX;
        t["getMouseY"] = &trussc::RectNodeButton::getMouseY;
        t["getName"] = &trussc::RectNodeButton::getName;
        t["getPMouseX"] = &trussc::RectNodeButton::getPMouseX;
        t["getPMouseY"] = &trussc::RectNodeButton::getPMouseY;
        t["getParent"] = &trussc::RectNodeButton::getParent;
        t["getPos"] = &trussc::RectNodeButton::getPos;
        t["getQuaternion"] = &trussc::RectNodeButton::getQuaternion;
        t["getRight"] = &trussc::RectNodeButton::getRight;
        t["getRot"] = &trussc::RectNodeButton::getRot;
        t["getRotDeg"] = &trussc::RectNodeButton::getRotDeg;
        t["getScale"] = &trussc::RectNodeButton::getScale;
        t["getScaleX"] = &trussc::RectNodeButton::getScaleX;
        t["getScaleY"] = &trussc::RectNodeButton::getScaleY;
        t["getScaleZ"] = &trussc::RectNodeButton::getScaleZ;
        t["getSize"] = &trussc::RectNodeButton::getSize;
        t["getTop"] = &trussc::RectNodeButton::getTop;
        t["getTypeName"] = &trussc::RectNodeButton::getTypeName;
        t["getVisible"] = &trussc::RectNodeButton::getVisible;
        t["getWidth"] = &trussc::RectNodeButton::getWidth;
        t["getX"] = &trussc::RectNodeButton::getX;
        t["getY"] = &trussc::RectNodeButton::getY;
        t["getZ"] = &trussc::RectNodeButton::getZ;
        t["globalToLocal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::RectNodeButton::hasName;
        t["hitTest"] = [](trussc::RectNodeButton& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child) { return self.insertChild(index, child); }, [](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::RectNodeButton::isActive;
        t["isClipping"] = &trussc::RectNodeButton::isClipping;
        t["isDead"] = &trussc::RectNodeButton::isDead;
        t["isEventsEnabled"] = &trussc::RectNodeButton::isEventsEnabled;
        t["isMouseOver"] = &trussc::RectNodeButton::isMouseOver;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["isVisible"] = &trussc::RectNodeButton::isVisible;
        t["localToGlobal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::RectNodeButton::moveToBack;
        t["moveToFront"] = &trussc::RectNodeButton::moveToFront;
        t["onChildAdded"] = &trussc::RectNodeButton::onChildAdded;
        t["onChildRemoved"] = &trussc::RectNodeButton::onChildRemoved;
        t["removeAllChildren"] = &trussc::RectNodeButton::removeAllChildren;
        t["removeChild"] = &trussc::RectNodeButton::removeChild;
        t["setActive"] = &trussc::RectNodeButton::setActive;
        t["setCameraContext"] = &trussc::RectNodeButton::setCameraContext;
        t["setClipping"] = &trussc::RectNodeButton::setClipping;
        t["setEuler"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::RectNodeButton& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::RectNodeButton::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::RectNodeButton::setHeight;
        t["setIsActive"] = &trussc::RectNodeButton::setIsActive;
        t["setIsVisible"] = &trussc::RectNodeButton::setIsVisible;
        t["setName"] = &trussc::RectNodeButton::setName;
        t["setPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::RectNodeButton::setQuaternion;
        t["setRect"] = &trussc::RectNodeButton::setRect;
        t["setRot"] = &trussc::RectNodeButton::setRot;
        t["setRotDeg"] = &trussc::RectNodeButton::setRotDeg;
        t["setScale"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::RectNodeButton& self, float uniform) { return self.setScale(uniform); }, [](trussc::RectNodeButton& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::RectNodeButton& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::RectNodeButton::setScaleX;
        t["setScaleY"] = &trussc::RectNodeButton::setScaleY;
        t["setScaleZ"] = &trussc::RectNodeButton::setScaleZ;
        t["setSize"] = sol::overload([](trussc::RectNodeButton& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNodeButton& self, float size) { return self.setSize(size); }, [](trussc::RectNodeButton& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVisible"] = &trussc::RectNodeButton::setVisible;
        t["setWidth"] = &trussc::RectNodeButton::setWidth;
        t["setX"] = &trussc::RectNodeButton::setX;
        t["setY"] = &trussc::RectNodeButton::setY;
        t["setZ"] = &trussc::RectNodeButton::setZ;
        t["setup"] = &trussc::RectNodeButton::setup;
        t["update"] = &trussc::RectNodeButton::update;
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
        auto constructors = tcxLuaConstructors<trussc::Logger
            , trussc::Logger()
        >();
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger", constructors, sol::call_constructor, constructors);
        t["onLog"] = tcx_lua_detail::eventProperty<trussc::Logger>(&trussc::Logger::onLog);
        t["closeFile"] = &trussc::Logger::closeFile;
        t["getConsoleLogLevel"] = &trussc::Logger::getConsoleLogLevel;
        t["getFileLogLevel"] = &trussc::Logger::getFileLogLevel;
        t["getLogFilePath"] = &trussc::Logger::getLogFilePath;
        t["getSystemLogLevel"] = &trussc::Logger::getSystemLogLevel;
        t["isFileOpen"] = &trussc::Logger::isFileOpen;
        t["log"] = &trussc::Logger::log;
        t["setConsoleLogLevel"] = &trussc::Logger::setConsoleLogLevel;
        t["setFileLogLevel"] = &trussc::Logger::setFileLogLevel;
        t["setLogFile"] = &trussc::Logger::setLogFile;
        t["setLogLevel"] = &trussc::Logger::setLogLevel;
        t["setSystemLogLevel"] = &trussc::Logger::setSystemLogLevel;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLab
            , trussc::ColorOKLab()
            , trussc::ColorOKLab(float, float, float)
            , trussc::ColorOKLab(float, float, float, float)
        >();
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab", constructors, sol::call_constructor, constructors);
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["b"] = &trussc::ColorOKLab::b;
        t["lerp"] = &trussc::ColorOKLab::lerp;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
    }
    lua->new_usertype<trussc::StencilTest>("StencilTest",
        sol::meta_function::equal_to, [](trussc::StencilTest a, trussc::StencilTest b){ return a == b; },
        "Always", sol::var(trussc::StencilTest::Always),
        "Never", sol::var(trussc::StencilTest::Never),
        "Less", sol::var(trussc::StencilTest::Less),
        "Equal", sol::var(trussc::StencilTest::Equal),
        "LessEqual", sol::var(trussc::StencilTest::LessEqual),
        "Greater", sol::var(trussc::StencilTest::Greater),
        "NotEqual", sol::var(trussc::StencilTest::NotEqual),
        "GreaterEqual", sol::var(trussc::StencilTest::GreaterEqual));
    {
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    lua->new_usertype<trussc::StrokeMesh::JoinType>("StrokeMeshJoinType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::JoinType a, trussc::StrokeMesh::JoinType b){ return a == b; },
        "JOIN_MITER", sol::var(trussc::StrokeMesh::JoinType::JOIN_MITER),
        "JOIN_ROUND", sol::var(trussc::StrokeMesh::JoinType::JOIN_ROUND),
        "JOIN_BEVEL", sol::var(trussc::StrokeMesh::JoinType::JOIN_BEVEL));
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::StrokeCap>("StrokeCap",
        sol::meta_function::equal_to, [](trussc::StrokeCap a, trussc::StrokeCap b){ return a == b; },
        "Butt", sol::var(trussc::StrokeCap::Butt),
        "Round", sol::var(trussc::StrokeCap::Round),
        "Square", sol::var(trussc::StrokeCap::Square));
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
    {
        sol::usertype<trussc::SendResult> t = lua->new_usertype<trussc::SendResult>("SendResult");
        t["error"] = &trussc::SendResult::error;
        t["id"] = &trussc::SendResult::id;
        t["ok"] = &trussc::SendResult::ok;
    }
    {
        sol::usertype<trussc::FocusEventArgs> t = lua->new_usertype<trussc::FocusEventArgs>("FocusEventArgs");
        t["focused"] = &trussc::FocusEventArgs::focused;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
