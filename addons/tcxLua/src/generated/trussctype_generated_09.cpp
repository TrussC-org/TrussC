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
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::localMatrixChanged);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseDragged);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseScrolled);
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
        auto constructors = tcxLuaConstructors<trussc::Rect
            , trussc::Rect()
            , trussc::Rect(float, float, float, float)
            , trussc::Rect(const trussc::Vec2 &, float, float)
            , trussc::Rect(const trussc::Vec3 &, float, float)
            , trussc::Rect(float, float, float, float, float)
        >();
        sol::usertype<trussc::Rect> t = lua->new_usertype<trussc::Rect>("Rect", constructors, sol::call_constructor, constructors);
        t["height"] = &trussc::Rect::height;
        t["width"] = &trussc::Rect::width;
        t["x"] = &trussc::Rect::x;
        t["y"] = &trussc::Rect::y;
        t["contains"] = &trussc::Rect::contains;
        t["getBottom"] = &trussc::Rect::getBottom;
        t["getCenter"] = &trussc::Rect::getCenter;
        t["getCenterX"] = &trussc::Rect::getCenterX;
        t["getCenterY"] = &trussc::Rect::getCenterY;
        t["getRight"] = &trussc::Rect::getRight;
        t["intersects"] = &trussc::Rect::intersects;
        t["set"] = sol::overload([](trussc::Rect& self, float x_, float y_, float w_, float h_) -> decltype(auto) { return self.set(x_, y_, w_, h_); }, [](trussc::Rect& self, const trussc::Vec2 & pos, float w_, float h_) -> decltype(auto) { return self.set(pos, w_, h_); });
    }
    {
        auto constructors = tcxLuaConstructors<trussc::JsonReadReflector
            , trussc::JsonReadReflector(trussc::Json)
        >();
        sol::usertype<trussc::JsonReadReflector> t = lua->new_usertype<trussc::JsonReadReflector>("JsonReadReflector", constructors, sol::call_constructor, constructors);
        t["applied"] = &trussc::JsonReadReflector::applied;
        t["readOnly"] = &trussc::JsonReadReflector::readOnly;
        t["skipped"] = &trussc::JsonReadReflector::skipped;
        t["endGroup"] = &trussc::JsonReadReflector::endGroup;
        t["isDerived"] = &trussc::JsonReadReflector::isDerived;
        t["isReadOnly"] = &trussc::JsonReadReflector::isReadOnly;
        t["popDerived"] = &trussc::JsonReadReflector::popDerived;
        t["popReadOnly"] = &trussc::JsonReadReflector::popReadOnly;
        t["pushDerived"] = &trussc::JsonReadReflector::pushDerived;
        t["pushReadOnly"] = &trussc::JsonReadReflector::pushReadOnly;
        t["unknownKeys"] = &trussc::JsonReadReflector::unknownKeys;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::FileReader
            , trussc::FileReader()
        >();
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader", constructors, sol::call_constructor, constructors);
        t["close"] = &trussc::FileReader::close;
        t["eof"] = &trussc::FileReader::eof;
        t["isOpen"] = &trussc::FileReader::isOpen;
        t["open"] = &trussc::FileReader::open;
        t["readChar"] = &trussc::FileReader::readChar;
        t["readLine"] = [](trussc::FileReader& self) { return self.readLine(); };
        t["remaining"] = &trussc::FileReader::remaining;
        t["seek"] = &trussc::FileReader::seek;
        t["tell"] = &trussc::FileReader::tell;
    }
    {
        sol::usertype<trussc::AudioDeviceChangedArgs> t = lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
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
    lua->new_usertype<trussc::ThermalState>("ThermalState",
        sol::meta_function::equal_to, [](trussc::ThermalState a, trussc::ThermalState b){ return a == b; },
        "Nominal", sol::var(trussc::ThermalState::Nominal),
        "Fair", sol::var(trussc::ThermalState::Fair),
        "Serious", sol::var(trussc::ThermalState::Serious),
        "Critical", sol::var(trussc::ThermalState::Critical));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    lua->new_usertype<trussc::SoundStream::Kind>("SoundStreamKind",
        sol::meta_function::equal_to, [](trussc::SoundStream::Kind a, trussc::SoundStream::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundStream::Kind::Eager),
        "Stream", sol::var(trussc::SoundStream::Kind::Stream));
    {
        sol::usertype<trussc::ChipSoundBundle::Entry> t = lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
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
