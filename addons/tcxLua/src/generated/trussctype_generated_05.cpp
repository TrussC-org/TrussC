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
        t["localMatrixChanged"] = &trussc::RectNodeButton::localMatrixChanged;
        t["mouseDragged"] = &trussc::RectNodeButton::mouseDragged;
        t["mousePressed"] = &trussc::RectNodeButton::mousePressed;
        t["mouseReleased"] = &trussc::RectNodeButton::mouseReleased;
        t["mouseScrolled"] = &trussc::RectNodeButton::mouseScrolled;
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
        auto constructors = tcxLuaConstructors<trussc::StrokeMesh
            , trussc::StrokeMesh()
            , trussc::StrokeMesh(const trussc::Path &)
        >();
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh", constructors, sol::call_constructor, constructors);
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["clear"] = &trussc::StrokeMesh::clear;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["update"] = &trussc::StrokeMesh::update;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ChipSoundNote
            , trussc::ChipSoundNote()
            , trussc::ChipSoundNote(trussc::Wave, float, float)
            , trussc::ChipSoundNote(trussc::Wave, float, float, float)
        >();
        sol::usertype<trussc::ChipSoundNote> t = lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote", constructors, sol::call_constructor, constructors);
        t["attack"] = &trussc::ChipSoundNote::attack;
        t["decay"] = &trussc::ChipSoundNote::decay;
        t["duration"] = &trussc::ChipSoundNote::duration;
        t["hz"] = &trussc::ChipSoundNote::hz;
        t["release"] = &trussc::ChipSoundNote::release;
        t["sustain"] = &trussc::ChipSoundNote::sustain;
        t["volume"] = &trussc::ChipSoundNote::volume;
        t["wave"] = &trussc::ChipSoundNote::wave;
        t["build"] = &trussc::ChipSoundNote::build;
        t["generateBuffer"] = &trussc::ChipSoundNote::generateBuffer;
        t["getTotalDuration"] = &trussc::ChipSoundNote::getTotalDuration;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::IesProfile
            , trussc::IesProfile()
        >();
        sol::usertype<trussc::IesProfile> t = lua->new_usertype<trussc::IesProfile>("IesProfile", constructors, sol::call_constructor, constructors);
        t["getMaxCandela"] = &trussc::IesProfile::getMaxCandela;
        t["getMaxVerticalAngle"] = &trussc::IesProfile::getMaxVerticalAngle;
        t["getSampler"] = &trussc::IesProfile::getSampler;
        t["getTextureWidth"] = &trussc::IesProfile::getTextureWidth;
        t["getView"] = &trussc::IesProfile::getView;
        t["isLoaded"] = &trussc::IesProfile::isLoaded;
        t["load"] = &trussc::IesProfile::load;
        t["loadFromString"] = &trussc::IesProfile::loadFromString;
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
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
    }
    {
        sol::usertype<trussc::Runtime> t = lua->new_usertype<trussc::Runtime>("Runtime");
        t["events"] = &trussc::Runtime::events;
        t["setLogoutTimeout"] = &trussc::Runtime::setLogoutTimeout;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
