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
        t["handleUpdate"] = &trussc::App::handleUpdate;
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
    {
        sol::usertype<trussc::PlayingSoundInfo> t = lua->new_usertype<trussc::PlayingSoundInfo>("PlayingSoundInfo");
        t["duration"] = &trussc::PlayingSoundInfo::duration;
        t["level"] = &trussc::PlayingSoundInfo::level;
        t["loop"] = &trussc::PlayingSoundInfo::loop;
        t["pan"] = &trussc::PlayingSoundInfo::pan;
        t["path"] = &trussc::PlayingSoundInfo::path;
        t["paused"] = &trussc::PlayingSoundInfo::paused;
        t["position"] = &trussc::PlayingSoundInfo::position;
        t["slot"] = &trussc::PlayingSoundInfo::slot;
        t["speed"] = &trussc::PlayingSoundInfo::speed;
        t["streaming"] = &trussc::PlayingSoundInfo::streaming;
        t["volume"] = &trussc::PlayingSoundInfo::volume;
    }
    lua->new_usertype<trussc::PrimitiveType>("PrimitiveType",
        sol::meta_function::equal_to, [](trussc::PrimitiveType a, trussc::PrimitiveType b){ return a == b; },
        "Points", sol::var(trussc::PrimitiveType::Points),
        "Lines", sol::var(trussc::PrimitiveType::Lines),
        "LineStrip", sol::var(trussc::PrimitiveType::LineStrip),
        "Triangles", sol::var(trussc::PrimitiveType::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveType::TriangleStrip),
        "Quads", sol::var(trussc::PrimitiveType::Quads));
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["key"] = &trussc::KeyEventArgs::key;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["super"] = &trussc::KeyEventArgs::super;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
    {
        sol::usertype<trussc::TcpReceiveEventArgs> t = lua->new_usertype<trussc::TcpReceiveEventArgs>("TcpReceiveEventArgs");
        t["data"] = &trussc::TcpReceiveEventArgs::data;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
