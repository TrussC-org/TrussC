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
void tcxLuaGenShard_07(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::ScrollBar> t = lua->new_usertype<trussc::ScrollBar>("ScrollBar");
        t["mousePressed"] = &trussc::ScrollBar::mousePressed;
        t["mouseReleased"] = &trussc::ScrollBar::mouseReleased;
        t["mouseDragged"] = &trussc::ScrollBar::mouseDragged;
        t["mouseScrolled"] = &trussc::ScrollBar::mouseScrolled;
        t["localMatrixChanged"] = &trussc::ScrollBar::localMatrixChanged;
        t["getBarColor"] = &trussc::ScrollBar::getBarColor;
        t["setBarColor"] = &trussc::ScrollBar::setBarColor;
        t["getBarWidth"] = &trussc::ScrollBar::getBarWidth;
        t["setBarWidth"] = &trussc::ScrollBar::setBarWidth;
        t["getMargin"] = &trussc::ScrollBar::getMargin;
        t["setMargin"] = &trussc::ScrollBar::setMargin;
        t["getOffset"] = &trussc::ScrollBar::getOffset;
        t["updateFromContainer"] = &trussc::ScrollBar::updateFromContainer;
        t["getWidth"] = &trussc::ScrollBar::getWidth;
        t["getHeight"] = &trussc::ScrollBar::getHeight;
        t["getSize"] = &trussc::ScrollBar::getSize;
        t["setWidth"] = &trussc::ScrollBar::setWidth;
        t["setHeight"] = &trussc::ScrollBar::setHeight;
        t["setSize"] = sol::overload([](trussc::ScrollBar& self, float w, float h) { return self.setSize(w, h); }, [](trussc::ScrollBar& self, float size) { return self.setSize(size); }, [](trussc::ScrollBar& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::ScrollBar::setRect;
        t["setClipping"] = &trussc::ScrollBar::setClipping;
        t["isClipping"] = &trussc::ScrollBar::isClipping;
        t["getLeft"] = &trussc::ScrollBar::getLeft;
        t["getRight"] = &trussc::ScrollBar::getRight;
        t["getTop"] = &trussc::ScrollBar::getTop;
        t["getBottom"] = &trussc::ScrollBar::getBottom;
        t["hitTest"] = [](trussc::ScrollBar& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["setup"] = &trussc::ScrollBar::setup;
        t["update"] = &trussc::ScrollBar::update;
        t["cleanup"] = &trussc::ScrollBar::cleanup;
        t["addChild"] = sol::overload([](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child) { return self.addChild(child); }, [](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child) { return self.insertChild(index, child); }, [](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::ScrollBar::removeChild;
        t["removeAllChildren"] = &trussc::ScrollBar::removeAllChildren;
        t["onChildAdded"] = &trussc::ScrollBar::onChildAdded;
        t["onChildRemoved"] = &trussc::ScrollBar::onChildRemoved;
        t["getParent"] = &trussc::ScrollBar::getParent;
        t["getChildren"] = &trussc::ScrollBar::getChildren;
        t["getChildCount"] = &trussc::ScrollBar::getChildCount;
        t["getChildIndex"] = &trussc::ScrollBar::getChildIndex;
        t["moveToFront"] = &trussc::ScrollBar::moveToFront;
        t["moveToBack"] = &trussc::ScrollBar::moveToBack;
        t["isActive"] = &trussc::ScrollBar::isActive;
        t["setActive"] = &trussc::ScrollBar::setActive;
        t["isVisible"] = &trussc::ScrollBar::isVisible;
        t["setVisible"] = &trussc::ScrollBar::setVisible;
        t["getActive"] = &trussc::ScrollBar::getActive;
        t["getVisible"] = &trussc::ScrollBar::getVisible;
        t["setIsActive"] = &trussc::ScrollBar::setIsActive;
        t["setIsVisible"] = &trussc::ScrollBar::setIsVisible;
        t["destroy"] = &trussc::ScrollBar::destroy;
        t["isDead"] = &trussc::ScrollBar::isDead;
        t["enableEvents"] = &trussc::ScrollBar::enableEvents;
        t["disableEvents"] = &trussc::ScrollBar::disableEvents;
        t["isEventsEnabled"] = &trussc::ScrollBar::isEventsEnabled;
        t["isMouseOver"] = &trussc::ScrollBar::isMouseOver;
        t["setName"] = &trussc::ScrollBar::setName;
        t["getName"] = &trussc::ScrollBar::getName;
        t["hasName"] = &trussc::ScrollBar::hasName;
        t["getTypeName"] = &trussc::ScrollBar::getTypeName;
        t["getDisplayName"] = &trussc::ScrollBar::getDisplayName;
        t["getInstanceId"] = &trussc::ScrollBar::getInstanceId;
        t["findByInstanceId"] = &trussc::ScrollBar::findByInstanceId;
        t["getPos"] = &trussc::ScrollBar::getPos;
        t["getX"] = &trussc::ScrollBar::getX;
        t["getY"] = &trussc::ScrollBar::getY;
        t["getZ"] = &trussc::ScrollBar::getZ;
        t["setPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::ScrollBar& self, float x, float y) { return self.setPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::ScrollBar::setX;
        t["setY"] = &trussc::ScrollBar::setY;
        t["setZ"] = &trussc::ScrollBar::setZ;
        t["getQuaternion"] = &trussc::ScrollBar::getQuaternion;
        t["setQuaternion"] = &trussc::ScrollBar::setQuaternion;
        t["getEuler"] = &trussc::ScrollBar::getEuler;
        t["setEuler"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::ScrollBar& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::ScrollBar::getEulerDeg;
        t["setEulerDeg"] = &trussc::ScrollBar::setEulerDeg;
        t["getRot"] = &trussc::ScrollBar::getRot;
        t["setRot"] = &trussc::ScrollBar::setRot;
        t["getRotDeg"] = &trussc::ScrollBar::getRotDeg;
        t["setRotDeg"] = &trussc::ScrollBar::setRotDeg;
        t["getScale"] = &trussc::ScrollBar::getScale;
        t["getScaleX"] = &trussc::ScrollBar::getScaleX;
        t["getScaleY"] = &trussc::ScrollBar::getScaleY;
        t["getScaleZ"] = &trussc::ScrollBar::getScaleZ;
        t["setScale"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::ScrollBar& self, float uniform) { return self.setScale(uniform); }, [](trussc::ScrollBar& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::ScrollBar& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::ScrollBar::setScaleX;
        t["setScaleY"] = &trussc::ScrollBar::setScaleY;
        t["setScaleZ"] = &trussc::ScrollBar::setScaleZ;
        t["getLocalMatrix"] = &trussc::ScrollBar::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::ScrollBar::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::ScrollBar::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::ScrollBar::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::ScrollBar& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::ScrollBar::getMouseX;
        t["getMouseY"] = &trussc::ScrollBar::getMouseY;
        t["getPMouseX"] = &trussc::ScrollBar::getPMouseX;
        t["getPMouseY"] = &trussc::ScrollBar::getPMouseY;
        t["findHitNode"] = &trussc::ScrollBar::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::ScrollBar::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::ScrollBar::getCameraContext;
        t["setCameraContext"] = &trussc::ScrollBar::setCameraContext;
        t["getModTypeNames"] = &trussc::ScrollBar::getModTypeNames;
        t["getMods"] = &trussc::ScrollBar::getMods;
        t["getModByTypeName"] = &trussc::ScrollBar::getModByTypeName;
        t["callAfter"] = &trussc::ScrollBar::callAfter;
        t["callEvery"] = &trussc::ScrollBar::callEvery;
        t["callEveryCatchUp"] = &trussc::ScrollBar::callEveryCatchUp;
        t["cancelTimer"] = &trussc::ScrollBar::cancelTimer;
        t["cancelAllTimers"] = &trussc::ScrollBar::cancelAllTimers;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Vec3
            , trussc::Vec3()
            , trussc::Vec3(float, float, float)
            , trussc::Vec3(float)
            , trussc::Vec3(const trussc::Vec2 &)
            , trussc::Vec3(const trussc::Vec2 &, float)
        >();
        sol::usertype<trussc::Vec3> t = lua->new_usertype<trussc::Vec3>("Vec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::index] = [](const trussc::Vec3& a, int b){ return a[b]; };
        t[sol::meta_function::addition] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec3& a){ return -a; };
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec3& a, float b){ return a * b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a * b; });
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec3& a, float b){ return a / b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a == b; };
        t["x"] = &trussc::Vec3::x;
        t["y"] = &trussc::Vec3::y;
        t["z"] = &trussc::Vec3::z;
        t["set"] = sol::overload([](trussc::Vec3& self, float x_, float y_, float z_) -> decltype(auto) { return self.set(x_, y_, z_); }, [](trussc::Vec3& self, const trussc::Vec3 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec3::length;
        t["lengthSquared"] = &trussc::Vec3::lengthSquared;
        t["normalized"] = &trussc::Vec3::normalized;
        t["normalize"] = &trussc::Vec3::normalize;
        t["limit"] = &trussc::Vec3::limit;
        t["dot"] = &trussc::Vec3::dot;
        t["cross"] = &trussc::Vec3::cross;
        t["distance"] = &trussc::Vec3::distance;
        t["distanceSquared"] = &trussc::Vec3::distanceSquared;
        t["lerp"] = &trussc::Vec3::lerp;
        t["reflected"] = &trussc::Vec3::reflected;
        t["xy"] = &trussc::Vec3::xy;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLCH
            , trussc::ColorOKLCH()
            , trussc::ColorOKLCH(float, float, float)
            , trussc::ColorOKLCH(float, float, float, float)
        >();
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH", constructors, sol::call_constructor, constructors);
        t["L"] = &trussc::ColorOKLCH::L;
        t["C"] = &trussc::ColorOKLCH::C;
        t["H"] = &trussc::ColorOKLCH::H;
        t["alpha"] = &trussc::ColorOKLCH::alpha;
        t["toOKLab"] = &trussc::ColorOKLCH::toOKLab;
        t["toLinear"] = &trussc::ColorOKLCH::toLinear;
        t["toRGB"] = &trussc::ColorOKLCH::toRGB;
        t["toHSB"] = &trussc::ColorOKLCH::toHSB;
        t["lerp"] = sol::overload([](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t) { return self.lerp(target, t); }, [](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    lua->new_usertype<trussc::TextureFormat>("TextureFormat",
        sol::meta_function::equal_to, [](trussc::TextureFormat a, trussc::TextureFormat b){ return a == b; },
        "RGBA8", sol::var(trussc::TextureFormat::RGBA8),
        "RGBA16F", sol::var(trussc::TextureFormat::RGBA16F),
        "RGBA32F", sol::var(trussc::TextureFormat::RGBA32F),
        "R8", sol::var(trussc::TextureFormat::R8),
        "R16F", sol::var(trussc::TextureFormat::R16F),
        "R32F", sol::var(trussc::TextureFormat::R32F),
        "RG8", sol::var(trussc::TextureFormat::RG8),
        "RG16F", sol::var(trussc::TextureFormat::RG16F),
        "RG32F", sol::var(trussc::TextureFormat::RG32F),
        "BGRA8", sol::var(trussc::TextureFormat::BGRA8),
        "RGBA16", sol::var(trussc::TextureFormat::RGBA16));
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
    {
        sol::usertype<trussc::SaveResult> t = lua->new_usertype<trussc::SaveResult>("SaveResult");
        t["error"] = &trussc::SaveResult::error;
        t["message"] = &trussc::SaveResult::message;
        t["ok"] = &trussc::SaveResult::ok;
        t["success"] = &trussc::SaveResult::success;
        t["fail"] = sol::overload([](trussc::SaveError error) { return trussc::SaveResult::fail(error); }, [](trussc::SaveError error, std::string message) { return trussc::SaveResult::fail(error, message); });
    }
    {
        sol::usertype<trussc::TcpSendCompleteEventArgs> t = lua->new_usertype<trussc::TcpSendCompleteEventArgs>("TcpSendCompleteEventArgs");
        t["clientId"] = &trussc::TcpSendCompleteEventArgs::clientId;
        t["sendId"] = &trussc::TcpSendCompleteEventArgs::sendId;
        t["error"] = &trussc::TcpSendCompleteEventArgs::error;
        t["bytesSent"] = &trussc::TcpSendCompleteEventArgs::bytesSent;
    }
    {
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
        t["id"] = &trussc::TouchEventArgs::id;
    }
    lua->new_usertype<trussc::SoundSource::Kind>("SoundSourceKind",
        sol::meta_function::equal_to, [](trussc::SoundSource::Kind a, trussc::SoundSource::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundSource::Kind::Eager),
        "Stream", sol::var(trussc::SoundSource::Kind::Stream));
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
    {
        sol::usertype<trussc::TcpReceiveEventArgs> t = lua->new_usertype<trussc::TcpReceiveEventArgs>("TcpReceiveEventArgs");
        t["data"] = &trussc::TcpReceiveEventArgs::data;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
