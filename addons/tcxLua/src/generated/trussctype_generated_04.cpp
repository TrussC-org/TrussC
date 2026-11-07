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
void tcxLuaGenShard_04(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::App
            , trussc::App()
        >();
        sol::usertype<trussc::App> t = lua->new_usertype<trussc::App>("App", constructors, sol::call_constructor, constructors);
        t["localMatrixChanged"] = &trussc::App::localMatrixChanged;
        t["addChild"] = sol::overload([](trussc::App& self, trussc::App::Ptr child) { return self.addChild(child); }, [](trussc::App& self, trussc::App::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["audioIn"] = &trussc::App::audioIn;
        t["audioOut"] = &trussc::App::audioOut;
        t["callAfter"] = &trussc::App::callAfter;
        t["callEvery"] = &trussc::App::callEvery;
        t["callEveryCatchUp"] = &trussc::App::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::App::cancelAllTimers;
        t["cancelTimer"] = &trussc::App::cancelTimer;
        t["cleanup"] = &trussc::App::cleanup;
        t["destroy"] = &trussc::App::destroy;
        t["disableEvents"] = &trussc::App::disableEvents;
        t["draw"] = &trussc::App::draw;
        t["enableEvents"] = &trussc::App::enableEvents;
        t["exit"] = &trussc::App::exit;
        t["filesDropped"] = &trussc::App::filesDropped;
        t["findByInstanceId"] = &trussc::App::findByInstanceId;
        t["findHitNode"] = &trussc::App::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::App::findHitNodeFromScreen;
        t["getActive"] = &trussc::App::getActive;
        t["getBottom"] = &trussc::App::getBottom;
        t["getCameraContext"] = &trussc::App::getCameraContext;
        t["getChildCount"] = &trussc::App::getChildCount;
        t["getChildIndex"] = &trussc::App::getChildIndex;
        t["getChildren"] = &trussc::App::getChildren;
        t["getDisplayName"] = &trussc::App::getDisplayName;
        t["getEuler"] = &trussc::App::getEuler;
        t["getEulerDeg"] = &trussc::App::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::App::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::App::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::App::getGlobalPos;
        t["getHeight"] = &trussc::App::getHeight;
        t["getInstanceId"] = &trussc::App::getInstanceId;
        t["getLeft"] = &trussc::App::getLeft;
        t["getLocalMatrix"] = &trussc::App::getLocalMatrix;
        t["getModByTypeName"] = &trussc::App::getModByTypeName;
        t["getModTypeNames"] = &trussc::App::getModTypeNames;
        t["getMods"] = &trussc::App::getMods;
        t["getMouseX"] = &trussc::App::getMouseX;
        t["getMouseY"] = &trussc::App::getMouseY;
        t["getName"] = &trussc::App::getName;
        t["getPMouseX"] = &trussc::App::getPMouseX;
        t["getPMouseY"] = &trussc::App::getPMouseY;
        t["getParent"] = &trussc::App::getParent;
        t["getPos"] = &trussc::App::getPos;
        t["getQuaternion"] = &trussc::App::getQuaternion;
        t["getRight"] = &trussc::App::getRight;
        t["getRot"] = &trussc::App::getRot;
        t["getRotDeg"] = &trussc::App::getRotDeg;
        t["getScale"] = &trussc::App::getScale;
        t["getScaleX"] = &trussc::App::getScaleX;
        t["getScaleY"] = &trussc::App::getScaleY;
        t["getScaleZ"] = &trussc::App::getScaleZ;
        t["getSize"] = &trussc::App::getSize;
        t["getTop"] = &trussc::App::getTop;
        t["getTypeName"] = &trussc::App::getTypeName;
        t["getVisible"] = &trussc::App::getVisible;
        t["getWidth"] = &trussc::App::getWidth;
        t["getWindow"] = &trussc::App::getWindow;
        t["getX"] = &trussc::App::getX;
        t["getY"] = &trussc::App::getY;
        t["getZ"] = &trussc::App::getZ;
        t["globalToLocal"] = [](trussc::App& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["handleDraw"] = &trussc::App::handleDraw;
        t["handleKeyPressed"] = &trussc::App::handleKeyPressed;
        t["handleKeyReleased"] = &trussc::App::handleKeyReleased;
        t["handleMouseDragged"] = [](trussc::App& self, const trussc::MouseDragEventArgs & e) { return self.handleMouseDragged(e); };
        t["handleMouseMoved"] = [](trussc::App& self, const trussc::MouseMoveEventArgs & e) { return self.handleMouseMoved(e); };
        t["handleMousePressed"] = &trussc::App::handleMousePressed;
        t["handleMouseReleased"] = &trussc::App::handleMouseReleased;
        t["handleMouseScrolled"] = &trussc::App::handleMouseScrolled;
        t["handleUpdate"] = sol::overload([](trussc::App& self) { return self.handleUpdate(); }, [](trussc::App& self, int a0, int a1) { return self.handleUpdate(a0, a1); });
        t["handleWindowResized"] = &trussc::App::handleWindowResized;
        t["hasName"] = &trussc::App::hasName;
        t["hitTest"] = [](trussc::App& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::App& self, size_t index, trussc::App::Ptr child) { return self.insertChild(index, child); }, [](trussc::App& self, size_t index, trussc::App::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::App::isActive;
        t["isClipping"] = &trussc::App::isClipping;
        t["isDead"] = &trussc::App::isDead;
        t["isEventsEnabled"] = &trussc::App::isEventsEnabled;
        t["isExitRequested"] = &trussc::App::isExitRequested;
        t["isMouseOver"] = &trussc::App::isMouseOver;
        t["isVisible"] = &trussc::App::isVisible;
        t["keyPressed"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyPressed(e); }, [](trussc::App& self, int key) { return self.keyPressed(key); });
        t["keyReleased"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyReleased(e); }, [](trussc::App& self, int key) { return self.keyReleased(key); });
        t["localToGlobal"] = [](trussc::App& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["mouseDragged"] = sol::overload([](trussc::App& self, const trussc::MouseDragEventArgs & e) { return self.mouseDragged(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseDragged(pos, button); });
        t["mouseMoved"] = sol::overload([](trussc::App& self, const trussc::MouseMoveEventArgs & e) { return self.mouseMoved(e); }, [](trussc::App& self, trussc::Vec2 pos) { return self.mouseMoved(pos); });
        t["mousePressed"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mousePressed(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mousePressed(pos, button); });
        t["mouseReleased"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mouseReleased(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseReleased(pos, button); });
        t["mouseScrolled"] = sol::overload([](trussc::App& self, const trussc::ScrollEventArgs & e) { return self.mouseScrolled(e); }, [](trussc::App& self, trussc::Vec2 delta) { return self.mouseScrolled(delta); });
        t["moveToBack"] = &trussc::App::moveToBack;
        t["moveToFront"] = &trussc::App::moveToFront;
        t["onChildAdded"] = &trussc::App::onChildAdded;
        t["onChildRemoved"] = &trussc::App::onChildRemoved;
        t["removeAllChildren"] = &trussc::App::removeAllChildren;
        t["removeChild"] = &trussc::App::removeChild;
        t["requestExit"] = &trussc::App::requestExit;
        t["setActive"] = &trussc::App::setActive;
        t["setCameraContext"] = &trussc::App::setCameraContext;
        t["setClipping"] = &trussc::App::setClipping;
        t["setEuler"] = sol::overload([](trussc::App& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::App& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::App::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::App& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::App& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::App& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::App::setHeight;
        t["setIsActive"] = &trussc::App::setIsActive;
        t["setIsVisible"] = &trussc::App::setIsVisible;
        t["setName"] = &trussc::App::setName;
        t["setPos"] = sol::overload([](trussc::App& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::App& self, float x, float y) { return self.setPos(x, y); }, [](trussc::App& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::App::setQuaternion;
        t["setRect"] = &trussc::App::setRect;
        t["setRot"] = &trussc::App::setRot;
        t["setRotDeg"] = &trussc::App::setRotDeg;
        t["setScale"] = sol::overload([](trussc::App& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::App& self, float uniform) { return self.setScale(uniform); }, [](trussc::App& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::App& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::App::setScaleX;
        t["setScaleY"] = &trussc::App::setScaleY;
        t["setScaleZ"] = &trussc::App::setScaleZ;
        t["setSize"] = &trussc::App::setSize;
        t["setVisible"] = &trussc::App::setVisible;
        t["setWidth"] = &trussc::App::setWidth;
        t["setX"] = &trussc::App::setX;
        t["setY"] = &trussc::App::setY;
        t["setZ"] = &trussc::App::setZ;
        t["setup"] = &trussc::App::setup;
        t["touchMoved"] = &trussc::App::touchMoved;
        t["touchPressed"] = &trussc::App::touchPressed;
        t["touchReleased"] = &trussc::App::touchReleased;
        t["update"] = &trussc::App::update;
        t["windowFocusChanged"] = &trussc::App::windowFocusChanged;
        t["windowResized"] = &trussc::App::windowResized;
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
        auto constructors = tcxLuaConstructors<trussc::IVec2
            , trussc::IVec2()
            , trussc::IVec2(int, int)
            , trussc::IVec2(int)
        >();
        sol::usertype<trussc::IVec2> t = lua->new_usertype<trussc::IVec2>("IVec2", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec2& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec2& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec2& a, const trussc::IVec2 & b){ return a == b; };
        t["x"] = &trussc::IVec2::x;
        t["y"] = &trussc::IVec2::y;
        t["toVec2"] = &trussc::IVec2::toVec2;
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
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["pickable"] = &trussc::CameraContext::pickable;
        t["projection"] = &trussc::CameraContext::projection;
        t["view"] = &trussc::CameraContext::view;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    {
        sol::usertype<trussc::Node::HitResult> t = lua->new_usertype<trussc::Node::HitResult>("NodeHitResult");
        t["distance"] = &trussc::Node::HitResult::distance;
        t["localPoint"] = &trussc::Node::HitResult::localPoint;
        t["node"] = &trussc::Node::HitResult::node;
        t["hit"] = &trussc::Node::HitResult::hit;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["accuracy"] = &trussc::Location::accuracy;
        t["altitude"] = &trussc::Location::altitude;
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
    }
    lua->new_usertype<trussc::ImageType>("ImageType",
        sol::meta_function::equal_to, [](trussc::ImageType a, trussc::ImageType b){ return a == b; },
        "Color", sol::var(trussc::ImageType::Color),
        "Grayscale", sol::var(trussc::ImageType::Grayscale));
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
