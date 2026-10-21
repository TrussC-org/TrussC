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
void tcxLuaGenShard_09(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::RectNode> t = lua->new_usertype<trussc::RectNode>("RectNode");
        t["localMatrixChanged"] = &trussc::RectNode::localMatrixChanged;
        t["mouseDragged"] = &trussc::RectNode::mouseDragged;
        t["mousePressed"] = &trussc::RectNode::mousePressed;
        t["mouseReleased"] = &trussc::RectNode::mouseReleased;
        t["mouseScrolled"] = &trussc::RectNode::mouseScrolled;
        t["addChild"] = sol::overload([](trussc::RectNode& self, trussc::RectNode::Ptr child) { return self.addChild(child); }, [](trussc::RectNode& self, trussc::RectNode::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::RectNode::callAfter;
        t["callEvery"] = &trussc::RectNode::callEvery;
        t["callEveryCatchUp"] = &trussc::RectNode::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::RectNode::cancelAllTimers;
        t["cancelTimer"] = &trussc::RectNode::cancelTimer;
        t["cleanup"] = &trussc::RectNode::cleanup;
        t["destroy"] = &trussc::RectNode::destroy;
        t["disableEvents"] = &trussc::RectNode::disableEvents;
        t["draw"] = &trussc::RectNode::draw;
        t["enableEvents"] = &trussc::RectNode::enableEvents;
        t["findByInstanceId"] = &trussc::RectNode::findByInstanceId;
        t["findHitNode"] = &trussc::RectNode::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::RectNode::findHitNodeFromScreen;
        t["getActive"] = &trussc::RectNode::getActive;
        t["getBottom"] = &trussc::RectNode::getBottom;
        t["getCameraContext"] = &trussc::RectNode::getCameraContext;
        t["getChildCount"] = &trussc::RectNode::getChildCount;
        t["getChildIndex"] = &trussc::RectNode::getChildIndex;
        t["getChildren"] = &trussc::RectNode::getChildren;
        t["getDisplayName"] = &trussc::RectNode::getDisplayName;
        t["getEuler"] = &trussc::RectNode::getEuler;
        t["getEulerDeg"] = &trussc::RectNode::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::RectNode::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::RectNode::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::RectNode::getGlobalPos;
        t["getHeight"] = &trussc::RectNode::getHeight;
        t["getInstanceId"] = &trussc::RectNode::getInstanceId;
        t["getLeft"] = &trussc::RectNode::getLeft;
        t["getLocalMatrix"] = &trussc::RectNode::getLocalMatrix;
        t["getModByTypeName"] = &trussc::RectNode::getModByTypeName;
        t["getModTypeNames"] = &trussc::RectNode::getModTypeNames;
        t["getMods"] = &trussc::RectNode::getMods;
        t["getMouseX"] = &trussc::RectNode::getMouseX;
        t["getMouseY"] = &trussc::RectNode::getMouseY;
        t["getName"] = &trussc::RectNode::getName;
        t["getPMouseX"] = &trussc::RectNode::getPMouseX;
        t["getPMouseY"] = &trussc::RectNode::getPMouseY;
        t["getParent"] = &trussc::RectNode::getParent;
        t["getPos"] = &trussc::RectNode::getPos;
        t["getQuaternion"] = &trussc::RectNode::getQuaternion;
        t["getRight"] = &trussc::RectNode::getRight;
        t["getRot"] = &trussc::RectNode::getRot;
        t["getRotDeg"] = &trussc::RectNode::getRotDeg;
        t["getScale"] = &trussc::RectNode::getScale;
        t["getScaleX"] = &trussc::RectNode::getScaleX;
        t["getScaleY"] = &trussc::RectNode::getScaleY;
        t["getScaleZ"] = &trussc::RectNode::getScaleZ;
        t["getSize"] = &trussc::RectNode::getSize;
        t["getTop"] = &trussc::RectNode::getTop;
        t["getTypeName"] = &trussc::RectNode::getTypeName;
        t["getVisible"] = &trussc::RectNode::getVisible;
        t["getWidth"] = &trussc::RectNode::getWidth;
        t["getX"] = &trussc::RectNode::getX;
        t["getY"] = &trussc::RectNode::getY;
        t["getZ"] = &trussc::RectNode::getZ;
        t["globalToLocal"] = [](trussc::RectNode& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::RectNode::hasName;
        t["hitTest"] = [](trussc::RectNode& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::RectNode& self, size_t index, trussc::RectNode::Ptr child) { return self.insertChild(index, child); }, [](trussc::RectNode& self, size_t index, trussc::RectNode::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::RectNode::isActive;
        t["isClipping"] = &trussc::RectNode::isClipping;
        t["isDead"] = &trussc::RectNode::isDead;
        t["isEventsEnabled"] = &trussc::RectNode::isEventsEnabled;
        t["isMouseOver"] = &trussc::RectNode::isMouseOver;
        t["isVisible"] = &trussc::RectNode::isVisible;
        t["localToGlobal"] = [](trussc::RectNode& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::RectNode::moveToBack;
        t["moveToFront"] = &trussc::RectNode::moveToFront;
        t["onChildAdded"] = &trussc::RectNode::onChildAdded;
        t["onChildRemoved"] = &trussc::RectNode::onChildRemoved;
        t["removeAllChildren"] = &trussc::RectNode::removeAllChildren;
        t["removeChild"] = &trussc::RectNode::removeChild;
        t["setActive"] = &trussc::RectNode::setActive;
        t["setCameraContext"] = &trussc::RectNode::setCameraContext;
        t["setClipping"] = &trussc::RectNode::setClipping;
        t["setEuler"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::RectNode& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::RectNode::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::RectNode& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::RectNode& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::RectNode::setHeight;
        t["setIsActive"] = &trussc::RectNode::setIsActive;
        t["setIsVisible"] = &trussc::RectNode::setIsVisible;
        t["setName"] = &trussc::RectNode::setName;
        t["setPos"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::RectNode& self, float x, float y) { return self.setPos(x, y); }, [](trussc::RectNode& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::RectNode::setQuaternion;
        t["setRect"] = &trussc::RectNode::setRect;
        t["setRot"] = &trussc::RectNode::setRot;
        t["setRotDeg"] = &trussc::RectNode::setRotDeg;
        t["setScale"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::RectNode& self, float uniform) { return self.setScale(uniform); }, [](trussc::RectNode& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::RectNode& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::RectNode::setScaleX;
        t["setScaleY"] = &trussc::RectNode::setScaleY;
        t["setScaleZ"] = &trussc::RectNode::setScaleZ;
        t["setSize"] = sol::overload([](trussc::RectNode& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNode& self, float size) { return self.setSize(size); }, [](trussc::RectNode& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVisible"] = &trussc::RectNode::setVisible;
        t["setWidth"] = &trussc::RectNode::setWidth;
        t["setX"] = &trussc::RectNode::setX;
        t["setY"] = &trussc::RectNode::setY;
        t["setZ"] = &trussc::RectNode::setZ;
        t["setup"] = &trussc::RectNode::setup;
        t["update"] = &trussc::RectNode::update;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Vec2
            , trussc::Vec2()
            , trussc::Vec2(float, float)
            , trussc::Vec2(float)
        >();
        sol::usertype<trussc::Vec2> t = lua->new_usertype<trussc::Vec2>("Vec2", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec2& a, float b){ return a * b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec2& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec2& a, float b){ return a / b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec2& a, int b){ return a[b]; };
        t["x"] = &trussc::Vec2::x;
        t["y"] = &trussc::Vec2::y;
        t["angle"] = sol::overload([](trussc::Vec2& self) { return self.angle(); }, [](trussc::Vec2& self, const trussc::Vec2 & v) { return self.angle(v); });
        t["cross"] = &trussc::Vec2::cross;
        t["distance"] = &trussc::Vec2::distance;
        t["distanceSquared"] = &trussc::Vec2::distanceSquared;
        t["dot"] = &trussc::Vec2::dot;
        t["length"] = &trussc::Vec2::length;
        t["lengthSquared"] = &trussc::Vec2::lengthSquared;
        t["lerp"] = &trussc::Vec2::lerp;
        t["limit"] = &trussc::Vec2::limit;
        t["normalize"] = &trussc::Vec2::normalize;
        t["normalized"] = &trussc::Vec2::normalized;
        t["perpendicular"] = &trussc::Vec2::perpendicular;
        t["reflected"] = &trussc::Vec2::reflected;
        t["rotate"] = &trussc::Vec2::rotate;
        t["rotated"] = &trussc::Vec2::rotated;
        t["set"] = sol::overload([](trussc::Vec2& self, float x_, float y_) -> decltype(auto) { return self.set(x_, y_); }, [](trussc::Vec2& self, const trussc::Vec2 & v) -> decltype(auto) { return self.set(v); });
        t["fromAngle"] = sol::overload([](float radians) { return trussc::Vec2::fromAngle(radians); }, [](float radians, float length) { return trussc::Vec2::fromAngle(radians, length); });
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
        auto constructors = tcxLuaConstructors<trussc::Logger
            , trussc::Logger()
        >();
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger", constructors, sol::call_constructor, constructors);
        t["onLog"] = &trussc::Logger::onLog;
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
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    {
        sol::usertype<trussc::SerialDisconnectEventArgs> t = lua->new_usertype<trussc::SerialDisconnectEventArgs>("SerialDisconnectEventArgs");
        t["baudRate"] = &trussc::SerialDisconnectEventArgs::baudRate;
        t["portName"] = &trussc::SerialDisconnectEventArgs::portName;
        t["reason"] = &trussc::SerialDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::SerialDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    {
        sol::usertype<trussc::ScreenshotSaveEventArgs> t = lua->new_usertype<trussc::ScreenshotSaveEventArgs>("ScreenshotSaveEventArgs");
        t["path"] = &trussc::ScreenshotSaveEventArgs::path;
        t["result"] = &trussc::ScreenshotSaveEventArgs::result;
    }
    {
        sol::usertype<trussc::HeadlessSettings> t = lua->new_usertype<trussc::HeadlessSettings>("HeadlessSettings");
        t["targetFps"] = &trussc::HeadlessSettings::targetFps;
        t["setFps"] = &trussc::HeadlessSettings::setFps;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
