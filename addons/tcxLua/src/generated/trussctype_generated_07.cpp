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
void tcxLuaGenShard_07(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::ScrollBar> t = lua->new_usertype<trussc::ScrollBar>("ScrollBar");
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::localMatrixChanged);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseDragged);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::ScrollBar>(&trussc::ScrollBar::mouseScrolled);
        t["addChild"] = sol::overload([](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child) { return self.addChild(child); }, [](trussc::ScrollBar& self, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::ScrollBar::callAfter;
        t["callEvery"] = &trussc::ScrollBar::callEvery;
        t["callEveryCatchUp"] = &trussc::ScrollBar::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::ScrollBar::cancelAllTimers;
        t["cancelTimer"] = &trussc::ScrollBar::cancelTimer;
        t["cleanup"] = &trussc::ScrollBar::cleanup;
        t["destroy"] = &trussc::ScrollBar::destroy;
        t["disableEvents"] = &trussc::ScrollBar::disableEvents;
        t["enableEvents"] = &trussc::ScrollBar::enableEvents;
        t["findByInstanceId"] = &trussc::ScrollBar::findByInstanceId;
        t["findHitNode"] = &trussc::ScrollBar::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::ScrollBar::findHitNodeFromScreen;
        t["getActive"] = &trussc::ScrollBar::getActive;
        t["getBarColor"] = &trussc::ScrollBar::getBarColor;
        t["getBarWidth"] = &trussc::ScrollBar::getBarWidth;
        t["getBottom"] = &trussc::ScrollBar::getBottom;
        t["getCameraContext"] = &trussc::ScrollBar::getCameraContext;
        t["getChildCount"] = &trussc::ScrollBar::getChildCount;
        t["getChildIndex"] = &trussc::ScrollBar::getChildIndex;
        t["getChildren"] = &trussc::ScrollBar::getChildren;
        t["getDisplayName"] = &trussc::ScrollBar::getDisplayName;
        t["getEuler"] = &trussc::ScrollBar::getEuler;
        t["getEulerDeg"] = &trussc::ScrollBar::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::ScrollBar::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::ScrollBar::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::ScrollBar::getGlobalPos;
        t["getHeight"] = &trussc::ScrollBar::getHeight;
        t["getInstanceId"] = &trussc::ScrollBar::getInstanceId;
        t["getLeft"] = &trussc::ScrollBar::getLeft;
        t["getLocalMatrix"] = &trussc::ScrollBar::getLocalMatrix;
        t["getMargin"] = &trussc::ScrollBar::getMargin;
        t["getModByTypeName"] = &trussc::ScrollBar::getModByTypeName;
        t["getModTypeNames"] = &trussc::ScrollBar::getModTypeNames;
        t["getMods"] = &trussc::ScrollBar::getMods;
        t["getMouseX"] = &trussc::ScrollBar::getMouseX;
        t["getMouseY"] = &trussc::ScrollBar::getMouseY;
        t["getName"] = &trussc::ScrollBar::getName;
        t["getOffset"] = &trussc::ScrollBar::getOffset;
        t["getPMouseX"] = &trussc::ScrollBar::getPMouseX;
        t["getPMouseY"] = &trussc::ScrollBar::getPMouseY;
        t["getParent"] = &trussc::ScrollBar::getParent;
        t["getPos"] = &trussc::ScrollBar::getPos;
        t["getQuaternion"] = &trussc::ScrollBar::getQuaternion;
        t["getRight"] = &trussc::ScrollBar::getRight;
        t["getRot"] = &trussc::ScrollBar::getRot;
        t["getRotDeg"] = &trussc::ScrollBar::getRotDeg;
        t["getScale"] = &trussc::ScrollBar::getScale;
        t["getScaleX"] = &trussc::ScrollBar::getScaleX;
        t["getScaleY"] = &trussc::ScrollBar::getScaleY;
        t["getScaleZ"] = &trussc::ScrollBar::getScaleZ;
        t["getSize"] = &trussc::ScrollBar::getSize;
        t["getTop"] = &trussc::ScrollBar::getTop;
        t["getTypeName"] = &trussc::ScrollBar::getTypeName;
        t["getVisible"] = &trussc::ScrollBar::getVisible;
        t["getWidth"] = &trussc::ScrollBar::getWidth;
        t["getX"] = &trussc::ScrollBar::getX;
        t["getY"] = &trussc::ScrollBar::getY;
        t["getZ"] = &trussc::ScrollBar::getZ;
        t["globalToLocal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::ScrollBar::hasName;
        t["hitTest"] = [](trussc::ScrollBar& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child) { return self.insertChild(index, child); }, [](trussc::ScrollBar& self, size_t index, trussc::ScrollBar::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::ScrollBar::isActive;
        t["isClipping"] = &trussc::ScrollBar::isClipping;
        t["isDead"] = &trussc::ScrollBar::isDead;
        t["isEventsEnabled"] = &trussc::ScrollBar::isEventsEnabled;
        t["isMouseOver"] = &trussc::ScrollBar::isMouseOver;
        t["isVisible"] = &trussc::ScrollBar::isVisible;
        t["localToGlobal"] = [](trussc::ScrollBar& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::ScrollBar::moveToBack;
        t["moveToFront"] = &trussc::ScrollBar::moveToFront;
        t["onChildAdded"] = &trussc::ScrollBar::onChildAdded;
        t["onChildRemoved"] = &trussc::ScrollBar::onChildRemoved;
        t["removeAllChildren"] = &trussc::ScrollBar::removeAllChildren;
        t["removeChild"] = &trussc::ScrollBar::removeChild;
        t["setActive"] = &trussc::ScrollBar::setActive;
        t["setBarColor"] = &trussc::ScrollBar::setBarColor;
        t["setBarWidth"] = &trussc::ScrollBar::setBarWidth;
        t["setCameraContext"] = &trussc::ScrollBar::setCameraContext;
        t["setClipping"] = &trussc::ScrollBar::setClipping;
        t["setEuler"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::ScrollBar& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::ScrollBar::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::ScrollBar& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::ScrollBar::setHeight;
        t["setIsActive"] = &trussc::ScrollBar::setIsActive;
        t["setIsVisible"] = &trussc::ScrollBar::setIsVisible;
        t["setMargin"] = &trussc::ScrollBar::setMargin;
        t["setName"] = &trussc::ScrollBar::setName;
        t["setPos"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::ScrollBar& self, float x, float y) { return self.setPos(x, y); }, [](trussc::ScrollBar& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::ScrollBar::setQuaternion;
        t["setRect"] = &trussc::ScrollBar::setRect;
        t["setRot"] = &trussc::ScrollBar::setRot;
        t["setRotDeg"] = &trussc::ScrollBar::setRotDeg;
        t["setScale"] = sol::overload([](trussc::ScrollBar& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::ScrollBar& self, float uniform) { return self.setScale(uniform); }, [](trussc::ScrollBar& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::ScrollBar& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::ScrollBar::setScaleX;
        t["setScaleY"] = &trussc::ScrollBar::setScaleY;
        t["setScaleZ"] = &trussc::ScrollBar::setScaleZ;
        t["setSize"] = sol::overload([](trussc::ScrollBar& self, float w, float h) { return self.setSize(w, h); }, [](trussc::ScrollBar& self, float size) { return self.setSize(size); }, [](trussc::ScrollBar& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVisible"] = &trussc::ScrollBar::setVisible;
        t["setWidth"] = &trussc::ScrollBar::setWidth;
        t["setX"] = &trussc::ScrollBar::setX;
        t["setY"] = &trussc::ScrollBar::setY;
        t["setZ"] = &trussc::ScrollBar::setZ;
        t["setup"] = &trussc::ScrollBar::setup;
        t["update"] = &trussc::ScrollBar::update;
        t["updateFromContainer"] = &trussc::ScrollBar::updateFromContainer;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Vec4
            , trussc::Vec4()
            , trussc::Vec4(float, float, float, float)
            , trussc::Vec4(float)
            , trussc::Vec4(const trussc::Vec3 &)
            , trussc::Vec4(const trussc::Vec3 &, float)
            , trussc::Vec4(const trussc::Vec2 &)
            , trussc::Vec4(const trussc::Vec2 &, float)
            , trussc::Vec4(const trussc::Vec2 &, float, float)
        >();
        sol::usertype<trussc::Vec4> t = lua->new_usertype<trussc::Vec4>("Vec4", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a * b; }, [](const trussc::Vec4& a, float b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec4& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a / b; }, [](const trussc::Vec4& a, float b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec4& a, int b){ return a[b]; };
        t["w"] = &trussc::Vec4::w;
        t["x"] = &trussc::Vec4::x;
        t["y"] = &trussc::Vec4::y;
        t["z"] = &trussc::Vec4::z;
        t["dot"] = &trussc::Vec4::dot;
        t["length"] = &trussc::Vec4::length;
        t["lengthSquared"] = &trussc::Vec4::lengthSquared;
        t["lerp"] = &trussc::Vec4::lerp;
        t["normalize"] = &trussc::Vec4::normalize;
        t["normalized"] = &trussc::Vec4::normalized;
        t["set"] = sol::overload([](trussc::Vec4& self, float x_, float y_, float z_, float w_) -> decltype(auto) { return self.set(x_, y_, z_, w_); }, [](trussc::Vec4& self, const trussc::Vec4 & v) -> decltype(auto) { return self.set(v); });
        t["xy"] = &trussc::Vec4::xy;
        t["xyz"] = &trussc::Vec4::xyz;
    }
    {
        sol::usertype<trussc::HasTexture> t = lua->new_usertype<trussc::HasTexture>("HasTexture");
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["save"] = &trussc::HasTexture::save;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
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
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    {
        sol::usertype<trussc::SaveResult> t = lua->new_usertype<trussc::SaveResult>("SaveResult");
        t["error"] = &trussc::SaveResult::error;
        t["message"] = &trussc::SaveResult::message;
        t["ok"] = &trussc::SaveResult::ok;
        t["fail"] = sol::overload([](trussc::SaveError error) { return trussc::SaveResult::fail(error); }, [](trussc::SaveError error, std::string message) { return trussc::SaveResult::fail(error, message); });
        t["success"] = &trussc::SaveResult::success;
    }
    {
        sol::usertype<trussc::TcpSendCompleteEventArgs> t = lua->new_usertype<trussc::TcpSendCompleteEventArgs>("TcpSendCompleteEventArgs");
        t["bytesSent"] = &trussc::TcpSendCompleteEventArgs::bytesSent;
        t["clientId"] = &trussc::TcpSendCompleteEventArgs::clientId;
        t["error"] = &trussc::TcpSendCompleteEventArgs::error;
        t["sendId"] = &trussc::TcpSendCompleteEventArgs::sendId;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    lua->new_usertype<trussc::SoundBuffer::Kind>("SoundBufferKind",
        sol::meta_function::equal_to, [](trussc::SoundBuffer::Kind a, trussc::SoundBuffer::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundBuffer::Kind::Eager),
        "Stream", sol::var(trussc::SoundBuffer::Kind::Stream));
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
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
