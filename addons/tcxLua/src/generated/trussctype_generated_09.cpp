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
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["height"] = &trussc::WindowSettings::height;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["maxFrameLatency"] = &trussc::WindowSettings::maxFrameLatency;
        t["pipelinePoolSize"] = &trussc::WindowSettings::pipelinePoolSize;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["quitOnClose"] = &trussc::WindowSettings::quitOnClose;
        t["resourcePoolSize"] = &trussc::WindowSettings::resourcePoolSize;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["shaderPoolSize"] = &trussc::WindowSettings::shaderPoolSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["title"] = &trussc::WindowSettings::title;
        t["visible"] = &trussc::WindowSettings::visible;
        t["width"] = &trussc::WindowSettings::width;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setMaxFrameLatency"] = &trussc::WindowSettings::setMaxFrameLatency;
        t["setParentWindow"] = sol::overload([](trussc::WindowSettings& self, const std::shared_ptr<trussc::Window> & parent) -> decltype(auto) { return self.setParentWindow(parent); }, [](trussc::WindowSettings& self, trussc::Window & parent) -> decltype(auto) { return self.setParentWindow(parent); });
        t["setPipelinePoolSize"] = &trussc::WindowSettings::setPipelinePoolSize;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setQuitOnClose"] = &trussc::WindowSettings::setQuitOnClose;
        t["setResourcePoolSize"] = &trussc::WindowSettings::setResourcePoolSize;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setShaderPoolSize"] = &trussc::WindowSettings::setShaderPoolSize;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
        t["setVisible"] = &trussc::WindowSettings::setVisible;
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
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["build"] = &trussc::ChipSoundBundle::build;
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
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
    lua->new_usertype<trussc::PrimitiveType>("PrimitiveType",
        sol::meta_function::equal_to, [](trussc::PrimitiveType a, trussc::PrimitiveType b){ return a == b; },
        "Points", sol::var(trussc::PrimitiveType::Points),
        "Lines", sol::var(trussc::PrimitiveType::Lines),
        "LineStrip", sol::var(trussc::PrimitiveType::LineStrip),
        "Triangles", sol::var(trussc::PrimitiveType::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveType::TriangleStrip),
        "Quads", sol::var(trussc::PrimitiveType::Quads));
    lua->new_usertype<trussc::LogLevel>("LogLevel",
        sol::meta_function::equal_to, [](trussc::LogLevel a, trussc::LogLevel b){ return a == b; },
        "Verbose", sol::var(trussc::LogLevel::Verbose),
        "Notice", sol::var(trussc::LogLevel::Notice),
        "Warning", sol::var(trussc::LogLevel::Warning),
        "Error", sol::var(trussc::LogLevel::Error),
        "Fatal", sol::var(trussc::LogLevel::Fatal),
        "Silent", sol::var(trussc::LogLevel::Silent));
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
    lua->new_usertype<trussc::TextureFilter>("TextureFilter",
        sol::meta_function::equal_to, [](trussc::TextureFilter a, trussc::TextureFilter b){ return a == b; },
        "Nearest", sol::var(trussc::TextureFilter::Nearest),
        "Linear", sol::var(trussc::TextureFilter::Linear));
    {
        sol::usertype<trussc::DeviceLostEventArgs> t = lua->new_usertype<trussc::DeviceLostEventArgs>("DeviceLostEventArgs");
        t["cancel"] = &trussc::DeviceLostEventArgs::cancel;
        t["reason"] = &trussc::DeviceLostEventArgs::reason;
    }
    {
        sol::usertype<trussc::OccludedEventArgs> t = lua->new_usertype<trussc::OccludedEventArgs>("OccludedEventArgs");
        t["occluded"] = &trussc::OccludedEventArgs::occluded;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
