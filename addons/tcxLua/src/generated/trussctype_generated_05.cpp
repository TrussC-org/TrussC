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
        auto constructors = tcxLuaConstructors<trussc::Vec4
            , trussc::Vec4()
            , trussc::Vec4(float, float, float, float)
            , trussc::Vec4(float)
            , trussc::Vec4(const trussc::Vec3 &)
            , trussc::Vec4(const trussc::Vec3 &, float)
            , trussc::Vec4(const trussc::Vec2 &)
            , trussc::Vec4(const trussc::Vec2 &, float)
            , trussc::Vec4(const trussc::Vec2 &, float, float)
        >();
        sol::usertype<trussc::Vec4> t = lua->new_usertype<trussc::Vec4>("Vec4", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a * b; }, [](const trussc::Vec4& a, float b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec4& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a / b; }, [](const trussc::Vec4& a, float b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec4& a, int b){ return a[b]; };
        t["w"] = &trussc::Vec4::w;
        t["x"] = &trussc::Vec4::x;
        t["y"] = &trussc::Vec4::y;
        t["z"] = &trussc::Vec4::z;
        t["dot"] = &trussc::Vec4::dot;
        t["length"] = &trussc::Vec4::length;
        t["lengthSquared"] = &trussc::Vec4::lengthSquared;
        t["lerp"] = &trussc::Vec4::lerp;
        t["normalize"] = &trussc::Vec4::normalize;
        t["normalized"] = &trussc::Vec4::normalized;
        t["set"] = sol::overload([](trussc::Vec4& self, float x_, float y_, float z_, float w_) -> decltype(auto) { return self.set(x_, y_, z_, w_); }, [](trussc::Vec4& self, const trussc::Vec4 & v) -> decltype(auto) { return self.set(v); });
        t["xy"] = &trussc::Vec4::xy;
        t["xyz"] = &trussc::Vec4::xyz;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::FileWriter
            , trussc::FileWriter()
        >();
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter", constructors, sol::call_constructor, constructors);
        t["close"] = &trussc::FileWriter::close;
        t["flush"] = &trussc::FileWriter::flush;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["open"] = sol::overload([](trussc::FileWriter& self, const fs::path & path) { return self.open(path); }, [](trussc::FileWriter& self, const fs::path & path, bool append) { return self.open(path, append); });
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
    }
    {
        sol::usertype<trussc::MouseEventArgs> t = lua->new_usertype<trussc::MouseEventArgs>("MouseEventArgs");
        t["alt"] = &trussc::MouseEventArgs::alt;
        t["button"] = &trussc::MouseEventArgs::button;
        t["consumed"] = &trussc::MouseEventArgs::consumed;
        t["ctrl"] = &trussc::MouseEventArgs::ctrl;
        t["globalPos"] = &trussc::MouseEventArgs::globalPos;
        t["injected"] = &trussc::MouseEventArgs::injected;
        t["pos"] = &trussc::MouseEventArgs::pos;
        t["shift"] = &trussc::MouseEventArgs::shift;
        t["sourceTag"] = &trussc::MouseEventArgs::sourceTag;
        t["super"] = &trussc::MouseEventArgs::super;
        t["x"] = &trussc::MouseEventArgs::x;
        t["y"] = &trussc::MouseEventArgs::y;
        t["syncLegacy"] = &trussc::MouseEventArgs::syncLegacy;
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
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
    }
    lua->new_usertype<trussc::BlendOp>("BlendOp",
        sol::meta_function::equal_to, [](trussc::BlendOp a, trussc::BlendOp b){ return a == b; },
        "Add", sol::var(trussc::BlendOp::Add),
        "Subtract", sol::var(trussc::BlendOp::Subtract),
        "ReverseSubtract", sol::var(trussc::BlendOp::ReverseSubtract),
        "Minimum", sol::var(trussc::BlendOp::Minimum),
        "Maximum", sol::var(trussc::BlendOp::Maximum));
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::TcyMode>("TcyMode",
        sol::meta_function::equal_to, [](trussc::TcyMode a, trussc::TcyMode b){ return a == b; },
        "Rotate", sol::var(trussc::TcyMode::Rotate),
        "Upright", sol::var(trussc::TcyMode::Upright),
        "Combine", sol::var(trussc::TcyMode::Combine));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::ResizeEventArgs> t = lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        t["height"] = &trussc::ResizeEventArgs::height;
        t["width"] = &trussc::ResizeEventArgs::width;
    }
    {
        sol::usertype<trussc::EnumLabelSpan> t = lua->new_usertype<trussc::EnumLabelSpan>("EnumLabelSpan");
        t["count"] = &trussc::EnumLabelSpan::count;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
