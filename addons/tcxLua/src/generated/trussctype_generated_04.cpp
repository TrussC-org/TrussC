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
void tcxLuaGenShard_04(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::App
            , trussc::App()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::App>::user_metatable()];
        sol::usertype<trussc::App> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::App>>()
            : lua->new_usertype<trussc::App>("App", constructors, sol::call_constructor, constructors);
        (*lua)["App"] = t;
        t["localMatrixChanged"] = &trussc::App::localMatrixChanged;
        t["setSize"] = &trussc::App::setSize;
        t["requestExit"] = &trussc::App::requestExit;
        t["isExitRequested"] = &trussc::App::isExitRequested;
        t["getWindow"] = &trussc::App::getWindow;
        t["keyPressed"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyPressed(e); }, [](trussc::App& self, int key) { return self.keyPressed(key); });
        t["keyReleased"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyReleased(e); }, [](trussc::App& self, int key) { return self.keyReleased(key); });
        t["mousePressed"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mousePressed(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mousePressed(pos, button); });
        t["mouseReleased"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mouseReleased(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseReleased(pos, button); });
        t["mouseMoved"] = sol::overload([](trussc::App& self, const trussc::MouseMoveEventArgs & e) { return self.mouseMoved(e); }, [](trussc::App& self, trussc::Vec2 pos) { return self.mouseMoved(pos); });
        t["mouseDragged"] = sol::overload([](trussc::App& self, const trussc::MouseDragEventArgs & e) { return self.mouseDragged(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseDragged(pos, button); });
        t["mouseScrolled"] = sol::overload([](trussc::App& self, const trussc::ScrollEventArgs & e) { return self.mouseScrolled(e); }, [](trussc::App& self, trussc::Vec2 delta) { return self.mouseScrolled(delta); });
        t["touchPressed"] = &trussc::App::touchPressed;
        t["touchMoved"] = &trussc::App::touchMoved;
        t["touchReleased"] = &trussc::App::touchReleased;
        t["windowResized"] = &trussc::App::windowResized;
        t["filesDropped"] = &trussc::App::filesDropped;
        t["exit"] = &trussc::App::exit;
        t["audioOut"] = &trussc::App::audioOut;
        t["audioIn"] = &trussc::App::audioIn;
        t["handleKeyPressed"] = &trussc::App::handleKeyPressed;
        t["handleKeyReleased"] = &trussc::App::handleKeyReleased;
        t["handleMousePressed"] = &trussc::App::handleMousePressed;
        t["handleMouseReleased"] = &trussc::App::handleMouseReleased;
        t["handleMouseScrolled"] = &trussc::App::handleMouseScrolled;
        t["handleWindowResized"] = &trussc::App::handleWindowResized;
        t["handleUpdate"] = &trussc::App::handleUpdate;
        t["handleDraw"] = &trussc::App::handleDraw;
        t["getWidth"] = &trussc::App::getWidth;
        t["getHeight"] = &trussc::App::getHeight;
        t["getSize"] = &trussc::App::getSize;
        t["setWidth"] = &trussc::App::setWidth;
        t["setHeight"] = &trussc::App::setHeight;
        t["setRect"] = &trussc::App::setRect;
        t["setClipping"] = &trussc::App::setClipping;
        t["isClipping"] = &trussc::App::isClipping;
        t["getLeft"] = &trussc::App::getLeft;
        t["getRight"] = &trussc::App::getRight;
        t["getTop"] = &trussc::App::getTop;
        t["getBottom"] = &trussc::App::getBottom;
        t["hitTest"] = [](trussc::App& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["draw"] = &trussc::App::draw;
        t["setup"] = &trussc::App::setup;
        t["update"] = &trussc::App::update;
        t["cleanup"] = &trussc::App::cleanup;
        t["addChild"] = sol::overload([](trussc::App& self, trussc::App::Ptr child) { return self.addChild(child); }, [](trussc::App& self, trussc::App::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::App& self, size_t index, trussc::App::Ptr child) { return self.insertChild(index, child); }, [](trussc::App& self, size_t index, trussc::App::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::App::removeChild;
        t["removeAllChildren"] = &trussc::App::removeAllChildren;
        t["onChildAdded"] = &trussc::App::onChildAdded;
        t["onChildRemoved"] = &trussc::App::onChildRemoved;
        t["getParent"] = &trussc::App::getParent;
        t["getChildren"] = &trussc::App::getChildren;
        t["getChildCount"] = &trussc::App::getChildCount;
        t["getChildIndex"] = &trussc::App::getChildIndex;
        t["moveToFront"] = &trussc::App::moveToFront;
        t["moveToBack"] = &trussc::App::moveToBack;
        t["isActive"] = &trussc::App::isActive;
        t["setActive"] = &trussc::App::setActive;
        t["isVisible"] = &trussc::App::isVisible;
        t["setVisible"] = &trussc::App::setVisible;
        t["getActive"] = &trussc::App::getActive;
        t["getVisible"] = &trussc::App::getVisible;
        t["setIsActive"] = &trussc::App::setIsActive;
        t["setIsVisible"] = &trussc::App::setIsVisible;
        t["destroy"] = &trussc::App::destroy;
        t["isDead"] = &trussc::App::isDead;
        t["enableEvents"] = &trussc::App::enableEvents;
        t["disableEvents"] = &trussc::App::disableEvents;
        t["isEventsEnabled"] = &trussc::App::isEventsEnabled;
        t["isMouseOver"] = &trussc::App::isMouseOver;
        t["setName"] = &trussc::App::setName;
        t["getName"] = &trussc::App::getName;
        t["hasName"] = &trussc::App::hasName;
        t["getTypeName"] = &trussc::App::getTypeName;
        t["getDisplayName"] = &trussc::App::getDisplayName;
        t["getInstanceId"] = &trussc::App::getInstanceId;
        t["findByInstanceId"] = &trussc::App::findByInstanceId;
        t["getPos"] = &trussc::App::getPos;
        t["getX"] = &trussc::App::getX;
        t["getY"] = &trussc::App::getY;
        t["getZ"] = &trussc::App::getZ;
        t["setPos"] = sol::overload([](trussc::App& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::App& self, float x, float y) { return self.setPos(x, y); }, [](trussc::App& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::App::setX;
        t["setY"] = &trussc::App::setY;
        t["setZ"] = &trussc::App::setZ;
        t["getQuaternion"] = &trussc::App::getQuaternion;
        t["setQuaternion"] = &trussc::App::setQuaternion;
        t["getEuler"] = &trussc::App::getEuler;
        t["setEuler"] = sol::overload([](trussc::App& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::App& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::App::getEulerDeg;
        t["setEulerDeg"] = &trussc::App::setEulerDeg;
        t["getRot"] = &trussc::App::getRot;
        t["setRot"] = &trussc::App::setRot;
        t["getRotDeg"] = &trussc::App::getRotDeg;
        t["setRotDeg"] = &trussc::App::setRotDeg;
        t["getScale"] = &trussc::App::getScale;
        t["getScaleX"] = &trussc::App::getScaleX;
        t["getScaleY"] = &trussc::App::getScaleY;
        t["getScaleZ"] = &trussc::App::getScaleZ;
        t["setScale"] = sol::overload([](trussc::App& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::App& self, float uniform) { return self.setScale(uniform); }, [](trussc::App& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::App& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::App::setScaleX;
        t["setScaleY"] = &trussc::App::setScaleY;
        t["setScaleZ"] = &trussc::App::setScaleZ;
        t["getLocalMatrix"] = &trussc::App::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::App::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::App::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::App& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::App::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::App& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::App& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::App& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::App& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::App::getMouseX;
        t["getMouseY"] = &trussc::App::getMouseY;
        t["getPMouseX"] = &trussc::App::getPMouseX;
        t["getPMouseY"] = &trussc::App::getPMouseY;
        t["findHitNode"] = &trussc::App::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::App::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::App::getCameraContext;
        t["setCameraContext"] = &trussc::App::setCameraContext;
        t["getModTypeNames"] = &trussc::App::getModTypeNames;
        t["getMods"] = &trussc::App::getMods;
        t["getModByTypeName"] = &trussc::App::getModByTypeName;
        t["callAfter"] = &trussc::App::callAfter;
        t["callEvery"] = &trussc::App::callEvery;
        t["callEveryCatchUp"] = &trussc::App::callEveryCatchUp;
        t["cancelTimer"] = &trussc::App::cancelTimer;
        t["cancelAllTimers"] = &trussc::App::cancelAllTimers;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::StrokeMesh
            , trussc::StrokeMesh()
            , trussc::StrokeMesh(const trussc::Path &)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::StrokeMesh>::user_metatable()];
        sol::usertype<trussc::StrokeMesh> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::StrokeMesh>>()
            : lua->new_usertype<trussc::StrokeMesh>("StrokeMesh", constructors, sol::call_constructor, constructors);
        (*lua)["StrokeMesh"] = t;
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["clear"] = &trussc::StrokeMesh::clear;
        t["update"] = &trussc::StrokeMesh::update;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLCH
            , trussc::ColorOKLCH()
            , trussc::ColorOKLCH(float, float, float)
            , trussc::ColorOKLCH(float, float, float, float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ColorOKLCH>::user_metatable()];
        sol::usertype<trussc::ColorOKLCH> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ColorOKLCH>>()
            : lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH", constructors, sol::call_constructor, constructors);
        (*lua)["ColorOKLCH"] = t;
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
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ScrollEventArgs>::user_metatable()];
        sol::usertype<trussc::ScrollEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ScrollEventArgs>>()
            : lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        (*lua)["ScrollEventArgs"] = t;
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::LoadResult>::user_metatable()];
        sol::usertype<trussc::LoadResult> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::LoadResult>>()
            : lua->new_usertype<trussc::LoadResult>("LoadResult");
        (*lua)["LoadResult"] = t;
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::FpsSettings>::user_metatable()];
        sol::usertype<trussc::FpsSettings> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::FpsSettings>>()
            : lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        (*lua)["FpsSettings"] = t;
        t["updateFps"] = &trussc::FpsSettings::updateFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["synced"] = &trussc::FpsSettings::synced;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::VideoErrorEventArgs>::user_metatable()];
        sol::usertype<trussc::VideoErrorEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::VideoErrorEventArgs>>()
            : lua->new_usertype<trussc::VideoErrorEventArgs>("VideoErrorEventArgs");
        (*lua)["VideoErrorEventArgs"] = t;
        t["message"] = &trussc::VideoErrorEventArgs::message;
        t["errorCode"] = &trussc::VideoErrorEventArgs::errorCode;
    }
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    lua->new_usertype<trussc::Deliver>("Deliver",
        sol::meta_function::equal_to, [](trussc::Deliver a, trussc::Deliver b){ return a == b; },
        "Inline", sol::var(trussc::Deliver::Inline),
        "Main", sol::var(trussc::Deliver::Main));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
