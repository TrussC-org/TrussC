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
        t["localMatrixChanged"] = &trussc::ScrollBar::localMatrixChanged;
        t["mouseDragged"] = &trussc::ScrollBar::mouseDragged;
        t["mousePressed"] = &trussc::ScrollBar::mousePressed;
        t["mouseReleased"] = &trussc::ScrollBar::mouseReleased;
        t["mouseScrolled"] = &trussc::ScrollBar::mouseScrolled;
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
        auto constructors = tcxLuaConstructors<trussc::Vec3
            , trussc::Vec3()
            , trussc::Vec3(float, float, float)
            , trussc::Vec3(float)
            , trussc::Vec3(const trussc::Vec2 &)
            , trussc::Vec3(const trussc::Vec2 &, float)
        >();
        sol::usertype<trussc::Vec3> t = lua->new_usertype<trussc::Vec3>("Vec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec3& a, float b){ return a * b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec3& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec3& a, float b){ return a / b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec3& a, int b){ return a[b]; };
        t["x"] = &trussc::Vec3::x;
        t["y"] = &trussc::Vec3::y;
        t["z"] = &trussc::Vec3::z;
        t["cross"] = &trussc::Vec3::cross;
        t["distance"] = &trussc::Vec3::distance;
        t["distanceSquared"] = &trussc::Vec3::distanceSquared;
        t["dot"] = &trussc::Vec3::dot;
        t["length"] = &trussc::Vec3::length;
        t["lengthSquared"] = &trussc::Vec3::lengthSquared;
        t["lerp"] = &trussc::Vec3::lerp;
        t["limit"] = &trussc::Vec3::limit;
        t["normalize"] = &trussc::Vec3::normalize;
        t["normalized"] = &trussc::Vec3::normalized;
        t["reflected"] = &trussc::Vec3::reflected;
        t["set"] = sol::overload([](trussc::Vec3& self, float x_, float y_, float z_) -> decltype(auto) { return self.set(x_, y_, z_); }, [](trussc::Vec3& self, const trussc::Vec3 & v) -> decltype(auto) { return self.set(v); });
        t["xy"] = &trussc::Vec3::xy;
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
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["derived"] = &trussc::JsonWriteReflector::derived;
        t["includeDerived"] = &trussc::JsonWriteReflector::includeDerived;
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
        t["isDerived"] = &trussc::JsonWriteReflector::isDerived;
        t["isReadOnly"] = &trussc::JsonWriteReflector::isReadOnly;
        t["popDerived"] = &trussc::JsonWriteReflector::popDerived;
        t["popReadOnly"] = &trussc::JsonWriteReflector::popReadOnly;
        t["pushDerived"] = &trussc::JsonWriteReflector::pushDerived;
        t["pushReadOnly"] = &trussc::JsonWriteReflector::pushReadOnly;
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
    {
        auto constructors = tcxLuaConstructors<trussc::LogEventArgs
            , trussc::LogEventArgs(trussc::LogLevel, const std::string &)
        >();
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs", constructors, sol::call_constructor, constructors);
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::OnceGate
            , trussc::OnceGate()
            , trussc::OnceGate(double)
        >();
        sol::usertype<trussc::OnceGate> t = lua->new_usertype<trussc::OnceGate>("OnceGate", constructors, sol::call_constructor, constructors);
        t["isFirstTime"] = &trussc::OnceGate::isFirstTime;
        t["reset"] = &trussc::OnceGate::reset;
    }
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
    }
    lua->new_usertype<trussc::LayoutDirection>("LayoutDirection",
        sol::meta_function::equal_to, [](trussc::LayoutDirection a, trussc::LayoutDirection b){ return a == b; },
        "Vertical", sol::var(trussc::LayoutDirection::Vertical),
        "Horizontal", sol::var(trussc::LayoutDirection::Horizontal));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::ConsoleEventArgs> t = lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        t["args"] = &trussc::ConsoleEventArgs::args;
        t["raw"] = &trussc::ConsoleEventArgs::raw;
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
