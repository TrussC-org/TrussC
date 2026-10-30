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
        sol::usertype<trussc::CoreEvents> t = lua->new_usertype<trussc::CoreEvents>("CoreEvents");
        t["afterFrame"] = &trussc::CoreEvents::afterFrame;
        t["clipboardPasted"] = &trussc::CoreEvents::clipboardPasted;
        t["console"] = &trussc::CoreEvents::console;
        t["deviceLost"] = &trussc::CoreEvents::deviceLost;
        t["draw"] = &trussc::CoreEvents::draw;
        t["exit"] = &trussc::CoreEvents::exit;
        t["exitRequested"] = &trussc::CoreEvents::exitRequested;
        t["filesDropped"] = &trussc::CoreEvents::filesDropped;
        t["hotReloadUnload"] = &trussc::CoreEvents::hotReloadUnload;
        t["keyPressed"] = &trussc::CoreEvents::keyPressed;
        t["keyReleased"] = &trussc::CoreEvents::keyReleased;
        t["mouseDragged"] = &trussc::CoreEvents::mouseDragged;
        t["mouseMoved"] = &trussc::CoreEvents::mouseMoved;
        t["mousePressed"] = &trussc::CoreEvents::mousePressed;
        t["mouseReleased"] = &trussc::CoreEvents::mouseReleased;
        t["mouseScrolled"] = &trussc::CoreEvents::mouseScrolled;
        t["onRender"] = &trussc::CoreEvents::onRender;
        t["rawEvent"] = &trussc::CoreEvents::rawEvent;
        t["setup"] = &trussc::CoreEvents::setup;
        t["touchMoved"] = &trussc::CoreEvents::touchMoved;
        t["touchPressed"] = &trussc::CoreEvents::touchPressed;
        t["touchReleased"] = &trussc::CoreEvents::touchReleased;
        t["update"] = &trussc::CoreEvents::update;
        t["windowResized"] = &trussc::CoreEvents::windowResized;
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
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["level"] = &trussc::PlayingSound::level;
        t["loop"] = &trussc::PlayingSound::loop;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["pan"] = &trussc::PlayingSound::pan;
        t["paused"] = &trussc::PlayingSound::paused;
        t["playing"] = &trussc::PlayingSound::playing;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
        t["speed"] = &trussc::PlayingSound::speed;
        t["volume"] = &trussc::PlayingSound::volume;
    }
    {
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["a"] = &trussc::ShaderVertex::a;
        t["b"] = &trussc::ShaderVertex::b;
        t["g"] = &trussc::ShaderVertex::g;
        t["r"] = &trussc::ShaderVertex::r;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
    }
    lua->new_usertype<trussc::StrokeMesh::CapType>("StrokeMeshCapType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::CapType a, trussc::StrokeMesh::CapType b){ return a == b; },
        "CAP_BUTT", sol::var(trussc::StrokeMesh::CapType::CAP_BUTT),
        "CAP_ROUND", sol::var(trussc::StrokeMesh::CapType::CAP_ROUND),
        "CAP_SQUARE", sol::var(trussc::StrokeMesh::CapType::CAP_SQUARE));
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
    {
        sol::usertype<trussc::TcpErrorEventArgs> t = lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpErrorEventArgs::message;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
