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
        t["localMatrixChanged"] = &trussc::RectNode::localMatrixChanged;
        t["mouseDragged"] = &trussc::RectNode::mouseDragged;
        t["mousePressed"] = &trussc::RectNode::mousePressed;
        t["mouseReleased"] = &trussc::RectNode::mouseReleased;
        t["mouseScrolled"] = &trussc::RectNode::mouseScrolled;
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
        auto constructors = tcxLuaConstructors<trussc::FullscreenShader
            , trussc::FullscreenShader()
        >();
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader", constructors, sol::call_constructor, constructors);
        t["begin"] = &trussc::FullscreenShader::begin;
        t["clear"] = &trussc::FullscreenShader::clear;
        t["draw"] = &trussc::FullscreenShader::draw;
        t["drawVertices"] = sol::overload([](trussc::FullscreenShader& self, int vertexCount) { return self.drawVertices(vertexCount); }, [](trussc::FullscreenShader& self, int vertexCount, int instanceCount) { return self.drawVertices(vertexCount, instanceCount); });
        t["end"] = &trussc::FullscreenShader::end;
        t["isLoaded"] = &trussc::FullscreenShader::isLoaded;
        t["load"] = &trussc::FullscreenShader::load;
        t["setTexture"] = sol::overload([](trussc::FullscreenShader& self, int slot, sg_image image, sg_sampler sampler) { return self.setTexture(slot, image, sampler); }, [](trussc::FullscreenShader& self, int slot, sg_view view, sg_sampler sampler) { return self.setTexture(slot, view, sampler); });
        t["setUniform"] = sol::overload([](trussc::FullscreenShader& self, int slot, float value) { return self.setUniform(slot, value); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec2 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec3 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec4 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Color & c) { return self.setUniform(slot, c); }, [](trussc::FullscreenShader& self, int slot, const std::vector<float> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec2> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec3> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec4> & v) { return self.setUniform(slot, v); });
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
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget),
        "Storage", sol::var(trussc::TextureUsage::Storage));
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
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
    {
        sol::usertype<trussc::FocusEventArgs> t = lua->new_usertype<trussc::FocusEventArgs>("FocusEventArgs");
        t["focused"] = &trussc::FocusEventArgs::focused;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
