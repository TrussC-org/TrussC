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
void tcxLuaGenShard_03(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::ScrollContainer
            , trussc::ScrollContainer()
        >();
        sol::usertype<trussc::ScrollContainer> t = lua->new_usertype<trussc::ScrollContainer>("ScrollContainer", constructors, sol::call_constructor, constructors);
        t["localMatrixChanged"] = &trussc::ScrollContainer::localMatrixChanged;
        t["mouseDragged"] = &trussc::ScrollContainer::mouseDragged;
        t["mousePressed"] = &trussc::ScrollContainer::mousePressed;
        t["mouseReleased"] = &trussc::ScrollContainer::mouseReleased;
        t["mouseScrolled"] = &trussc::ScrollContainer::mouseScrolled;
        t["addChild"] = sol::overload([](trussc::ScrollContainer& self, trussc::ScrollContainer::Ptr child) { return self.addChild(child); }, [](trussc::ScrollContainer& self, trussc::ScrollContainer::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::ScrollContainer::callAfter;
        t["callEvery"] = &trussc::ScrollContainer::callEvery;
        t["callEveryCatchUp"] = &trussc::ScrollContainer::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::ScrollContainer::cancelAllTimers;
        t["cancelTimer"] = &trussc::ScrollContainer::cancelTimer;
        t["cleanup"] = &trussc::ScrollContainer::cleanup;
        t["destroy"] = &trussc::ScrollContainer::destroy;
        t["disableEvents"] = &trussc::ScrollContainer::disableEvents;
        t["draw"] = &trussc::ScrollContainer::draw;
        t["enableEvents"] = &trussc::ScrollContainer::enableEvents;
        t["findByInstanceId"] = &trussc::ScrollContainer::findByInstanceId;
        t["findHitNode"] = &trussc::ScrollContainer::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::ScrollContainer::findHitNodeFromScreen;
        t["getActive"] = &trussc::ScrollContainer::getActive;
        t["getBottom"] = &trussc::ScrollContainer::getBottom;
        t["getCameraContext"] = &trussc::ScrollContainer::getCameraContext;
        t["getChildCount"] = &trussc::ScrollContainer::getChildCount;
        t["getChildIndex"] = &trussc::ScrollContainer::getChildIndex;
        t["getChildren"] = &trussc::ScrollContainer::getChildren;
        t["getContent"] = &trussc::ScrollContainer::getContent;
        t["getContentRect"] = &trussc::ScrollContainer::getContentRect;
        t["getDisplayName"] = &trussc::ScrollContainer::getDisplayName;
        t["getEuler"] = &trussc::ScrollContainer::getEuler;
        t["getEulerDeg"] = &trussc::ScrollContainer::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::ScrollContainer::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::ScrollContainer::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::ScrollContainer::getGlobalPos;
        t["getHeight"] = &trussc::ScrollContainer::getHeight;
        t["getInstanceId"] = &trussc::ScrollContainer::getInstanceId;
        t["getLeft"] = &trussc::ScrollContainer::getLeft;
        t["getLocalMatrix"] = &trussc::ScrollContainer::getLocalMatrix;
        t["getMaxScrollX"] = &trussc::ScrollContainer::getMaxScrollX;
        t["getMaxScrollY"] = &trussc::ScrollContainer::getMaxScrollY;
        t["getModByTypeName"] = &trussc::ScrollContainer::getModByTypeName;
        t["getModTypeNames"] = &trussc::ScrollContainer::getModTypeNames;
        t["getMods"] = &trussc::ScrollContainer::getMods;
        t["getMouseX"] = &trussc::ScrollContainer::getMouseX;
        t["getMouseY"] = &trussc::ScrollContainer::getMouseY;
        t["getName"] = &trussc::ScrollContainer::getName;
        t["getPMouseX"] = &trussc::ScrollContainer::getPMouseX;
        t["getPMouseY"] = &trussc::ScrollContainer::getPMouseY;
        t["getParent"] = &trussc::ScrollContainer::getParent;
        t["getPos"] = &trussc::ScrollContainer::getPos;
        t["getQuaternion"] = &trussc::ScrollContainer::getQuaternion;
        t["getRight"] = &trussc::ScrollContainer::getRight;
        t["getRot"] = &trussc::ScrollContainer::getRot;
        t["getRotDeg"] = &trussc::ScrollContainer::getRotDeg;
        t["getScale"] = &trussc::ScrollContainer::getScale;
        t["getScaleX"] = &trussc::ScrollContainer::getScaleX;
        t["getScaleY"] = &trussc::ScrollContainer::getScaleY;
        t["getScaleZ"] = &trussc::ScrollContainer::getScaleZ;
        t["getScroll"] = &trussc::ScrollContainer::getScroll;
        t["getScrollSpeed"] = &trussc::ScrollContainer::getScrollSpeed;
        t["getScrollX"] = &trussc::ScrollContainer::getScrollX;
        t["getScrollY"] = &trussc::ScrollContainer::getScrollY;
        t["getSize"] = &trussc::ScrollContainer::getSize;
        t["getTop"] = &trussc::ScrollContainer::getTop;
        t["getTypeName"] = &trussc::ScrollContainer::getTypeName;
        t["getVisible"] = &trussc::ScrollContainer::getVisible;
        t["getWidth"] = &trussc::ScrollContainer::getWidth;
        t["getX"] = &trussc::ScrollContainer::getX;
        t["getY"] = &trussc::ScrollContainer::getY;
        t["getZ"] = &trussc::ScrollContainer::getZ;
        t["globalToLocal"] = [](trussc::ScrollContainer& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::ScrollContainer::hasName;
        t["hitTest"] = [](trussc::ScrollContainer& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::ScrollContainer& self, size_t index, trussc::ScrollContainer::Ptr child) { return self.insertChild(index, child); }, [](trussc::ScrollContainer& self, size_t index, trussc::ScrollContainer::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::ScrollContainer::isActive;
        t["isClipping"] = &trussc::ScrollContainer::isClipping;
        t["isDead"] = &trussc::ScrollContainer::isDead;
        t["isEventsEnabled"] = &trussc::ScrollContainer::isEventsEnabled;
        t["isHorizontalScrollEnabled"] = &trussc::ScrollContainer::isHorizontalScrollEnabled;
        t["isMouseOver"] = &trussc::ScrollContainer::isMouseOver;
        t["isVerticalScrollEnabled"] = &trussc::ScrollContainer::isVerticalScrollEnabled;
        t["isVisible"] = &trussc::ScrollContainer::isVisible;
        t["localToGlobal"] = [](trussc::ScrollContainer& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::ScrollContainer::moveToBack;
        t["moveToFront"] = &trussc::ScrollContainer::moveToFront;
        t["onChildAdded"] = &trussc::ScrollContainer::onChildAdded;
        t["onChildRemoved"] = &trussc::ScrollContainer::onChildRemoved;
        t["removeAllChildren"] = &trussc::ScrollContainer::removeAllChildren;
        t["removeChild"] = &trussc::ScrollContainer::removeChild;
        t["setActive"] = &trussc::ScrollContainer::setActive;
        t["setCameraContext"] = &trussc::ScrollContainer::setCameraContext;
        t["setClipping"] = &trussc::ScrollContainer::setClipping;
        t["setContent"] = &trussc::ScrollContainer::setContent;
        t["setEuler"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::ScrollContainer& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::ScrollContainer::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::ScrollContainer& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::ScrollContainer& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::ScrollContainer::setHeight;
        t["setHorizontalScrollEnabled"] = &trussc::ScrollContainer::setHorizontalScrollEnabled;
        t["setIsActive"] = &trussc::ScrollContainer::setIsActive;
        t["setIsVisible"] = &trussc::ScrollContainer::setIsVisible;
        t["setName"] = &trussc::ScrollContainer::setName;
        t["setPos"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::ScrollContainer& self, float x, float y) { return self.setPos(x, y); }, [](trussc::ScrollContainer& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::ScrollContainer::setQuaternion;
        t["setRect"] = &trussc::ScrollContainer::setRect;
        t["setRot"] = &trussc::ScrollContainer::setRot;
        t["setRotDeg"] = &trussc::ScrollContainer::setRotDeg;
        t["setScale"] = sol::overload([](trussc::ScrollContainer& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::ScrollContainer& self, float uniform) { return self.setScale(uniform); }, [](trussc::ScrollContainer& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::ScrollContainer& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::ScrollContainer::setScaleX;
        t["setScaleY"] = &trussc::ScrollContainer::setScaleY;
        t["setScaleZ"] = &trussc::ScrollContainer::setScaleZ;
        t["setScroll"] = sol::overload([](trussc::ScrollContainer& self, float x, float y) { return self.setScroll(x, y); }, [](trussc::ScrollContainer& self, trussc::Vec2 pos) { return self.setScroll(pos); });
        t["setScrollSpeed"] = &trussc::ScrollContainer::setScrollSpeed;
        t["setScrollX"] = &trussc::ScrollContainer::setScrollX;
        t["setScrollY"] = &trussc::ScrollContainer::setScrollY;
        t["setSize"] = sol::overload([](trussc::ScrollContainer& self, float w, float h) { return self.setSize(w, h); }, [](trussc::ScrollContainer& self, float size) { return self.setSize(size); }, [](trussc::ScrollContainer& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVerticalScrollEnabled"] = &trussc::ScrollContainer::setVerticalScrollEnabled;
        t["setVisible"] = &trussc::ScrollContainer::setVisible;
        t["setWidth"] = &trussc::ScrollContainer::setWidth;
        t["setX"] = &trussc::ScrollContainer::setX;
        t["setY"] = &trussc::ScrollContainer::setY;
        t["setZ"] = &trussc::ScrollContainer::setZ;
        t["setup"] = &trussc::ScrollContainer::setup;
        t["updateScrollBounds"] = &trussc::ScrollContainer::updateScrollBounds;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Ray
            , trussc::Ray()
            , trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)
        >();
        sol::usertype<trussc::Ray> t = lua->new_usertype<trussc::Ray>("Ray", constructors, sol::call_constructor, constructors);
        t["direction"] = &trussc::Ray::direction;
        t["origin"] = &trussc::Ray::origin;
        t["at"] = &trussc::Ray::at;
        t["intersectAABB"] = [](trussc::Ray& self, const trussc::Vec3 & boxMin, const trussc::Vec3 & boxMax) { return self.intersectAABB(boxMin, boxMax); };
        t["intersectPlane"] = [](trussc::Ray& self, const trussc::Vec3 & planeNormal, float planeD) { return self.intersectPlane(planeNormal, planeD); };
        t["intersectSphere"] = [](trussc::Ray& self, float radius) { return self.intersectSphere(radius); };
        t["intersectZPlane"] = [](trussc::Ray& self) { return self.intersectZPlane(); };
        t["transformed"] = &trussc::Ray::transformed;
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
    }
    {
        auto constructors = tcxLuaConstructors<trussc::AudioRecorder
            , trussc::AudioRecorder()
        >();
        sol::usertype<trussc::AudioRecorder> t = lua->new_usertype<trussc::AudioRecorder>("AudioRecorder", constructors, sol::call_constructor, constructors);
        t["getDroppedFrames"] = &trussc::AudioRecorder::getDroppedFrames;
        t["getPath"] = &trussc::AudioRecorder::getPath;
        t["getRecordedSeconds"] = &trussc::AudioRecorder::getRecordedSeconds;
        t["isRecording"] = &trussc::AudioRecorder::isRecording;
        t["start"] = sol::overload([](trussc::AudioRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::AudioRecorder& self, const fs::path & path, const trussc::AudioRecordSettings & settings) { return self.start(path, settings); });
        t["stop"] = &trussc::AudioRecorder::stop;
    }
    lua->new_usertype<trussc::PrimitiveMode>("PrimitiveMode",
        sol::meta_function::equal_to, [](trussc::PrimitiveMode a, trussc::PrimitiveMode b){ return a == b; },
        "Triangles", sol::var(trussc::PrimitiveMode::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveMode::TriangleStrip),
        "TriangleFan", sol::var(trussc::PrimitiveMode::TriangleFan),
        "Lines", sol::var(trussc::PrimitiveMode::Lines),
        "LineStrip", sol::var(trussc::PrimitiveMode::LineStrip),
        "LineLoop", sol::var(trussc::PrimitiveMode::LineLoop),
        "Points", sol::var(trussc::PrimitiveMode::Points));
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    lua->new_usertype<trussc::KinsokuLevel>("KinsokuLevel",
        sol::meta_function::equal_to, [](trussc::KinsokuLevel a, trussc::KinsokuLevel b){ return a == b; },
        "Off", sol::var(trussc::KinsokuLevel::Off),
        "PunctuationOnly", sol::var(trussc::KinsokuLevel::PunctuationOnly),
        "Standard", sol::var(trussc::KinsokuLevel::Standard));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
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
