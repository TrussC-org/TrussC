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
        auto constructors = tcxLuaConstructors<trussc::GlyphBatch
            , trussc::GlyphBatch()
        >();
        sol::usertype<trussc::GlyphBatch> t = lua->new_usertype<trussc::GlyphBatch>("GlyphBatch", constructors, sol::call_constructor, constructors);
        t["add"] = sol::overload([](trussc::GlyphBatch& self, uint32_t codepoint, const trussc::Color & color) { return self.add(codepoint, color); }, [](trussc::GlyphBatch& self, uint32_t codepoint, const trussc::Mat4 & matrix, const trussc::Color & color) { return self.add(codepoint, matrix, color); }, [](trussc::GlyphBatch& self, const std::string & text, const trussc::Color & color) { return self.add(text, color); });
        t["clear"] = &trussc::GlyphBatch::clear;
        t["draw"] = sol::overload([](trussc::GlyphBatch& self) { return self.draw(); }, [](trussc::GlyphBatch& self, const trussc::Mat4 & transform) { return self.draw(transform); });
        t["setAlign"] = &trussc::GlyphBatch::setAlign;
        t["setAlignToInk"] = &trussc::GlyphBatch::setAlignToInk;
        t["setFont"] = &trussc::GlyphBatch::setFont;
        t["setWritingMode"] = &trussc::GlyphBatch::setWritingMode;
    }
    {
        sol::usertype<trussc::MouseMoveEventArgs> t = lua->new_usertype<trussc::MouseMoveEventArgs>("MouseMoveEventArgs");
        t["alt"] = &trussc::MouseMoveEventArgs::alt;
        t["consumed"] = &trussc::MouseMoveEventArgs::consumed;
        t["ctrl"] = &trussc::MouseMoveEventArgs::ctrl;
        t["delta"] = &trussc::MouseMoveEventArgs::delta;
        t["deltaX"] = &trussc::MouseMoveEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseMoveEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseMoveEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseMoveEventArgs::globalPos;
        t["pos"] = &trussc::MouseMoveEventArgs::pos;
        t["shift"] = &trussc::MouseMoveEventArgs::shift;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::MouseEventArgs> t = lua->new_usertype<trussc::MouseEventArgs>("MouseEventArgs");
        t["alt"] = &trussc::MouseEventArgs::alt;
        t["button"] = &trussc::MouseEventArgs::button;
        t["consumed"] = &trussc::MouseEventArgs::consumed;
        t["ctrl"] = &trussc::MouseEventArgs::ctrl;
        t["globalPos"] = &trussc::MouseEventArgs::globalPos;
        t["pos"] = &trussc::MouseEventArgs::pos;
        t["shift"] = &trussc::MouseEventArgs::shift;
        t["super"] = &trussc::MouseEventArgs::super;
        t["x"] = &trussc::MouseEventArgs::x;
        t["y"] = &trussc::MouseEventArgs::y;
        t["syncLegacy"] = &trussc::MouseEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::AudioSettings> t = lua->new_usertype<trussc::AudioSettings>("AudioSettings");
        t["backend"] = &trussc::AudioSettings::backend;
        t["bufferSize"] = &trussc::AudioSettings::bufferSize;
        t["channels"] = &trussc::AudioSettings::channels;
        t["device"] = &trussc::AudioSettings::device;
        t["deviceName"] = &trussc::AudioSettings::deviceName;
        t["maxPolyphony"] = &trussc::AudioSettings::maxPolyphony;
        t["sampleRate"] = &trussc::AudioSettings::sampleRate;
    }
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    lua->new_usertype<trussc::MouseButton>("MouseButton",
        sol::meta_function::equal_to, [](trussc::MouseButton a, trussc::MouseButton b){ return a == b; },
        "Left", sol::var(trussc::MouseButton::Left),
        "Right", sol::var(trussc::MouseButton::Right),
        "Middle", sol::var(trussc::MouseButton::Middle),
        "None", sol::var(trussc::MouseButton::None));
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
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
        sol::usertype<trussc::FilesOpenedEventArgs> t = lua->new_usertype<trussc::FilesOpenedEventArgs>("FilesOpenedEventArgs");
        t["files"] = &trussc::FilesOpenedEventArgs::files;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
