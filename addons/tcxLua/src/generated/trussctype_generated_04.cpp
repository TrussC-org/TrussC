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
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::App>(&trussc::App::localMatrixChanged);
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
        auto constructors = tcxLuaConstructors<trussc::ColorLinear
            , trussc::ColorLinear()
            , trussc::ColorLinear(float, float, float)
            , trussc::ColorLinear(float, float, float, float)
            , trussc::ColorLinear(float)
            , trussc::ColorLinear(float, float)
        >();
        sol::usertype<trussc::ColorLinear> t = lua->new_usertype<trussc::ColorLinear>("ColorLinear", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::ColorLinear& a, float b){ return a * b; }, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a - b; };
        t[sol::meta_function::division] = [](const trussc::ColorLinear& a, float b){ return a / b; };
        t[sol::meta_function::equal_to] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a == b; };
        t["a"] = &trussc::ColorLinear::a;
        t["b"] = &trussc::ColorLinear::b;
        t["g"] = &trussc::ColorLinear::g;
        t["r"] = &trussc::ColorLinear::r;
        t["clamped"] = &trussc::ColorLinear::clamped;
        t["clampedLDR"] = &trussc::ColorLinear::clampedLDR;
        t["lerp"] = &trussc::ColorLinear::lerp;
        t["toHSB"] = &trussc::ColorLinear::toHSB;
        t["toOKLCH"] = &trussc::ColorLinear::toOKLCH;
        t["toOKLab"] = &trussc::ColorLinear::toOKLab;
        t["toSRGB"] = &trussc::ColorLinear::toSRGB;
    }
    {
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
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
        sol::usertype<trussc::Font::PlacedGlyph> t = lua->new_usertype<trussc::Font::PlacedGlyph>("FontPlacedGlyph");
        t["baselineY"] = &trussc::Font::PlacedGlyph::baselineY;
        t["codepoint"] = &trussc::Font::PlacedGlyph::codepoint;
        t["drawX"] = &trussc::Font::PlacedGlyph::drawX;
        t["pivotX"] = &trussc::Font::PlacedGlyph::pivotX;
        t["pivotY"] = &trussc::Font::PlacedGlyph::pivotY;
        t["rotationCw"] = &trussc::Font::PlacedGlyph::rotationCw;
        t["scaleX"] = &trussc::Font::PlacedGlyph::scaleX;
    }
    lua->new_usertype<trussc::EasyCam::Modifier>("EasyCamModifier",
        sol::meta_function::equal_to, [](trussc::EasyCam::Modifier a, trussc::EasyCam::Modifier b){ return a == b; },
        "None", sol::var(trussc::EasyCam::Modifier::None),
        "Shift", sol::var(trussc::EasyCam::Modifier::Shift),
        "Ctrl", sol::var(trussc::EasyCam::Modifier::Ctrl),
        "Alt", sol::var(trussc::EasyCam::Modifier::Alt),
        "Super", sol::var(trussc::EasyCam::Modifier::Super));
    {
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["id"] = &trussc::TouchEventArgs::id;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    {
        sol::usertype<trussc::TcpErrorEventArgs> t = lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpErrorEventArgs::message;
    }
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
