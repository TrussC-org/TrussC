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
    {
        auto constructors = tcxLuaConstructors<trussc::Color
            , trussc::Color()
            , trussc::Color(float, float, float)
            , trussc::Color(float, float, float, float)
            , trussc::Color(float)
            , trussc::Color(float, float)
        >();
        sol::usertype<trussc::Color> t = lua->new_usertype<trussc::Color>("Color", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::Color& a, float b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::Color& a, const trussc::Color & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Color& a, const trussc::Color & b){ return a - b; };
        t[sol::meta_function::division] = [](const trussc::Color& a, float b){ return a / b; };
        t[sol::meta_function::equal_to] = [](const trussc::Color& a, const trussc::Color & b){ return a == b; };
        t["a"] = &trussc::Color::a;
        t["b"] = &trussc::Color::b;
        t["g"] = &trussc::Color::g;
        t["r"] = &trussc::Color::r;
        t["clamped"] = &trussc::Color::clamped;
        t["lerp"] = &trussc::Color::lerp;
        t["lerpHSB"] = &trussc::Color::lerpHSB;
        t["lerpLinear"] = &trussc::Color::lerpLinear;
        t["lerpOKLCH"] = &trussc::Color::lerpOKLCH;
        t["lerpOKLab"] = &trussc::Color::lerpOKLab;
        t["lerpRGB"] = &trussc::Color::lerpRGB;
        t["set"] = sol::overload([](trussc::Color& self, float r_, float g_, float b_) -> decltype(auto) { return self.set(r_, g_, b_); }, [](trussc::Color& self, float r_, float g_, float b_, float a_) -> decltype(auto) { return self.set(r_, g_, b_, a_); }, [](trussc::Color& self, float gray) -> decltype(auto) { return self.set(gray); }, [](trussc::Color& self, float gray, float a_) -> decltype(auto) { return self.set(gray, a_); }, [](trussc::Color& self, const trussc::Color & c) -> decltype(auto) { return self.set(c); });
        t["toHSB"] = &trussc::Color::toHSB;
        t["toHex"] = sol::overload([](trussc::Color& self) { return self.toHex(); }, [](trussc::Color& self, bool includeAlpha) { return self.toHex(includeAlpha); });
        t["toLinear"] = &trussc::Color::toLinear;
        t["toOKLCH"] = &trussc::Color::toOKLCH;
        t["toOKLab"] = &trussc::Color::toOKLab;
        t["fromBytes"] = sol::overload([](int r, int g, int b) { return trussc::Color::fromBytes(r, g, b); }, [](int r, int g, int b, int a) { return trussc::Color::fromBytes(r, g, b, a); });
        t["fromHSB"] = sol::overload([](float h, float s, float b) { return trussc::Color::fromHSB(h, s, b); }, [](float h, float s, float b, float a) { return trussc::Color::fromHSB(h, s, b, a); });
        t["fromHex"] = sol::overload([](uint32_t hex) { return trussc::Color::fromHex(hex); }, [](uint32_t hex, bool hasAlpha) { return trussc::Color::fromHex(hex, hasAlpha); });
        t["fromLinear"] = sol::overload([](float r, float g, float b) { return trussc::Color::fromLinear(r, g, b); }, [](float r, float g, float b, float a) { return trussc::Color::fromLinear(r, g, b, a); });
        t["fromOKLCH"] = sol::overload([](float L, float C, float H) { return trussc::Color::fromOKLCH(L, C, H); }, [](float L, float C, float H, float a) { return trussc::Color::fromOKLCH(L, C, H, a); });
        t["fromOKLab"] = sol::overload([](float L, float a_lab, float b_lab) { return trussc::Color::fromOKLab(L, a_lab, b_lab); }, [](float L, float a_lab, float b_lab, float alpha) { return trussc::Color::fromOKLab(L, a_lab, b_lab, alpha); });
    }
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
        auto constructors = tcxLuaConstructors<trussc::ColorHSB
            , trussc::ColorHSB()
            , trussc::ColorHSB(float, float, float)
            , trussc::ColorHSB(float, float, float, float)
        >();
        sol::usertype<trussc::ColorHSB> t = lua->new_usertype<trussc::ColorHSB>("ColorHSB", constructors, sol::call_constructor, constructors);
        t["a"] = &trussc::ColorHSB::a;
        t["b"] = &trussc::ColorHSB::b;
        t["h"] = &trussc::ColorHSB::h;
        t["s"] = &trussc::ColorHSB::s;
        t["lerp"] = sol::overload([](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t) { return self.lerp(target, t); }, [](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
        t["toLinear"] = &trussc::ColorHSB::toLinear;
        t["toOKLCH"] = &trussc::ColorHSB::toOKLCH;
        t["toOKLab"] = &trussc::ColorHSB::toOKLab;
        t["toRGB"] = &trussc::ColorHSB::toRGB;
    }
    lua->new_usertype<trussc::Beep>("Beep",
        sol::meta_function::equal_to, [](trussc::Beep a, trussc::Beep b){ return a == b; },
        "ping", sol::var(trussc::Beep::ping),
        "success", sol::var(trussc::Beep::success),
        "complete", sol::var(trussc::Beep::complete),
        "coin", sol::var(trussc::Beep::coin),
        "error", sol::var(trussc::Beep::error),
        "warning", sol::var(trussc::Beep::warning),
        "cancel", sol::var(trussc::Beep::cancel),
        "click", sol::var(trussc::Beep::click),
        "typing", sol::var(trussc::Beep::typing),
        "notify", sol::var(trussc::Beep::notify),
        "sweep", sol::var(trussc::Beep::sweep));
    {
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    lua->new_usertype<trussc::StrokeMesh::CapType>("StrokeMeshCapType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::CapType a, trussc::StrokeMesh::CapType b){ return a == b; },
        "CAP_BUTT", sol::var(trussc::StrokeMesh::CapType::CAP_BUTT),
        "CAP_ROUND", sol::var(trussc::StrokeMesh::CapType::CAP_ROUND),
        "CAP_SQUARE", sol::var(trussc::StrokeMesh::CapType::CAP_SQUARE));
    lua->new_usertype<trussc::TextureWrap>("TextureWrap",
        sol::meta_function::equal_to, [](trussc::TextureWrap a, trussc::TextureWrap b){ return a == b; },
        "Repeat", sol::var(trussc::TextureWrap::Repeat),
        "ClampToEdge", sol::var(trussc::TextureWrap::ClampToEdge),
        "MirroredRepeat", sol::var(trussc::TextureWrap::MirroredRepeat));
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["accuracy"] = &trussc::Location::accuracy;
        t["altitude"] = &trussc::Location::altitude;
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
