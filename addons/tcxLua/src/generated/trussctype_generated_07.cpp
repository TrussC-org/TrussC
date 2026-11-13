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
        sol::usertype<trussc::AudioStats> t = lua->new_usertype<trussc::AudioStats>("AudioStats");
        t["clippedSamples"] = &trussc::AudioStats::clippedSamples;
        t["cpuUsage"] = &trussc::AudioStats::cpuUsage;
        t["cpuUsagePeak"] = &trussc::AudioStats::cpuUsagePeak;
        t["droppedDecoderError"] = &trussc::AudioStats::droppedDecoderError;
        t["droppedNotRunning"] = &trussc::AudioStats::droppedNotRunning;
        t["droppedPlays"] = &trussc::AudioStats::droppedPlays;
        t["droppedPolyphonyLimit"] = &trussc::AudioStats::droppedPolyphonyLimit;
        t["droppedStreamLimit"] = &trussc::AudioStats::droppedStreamLimit;
        t["initFailure"] = &trussc::AudioStats::initFailure;
        t["initFailureResult"] = &trussc::AudioStats::initFailureResult;
        t["peak"] = &trussc::AudioStats::peak;
        t["rms"] = &trussc::AudioStats::rms;
        t["stalled"] = &trussc::AudioStats::stalled;
        t["underrunFrames"] = &trussc::AudioStats::underrunFrames;
        t["voicesStoppedByReinit"] = &trussc::AudioStats::voicesStoppedByReinit;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::SoundStream
            , trussc::SoundStream()
        >();
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream", constructors, sol::call_constructor, constructors);
        t["channels"] = &trussc::SoundStream::channels;
        t["sampleRate"] = &trussc::SoundStream::sampleRate;
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["kind"] = &trussc::SoundStream::kind;
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
    }
    lua->new_usertype<trussc::Beep>("Beep",
        sol::meta_function::equal_to, [](trussc::Beep a, trussc::Beep b){ return a == b; },
        "ping", sol::var(trussc::Beep::ping),
        "success", sol::var(trussc::Beep::success),
        "complete", sol::var(trussc::Beep::complete),
        "coin", sol::var(trussc::Beep::coin),
        "error", sol::var(trussc::Beep::error),
        "warning", sol::var(trussc::Beep::warning),
        "cancel", sol::var(trussc::Beep::cancel),
        "click", sol::var(trussc::Beep::click),
        "typing", sol::var(trussc::Beep::typing),
        "notify", sol::var(trussc::Beep::notify),
        "sweep", sol::var(trussc::Beep::sweep));
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    lua->new_usertype<trussc::TextureWrap>("TextureWrap",
        sol::meta_function::equal_to, [](trussc::TextureWrap a, trussc::TextureWrap b){ return a == b; },
        "Repeat", sol::var(trussc::TextureWrap::Repeat),
        "ClampToEdge", sol::var(trussc::TextureWrap::ClampToEdge),
        "MirroredRepeat", sol::var(trussc::TextureWrap::MirroredRepeat));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    {
        sol::usertype<trussc::TcpServerClient> t = lua->new_usertype<trussc::TcpServerClient>("TcpServerClient");
        t["getHost"] = &trussc::TcpServerClient::getHost;
        t["getId"] = &trussc::TcpServerClient::getId;
        t["getPort"] = &trussc::TcpServerClient::getPort;
    }
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
        t["message"] = &trussc::UdpErrorEventArgs::message;
    }
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
