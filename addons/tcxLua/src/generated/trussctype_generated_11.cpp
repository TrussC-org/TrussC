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
void tcxLuaGenShard_11(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::Node
            , trussc::Node()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Node>::user_metatable()];
        sol::usertype<trussc::Node> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Node>>()
            : lua->new_usertype<trussc::Node>("Node", constructors, sol::call_constructor, constructors);
        (*lua)["Node"] = t;
        t["localMatrixChanged"] = &trussc::Node::localMatrixChanged;
        t["setup"] = &trussc::Node::setup;
        t["update"] = &trussc::Node::update;
        t["draw"] = &trussc::Node::draw;
        t["cleanup"] = &trussc::Node::cleanup;
        t["addChild"] = sol::overload([](trussc::Node& self, trussc::Node::Ptr child) { return self.addChild(child); }, [](trussc::Node& self, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::Node& self, size_t index, trussc::Node::Ptr child) { return self.insertChild(index, child); }, [](trussc::Node& self, size_t index, trussc::Node::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::Node::removeChild;
        t["removeAllChildren"] = &trussc::Node::removeAllChildren;
        t["onChildAdded"] = &trussc::Node::onChildAdded;
        t["onChildRemoved"] = &trussc::Node::onChildRemoved;
        t["getParent"] = &trussc::Node::getParent;
        t["getChildren"] = &trussc::Node::getChildren;
        t["getChildCount"] = &trussc::Node::getChildCount;
        t["getChildIndex"] = &trussc::Node::getChildIndex;
        t["moveToFront"] = &trussc::Node::moveToFront;
        t["moveToBack"] = &trussc::Node::moveToBack;
        t["isActive"] = &trussc::Node::isActive;
        t["setActive"] = &trussc::Node::setActive;
        t["isVisible"] = &trussc::Node::isVisible;
        t["setVisible"] = &trussc::Node::setVisible;
        t["getActive"] = &trussc::Node::getActive;
        t["getVisible"] = &trussc::Node::getVisible;
        t["setIsActive"] = &trussc::Node::setIsActive;
        t["setIsVisible"] = &trussc::Node::setIsVisible;
        t["destroy"] = &trussc::Node::destroy;
        t["isDead"] = &trussc::Node::isDead;
        t["enableEvents"] = &trussc::Node::enableEvents;
        t["disableEvents"] = &trussc::Node::disableEvents;
        t["isEventsEnabled"] = &trussc::Node::isEventsEnabled;
        t["isMouseOver"] = &trussc::Node::isMouseOver;
        t["setName"] = &trussc::Node::setName;
        t["getName"] = &trussc::Node::getName;
        t["hasName"] = &trussc::Node::hasName;
        t["getTypeName"] = &trussc::Node::getTypeName;
        t["getDisplayName"] = &trussc::Node::getDisplayName;
        t["getInstanceId"] = &trussc::Node::getInstanceId;
        t["findByInstanceId"] = &trussc::Node::findByInstanceId;
        t["getPos"] = &trussc::Node::getPos;
        t["getX"] = &trussc::Node::getX;
        t["getY"] = &trussc::Node::getY;
        t["getZ"] = &trussc::Node::getZ;
        t["setPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::Node& self, float x, float y) { return self.setPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::Node::setX;
        t["setY"] = &trussc::Node::setY;
        t["setZ"] = &trussc::Node::setZ;
        t["getQuaternion"] = &trussc::Node::getQuaternion;
        t["setQuaternion"] = &trussc::Node::setQuaternion;
        t["getEuler"] = &trussc::Node::getEuler;
        t["setEuler"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::Node& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::Node::getEulerDeg;
        t["setEulerDeg"] = &trussc::Node::setEulerDeg;
        t["getRot"] = &trussc::Node::getRot;
        t["setRot"] = &trussc::Node::setRot;
        t["getRotDeg"] = &trussc::Node::getRotDeg;
        t["setRotDeg"] = &trussc::Node::setRotDeg;
        t["getScale"] = &trussc::Node::getScale;
        t["getScaleX"] = &trussc::Node::getScaleX;
        t["getScaleY"] = &trussc::Node::getScaleY;
        t["getScaleZ"] = &trussc::Node::getScaleZ;
        t["setScale"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::Node& self, float uniform) { return self.setScale(uniform); }, [](trussc::Node& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::Node& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::Node::setScaleX;
        t["setScaleY"] = &trussc::Node::setScaleY;
        t["setScaleZ"] = &trussc::Node::setScaleZ;
        t["getLocalMatrix"] = &trussc::Node::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::Node::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::Node::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::Node& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::Node::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::Node& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::Node& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::Node& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::Node& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::Node::getMouseX;
        t["getMouseY"] = &trussc::Node::getMouseY;
        t["getPMouseX"] = &trussc::Node::getPMouseX;
        t["getPMouseY"] = &trussc::Node::getPMouseY;
        t["findHitNode"] = &trussc::Node::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::Node::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::Node::getCameraContext;
        t["setCameraContext"] = &trussc::Node::setCameraContext;
        t["getModTypeNames"] = &trussc::Node::getModTypeNames;
        t["getMods"] = &trussc::Node::getMods;
        t["getModByTypeName"] = &trussc::Node::getModByTypeName;
        t["callAfter"] = &trussc::Node::callAfter;
        t["callEvery"] = &trussc::Node::callEvery;
        t["callEveryCatchUp"] = &trussc::Node::callEveryCatchUp;
        t["cancelTimer"] = &trussc::Node::cancelTimer;
        t["cancelAllTimers"] = &trussc::Node::cancelAllTimers;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::ScreenRecorder
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::ScreenRecorder()
#endif
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ScreenRecorder>::user_metatable()];
        sol::usertype<trussc::ScreenRecorder> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ScreenRecorder>>()
            : lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder", constructors, sol::call_constructor, constructors);
        (*lua)["ScreenRecorder"] = t;
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stop"] = &trussc::ScreenRecorder::stop;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::ScreenRecorder::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["writer"] = &trussc::ScreenRecorder::writer;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::Quaternion
            , trussc::Quaternion()
            , trussc::Quaternion(float, float, float, float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Quaternion>::user_metatable()];
        sol::usertype<trussc::Quaternion> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Quaternion>>()
            : lua->new_usertype<trussc::Quaternion>("Quaternion", constructors, sol::call_constructor, constructors);
        (*lua)["Quaternion"] = t;
        t[sol::meta_function::equal_to] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; };
        t[sol::meta_function::multiplication] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; };
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["identity"] = &trussc::Quaternion::identity;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["slerp"] = &trussc::Quaternion::slerp;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ChipSoundBundle>::user_metatable()];
        sol::usertype<trussc::ChipSoundBundle> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ChipSoundBundle>>()
            : lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        (*lua)["ChipSoundBundle"] = t;
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
        t["build"] = &trussc::ChipSoundBundle::build;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::PlayingSound>::user_metatable()];
        sol::usertype<trussc::PlayingSound> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::PlayingSound>>()
            : lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        (*lua)["PlayingSound"] = t;
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["volume"] = &trussc::PlayingSound::volume;
        t["pan"] = &trussc::PlayingSound::pan;
        t["speed"] = &trussc::PlayingSound::speed;
        t["loop"] = &trussc::PlayingSound::loop;
        t["playing"] = &trussc::PlayingSound::playing;
        t["paused"] = &trussc::PlayingSound::paused;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
        t["level"] = &trussc::PlayingSound::level;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::TouchEventArgs>::user_metatable()];
        sol::usertype<trussc::TouchEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::TouchEventArgs>>()
            : lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        (*lua)["TouchEventArgs"] = t;
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
        t["id"] = &trussc::TouchEventArgs::id;
    }
    lua->new_usertype<trussc::PrimitiveMode>("PrimitiveMode",
        sol::meta_function::equal_to, [](trussc::PrimitiveMode a, trussc::PrimitiveMode b){ return a == b; },
        "Triangles", sol::var(trussc::PrimitiveMode::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveMode::TriangleStrip),
        "TriangleFan", sol::var(trussc::PrimitiveMode::TriangleFan),
        "Lines", sol::var(trussc::PrimitiveMode::Lines),
        "LineStrip", sol::var(trussc::PrimitiveMode::LineStrip),
        "LineLoop", sol::var(trussc::PrimitiveMode::LineLoop),
        "Points", sol::var(trussc::PrimitiveMode::Points));
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::UdpErrorEventArgs>::user_metatable()];
        sol::usertype<trussc::UdpErrorEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::UdpErrorEventArgs>>()
            : lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        (*lua)["UdpErrorEventArgs"] = t;
        t["message"] = &trussc::UdpErrorEventArgs::message;
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ResizeEventArgs>::user_metatable()];
        sol::usertype<trussc::ResizeEventArgs> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ResizeEventArgs>>()
            : lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        (*lua)["ResizeEventArgs"] = t;
        t["width"] = &trussc::ResizeEventArgs::width;
        t["height"] = &trussc::ResizeEventArgs::height;
    }
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    lua->new_usertype<trussc::TcyMode>("TcyMode",
        sol::meta_function::equal_to, [](trussc::TcyMode a, trussc::TcyMode b){ return a == b; },
        "Rotate", sol::var(trussc::TcyMode::Rotate),
        "Upright", sol::var(trussc::TcyMode::Upright),
        "Combine", sol::var(trussc::TcyMode::Combine));
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
