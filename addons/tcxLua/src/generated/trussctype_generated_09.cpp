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
        t["localMatrixChanged"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::localMatrixChanged);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseDragged);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::RectNode>(&trussc::RectNode::mouseScrolled);
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
        auto constructors = tcxLuaConstructors<trussc::Sound
            , trussc::Sound()
        >();
        sol::usertype<trussc::Sound> t = lua->new_usertype<trussc::Sound>("Sound", constructors, sol::call_constructor, constructors);
        t["clearChannelGains"] = &trussc::Sound::clearChannelGains;
        t["clearChannelMap"] = &trussc::Sound::clearChannelMap;
        t["getChannelGains"] = &trussc::Sound::getChannelGains;
        t["getChannelMap"] = &trussc::Sound::getChannelMap;
        t["getDuration"] = &trussc::Sound::getDuration;
        t["getMixMode"] = &trussc::Sound::getMixMode;
        t["getPan"] = &trussc::Sound::getPan;
        t["getPosition"] = &trussc::Sound::getPosition;
        t["getSpeed"] = &trussc::Sound::getSpeed;
        t["getVolume"] = &trussc::Sound::getVolume;
        t["isLoaded"] = &trussc::Sound::isLoaded;
        t["isLoop"] = &trussc::Sound::isLoop;
        t["isPaused"] = &trussc::Sound::isPaused;
        t["isPlaying"] = &trussc::Sound::isPlaying;
        t["isStreaming"] = &trussc::Sound::isStreaming;
        t["load"] = &trussc::Sound::load;
        t["loadFromBuffer"] = sol::overload([](trussc::Sound& self, const trussc::SoundBuffer & buf) { return self.loadFromBuffer(buf); }, [](trussc::Sound& self, std::shared_ptr<trussc::SoundBuffer> buf) { return self.loadFromBuffer(buf); });
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["loadStream"] = sol::overload([](trussc::Sound& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::Sound& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
#endif
        t["loadTestTone"] = sol::overload([](trussc::Sound& self) { return self.loadTestTone(); }, [](trussc::Sound& self, float frequency) { return self.loadTestTone(frequency); }, [](trussc::Sound& self, float frequency, float duration) { return self.loadTestTone(frequency, duration); });
        t["pause"] = &trussc::Sound::pause;
        t["play"] = &trussc::Sound::play;
        t["resume"] = &trussc::Sound::resume;
        t["setChannelGains"] = &trussc::Sound::setChannelGains;
        t["setChannelMap"] = sol::overload([](trussc::Sound& self, const std::vector<int> & map) { return self.setChannelMap(map); }, [](trussc::Sound& self, std::vector<std::vector<int>> map) { return self.setChannelMap(map); });
        t["setLoop"] = &trussc::Sound::setLoop;
        t["setMixMode"] = &trussc::Sound::setMixMode;
        t["setPan"] = &trussc::Sound::setPan;
        t["setPosition"] = &trussc::Sound::setPosition;
        t["setSpeed"] = &trussc::Sound::setSpeed;
        t["setVolume"] = &trussc::Sound::setVolume;
        t["stop"] = &trussc::Sound::stop;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::IVec3
            , trussc::IVec3()
            , trussc::IVec3(int, int, int)
            , trussc::IVec3(int)
            , trussc::IVec3(const trussc::IVec2 &)
            , trussc::IVec3(const trussc::IVec2 &, int)
        >();
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::IVec3& a, int b){ return a * b; };
        t[sol::meta_function::addition] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::IVec3& a){ return -a; };
        t[sol::meta_function::equal_to] = [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; };
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
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
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["audio"] = &trussc::VideoRecordSettings::audio;
        t["audioBitrate"] = &trussc::VideoRecordSettings::audioBitrate;
        t["audioChannels"] = &trussc::VideoRecordSettings::audioChannels;
        t["audioSampleRate"] = &trussc::VideoRecordSettings::audioSampleRate;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["duration"] = &trussc::VideoRecordSettings::duration;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
    }
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled),
        "Custom", sol::var(trussc::BlendMode::Custom));
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["backend"] = &trussc::AudioDeviceInfo::backend;
        t["channels"] = &trussc::AudioDeviceInfo::channels;
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
        t["name"] = &trussc::AudioDeviceInfo::name;
        t["sampleRates"] = &trussc::AudioDeviceInfo::sampleRates;
        t["uniqueId"] = &trussc::AudioDeviceInfo::uniqueId;
    }
    lua->new_usertype<trussc::StrokeMesh::CapType>("StrokeMeshCapType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::CapType a, trussc::StrokeMesh::CapType b){ return a == b; },
        "CAP_BUTT", sol::var(trussc::StrokeMesh::CapType::CAP_BUTT),
        "CAP_ROUND", sol::var(trussc::StrokeMesh::CapType::CAP_ROUND),
        "CAP_SQUARE", sol::var(trussc::StrokeMesh::CapType::CAP_SQUARE));
    lua->new_usertype<trussc::PointStyle>("PointStyle",
        sol::meta_function::equal_to, [](trussc::PointStyle a, trussc::PointStyle b){ return a == b; },
        "Square", sol::var(trussc::PointStyle::Square),
        "Round", sol::var(trussc::PointStyle::Round),
        "Pixel", sol::var(trussc::PointStyle::Pixel));
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
        t["reason"] = &trussc::ExitRequestEventArgs::reason;
    }
    {
        sol::usertype<trussc::MicStartResult> t = lua->new_usertype<trussc::MicStartResult>("MicStartResult");
        t["message"] = &trussc::MicStartResult::message;
        t["success"] = &trussc::MicStartResult::success;
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
