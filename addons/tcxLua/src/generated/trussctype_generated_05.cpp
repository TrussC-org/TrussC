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
void tcxLuaGenShard_05(const std::shared_ptr<sol::state>& lua) {
    {
        auto constructors = tcxLuaConstructors<trussc::RectNodeButton
            , trussc::RectNodeButton()
        >();
        sol::usertype<trussc::RectNodeButton> t = lua->new_usertype<trussc::RectNodeButton>("RectNodeButton", constructors, sol::call_constructor, constructors);
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["localMatrixChanged"] = &trussc::RectNodeButton::localMatrixChanged;
        t["mouseDragged"] = &trussc::RectNodeButton::mouseDragged;
        t["mousePressed"] = &trussc::RectNodeButton::mousePressed;
        t["mouseReleased"] = &trussc::RectNodeButton::mouseReleased;
        t["mouseScrolled"] = &trussc::RectNodeButton::mouseScrolled;
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["addChild"] = sol::overload([](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child) { return self.addChild(child); }, [](trussc::RectNodeButton& self, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["callAfter"] = &trussc::RectNodeButton::callAfter;
        t["callEvery"] = &trussc::RectNodeButton::callEvery;
        t["callEveryCatchUp"] = &trussc::RectNodeButton::callEveryCatchUp;
        t["cancelAllTimers"] = &trussc::RectNodeButton::cancelAllTimers;
        t["cancelTimer"] = &trussc::RectNodeButton::cancelTimer;
        t["cleanup"] = &trussc::RectNodeButton::cleanup;
        t["destroy"] = &trussc::RectNodeButton::destroy;
        t["disableEvents"] = &trussc::RectNodeButton::disableEvents;
        t["draw"] = &trussc::RectNodeButton::draw;
        t["enableEvents"] = &trussc::RectNodeButton::enableEvents;
        t["findByInstanceId"] = &trussc::RectNodeButton::findByInstanceId;
        t["findHitNode"] = &trussc::RectNodeButton::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::RectNodeButton::findHitNodeFromScreen;
        t["getActive"] = &trussc::RectNodeButton::getActive;
        t["getBottom"] = &trussc::RectNodeButton::getBottom;
        t["getCameraContext"] = &trussc::RectNodeButton::getCameraContext;
        t["getChildCount"] = &trussc::RectNodeButton::getChildCount;
        t["getChildIndex"] = &trussc::RectNodeButton::getChildIndex;
        t["getChildren"] = &trussc::RectNodeButton::getChildren;
        t["getDisplayName"] = &trussc::RectNodeButton::getDisplayName;
        t["getEuler"] = &trussc::RectNodeButton::getEuler;
        t["getEulerDeg"] = &trussc::RectNodeButton::getEulerDeg;
        t["getGlobalMatrix"] = &trussc::RectNodeButton::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::RectNodeButton::getGlobalMatrixInverse;
        t["getGlobalPos"] = &trussc::RectNodeButton::getGlobalPos;
        t["getHeight"] = &trussc::RectNodeButton::getHeight;
        t["getInstanceId"] = &trussc::RectNodeButton::getInstanceId;
        t["getLeft"] = &trussc::RectNodeButton::getLeft;
        t["getLocalMatrix"] = &trussc::RectNodeButton::getLocalMatrix;
        t["getModByTypeName"] = &trussc::RectNodeButton::getModByTypeName;
        t["getModTypeNames"] = &trussc::RectNodeButton::getModTypeNames;
        t["getMods"] = &trussc::RectNodeButton::getMods;
        t["getMouseX"] = &trussc::RectNodeButton::getMouseX;
        t["getMouseY"] = &trussc::RectNodeButton::getMouseY;
        t["getName"] = &trussc::RectNodeButton::getName;
        t["getPMouseX"] = &trussc::RectNodeButton::getPMouseX;
        t["getPMouseY"] = &trussc::RectNodeButton::getPMouseY;
        t["getParent"] = &trussc::RectNodeButton::getParent;
        t["getPos"] = &trussc::RectNodeButton::getPos;
        t["getQuaternion"] = &trussc::RectNodeButton::getQuaternion;
        t["getRight"] = &trussc::RectNodeButton::getRight;
        t["getRot"] = &trussc::RectNodeButton::getRot;
        t["getRotDeg"] = &trussc::RectNodeButton::getRotDeg;
        t["getScale"] = &trussc::RectNodeButton::getScale;
        t["getScaleX"] = &trussc::RectNodeButton::getScaleX;
        t["getScaleY"] = &trussc::RectNodeButton::getScaleY;
        t["getScaleZ"] = &trussc::RectNodeButton::getScaleZ;
        t["getSize"] = &trussc::RectNodeButton::getSize;
        t["getTop"] = &trussc::RectNodeButton::getTop;
        t["getTypeName"] = &trussc::RectNodeButton::getTypeName;
        t["getVisible"] = &trussc::RectNodeButton::getVisible;
        t["getWidth"] = &trussc::RectNodeButton::getWidth;
        t["getX"] = &trussc::RectNodeButton::getX;
        t["getY"] = &trussc::RectNodeButton::getY;
        t["getZ"] = &trussc::RectNodeButton::getZ;
        t["globalToLocal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["hasName"] = &trussc::RectNodeButton::hasName;
        t["hitTest"] = [](trussc::RectNodeButton& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["insertChild"] = sol::overload([](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child) { return self.insertChild(index, child); }, [](trussc::RectNodeButton& self, size_t index, trussc::RectNodeButton::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["isActive"] = &trussc::RectNodeButton::isActive;
        t["isClipping"] = &trussc::RectNodeButton::isClipping;
        t["isDead"] = &trussc::RectNodeButton::isDead;
        t["isEventsEnabled"] = &trussc::RectNodeButton::isEventsEnabled;
        t["isMouseOver"] = &trussc::RectNodeButton::isMouseOver;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["isVisible"] = &trussc::RectNodeButton::isVisible;
        t["localToGlobal"] = [](trussc::RectNodeButton& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["moveToBack"] = &trussc::RectNodeButton::moveToBack;
        t["moveToFront"] = &trussc::RectNodeButton::moveToFront;
        t["onChildAdded"] = &trussc::RectNodeButton::onChildAdded;
        t["onChildRemoved"] = &trussc::RectNodeButton::onChildRemoved;
        t["removeAllChildren"] = &trussc::RectNodeButton::removeAllChildren;
        t["removeChild"] = &trussc::RectNodeButton::removeChild;
        t["setActive"] = &trussc::RectNodeButton::setActive;
        t["setCameraContext"] = &trussc::RectNodeButton::setCameraContext;
        t["setClipping"] = &trussc::RectNodeButton::setClipping;
        t["setEuler"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::RectNodeButton& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["setEulerDeg"] = &trussc::RectNodeButton::setEulerDeg;
        t["setGlobalPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["setHeight"] = &trussc::RectNodeButton::setHeight;
        t["setIsActive"] = &trussc::RectNodeButton::setIsActive;
        t["setIsVisible"] = &trussc::RectNodeButton::setIsVisible;
        t["setName"] = &trussc::RectNodeButton::setName;
        t["setPos"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::RectNodeButton& self, float x, float y) { return self.setPos(x, y); }, [](trussc::RectNodeButton& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setQuaternion"] = &trussc::RectNodeButton::setQuaternion;
        t["setRect"] = &trussc::RectNodeButton::setRect;
        t["setRot"] = &trussc::RectNodeButton::setRot;
        t["setRotDeg"] = &trussc::RectNodeButton::setRotDeg;
        t["setScale"] = sol::overload([](trussc::RectNodeButton& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::RectNodeButton& self, float uniform) { return self.setScale(uniform); }, [](trussc::RectNodeButton& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::RectNodeButton& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::RectNodeButton::setScaleX;
        t["setScaleY"] = &trussc::RectNodeButton::setScaleY;
        t["setScaleZ"] = &trussc::RectNodeButton::setScaleZ;
        t["setSize"] = sol::overload([](trussc::RectNodeButton& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNodeButton& self, float size) { return self.setSize(size); }, [](trussc::RectNodeButton& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setVisible"] = &trussc::RectNodeButton::setVisible;
        t["setWidth"] = &trussc::RectNodeButton::setWidth;
        t["setX"] = &trussc::RectNodeButton::setX;
        t["setY"] = &trussc::RectNodeButton::setY;
        t["setZ"] = &trussc::RectNodeButton::setZ;
        t["setup"] = &trussc::RectNodeButton::setup;
        t["update"] = &trussc::RectNodeButton::update;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LayoutMod
            , trussc::LayoutMod()
            , trussc::LayoutMod(trussc::LayoutDirection)
            , trussc::LayoutMod(trussc::LayoutDirection, float)
        >();
        sol::usertype<trussc::LayoutMod> t = lua->new_usertype<trussc::LayoutMod>("LayoutMod", constructors, sol::call_constructor, constructors);
        t["getCrossAxis"] = &trussc::LayoutMod::getCrossAxis;
        t["getDirection"] = &trussc::LayoutMod::getDirection;
        t["getMainAxis"] = &trussc::LayoutMod::getMainAxis;
        t["getOwner"] = [](trussc::LayoutMod& self) { return self.getOwner(); };
        t["getPaddingBottom"] = &trussc::LayoutMod::getPaddingBottom;
        t["getPaddingLeft"] = &trussc::LayoutMod::getPaddingLeft;
        t["getPaddingRight"] = &trussc::LayoutMod::getPaddingRight;
        t["getPaddingTop"] = &trussc::LayoutMod::getPaddingTop;
        t["getSpacing"] = &trussc::LayoutMod::getSpacing;
        t["setCrossAxis"] = &trussc::LayoutMod::setCrossAxis;
        t["setDirection"] = &trussc::LayoutMod::setDirection;
        t["setMainAxis"] = &trussc::LayoutMod::setMainAxis;
        t["setPadding"] = sol::overload([](trussc::LayoutMod& self, float padding) -> decltype(auto) { return self.setPadding(padding); }, [](trussc::LayoutMod& self, float vertical, float horizontal) -> decltype(auto) { return self.setPadding(vertical, horizontal); }, [](trussc::LayoutMod& self, float top, float right, float bottom, float left) -> decltype(auto) { return self.setPadding(top, right, bottom, left); });
        t["setPaddingBottom"] = &trussc::LayoutMod::setPaddingBottom;
        t["setPaddingLeft"] = &trussc::LayoutMod::setPaddingLeft;
        t["setPaddingRight"] = &trussc::LayoutMod::setPaddingRight;
        t["setPaddingTop"] = &trussc::LayoutMod::setPaddingTop;
        t["setSpacing"] = &trussc::LayoutMod::setSpacing;
        t["updateLayout"] = &trussc::LayoutMod::updateLayout;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::JsonReadReflector
            , trussc::JsonReadReflector(trussc::Json)
        >();
        sol::usertype<trussc::JsonReadReflector> t = lua->new_usertype<trussc::JsonReadReflector>("JsonReadReflector", constructors, sol::call_constructor, constructors);
        t["applied"] = &trussc::JsonReadReflector::applied;
        t["readOnly"] = &trussc::JsonReadReflector::readOnly;
        t["skipped"] = &trussc::JsonReadReflector::skipped;
        t["endGroup"] = &trussc::JsonReadReflector::endGroup;
        t["isDerived"] = &trussc::JsonReadReflector::isDerived;
        t["isReadOnly"] = &trussc::JsonReadReflector::isReadOnly;
        t["popDerived"] = &trussc::JsonReadReflector::popDerived;
        t["popReadOnly"] = &trussc::JsonReadReflector::popReadOnly;
        t["pushDerived"] = &trussc::JsonReadReflector::pushDerived;
        t["pushReadOnly"] = &trussc::JsonReadReflector::pushReadOnly;
        t["unknownKeys"] = &trussc::JsonReadReflector::unknownKeys;
    }
    lua->new_usertype<trussc::EaseType>("EaseType",
        sol::meta_function::equal_to, [](trussc::EaseType a, trussc::EaseType b){ return a == b; },
        "Linear", sol::var(trussc::EaseType::Linear),
        "Quad", sol::var(trussc::EaseType::Quad),
        "Cubic", sol::var(trussc::EaseType::Cubic),
        "Quart", sol::var(trussc::EaseType::Quart),
        "Quint", sol::var(trussc::EaseType::Quint),
        "Sine", sol::var(trussc::EaseType::Sine),
        "Expo", sol::var(trussc::EaseType::Expo),
        "Circ", sol::var(trussc::EaseType::Circ),
        "Back", sol::var(trussc::EaseType::Back),
        "Elastic", sol::var(trussc::EaseType::Elastic),
        "Bounce", sol::var(trussc::EaseType::Bounce),
        "Custom", sol::var(trussc::EaseType::Custom));
    lua->new_usertype<trussc::LoadError>("LoadError",
        sol::meta_function::equal_to, [](trussc::LoadError a, trussc::LoadError b){ return a == b; },
        "None", sol::var(trussc::LoadError::None),
        "FileNotFound", sol::var(trussc::LoadError::FileNotFound),
        "UnsupportedFormat", sol::var(trussc::LoadError::UnsupportedFormat),
        "DecodeFailed", sol::var(trussc::LoadError::DecodeFailed),
        "Unknown", sol::var(trussc::LoadError::Unknown),
        "TooLarge", sol::var(trussc::LoadError::TooLarge),
        "Cancelled", sol::var(trussc::LoadError::Cancelled));
    {
        auto constructors = tcxLuaConstructors<trussc::LogEventArgs
            , trussc::LogEventArgs(trussc::LogLevel, const std::string &)
        >();
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs", constructors, sol::call_constructor, constructors);
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    lua->new_usertype<trussc::AudioRecordSettings::SampleFormat>("AudioRecordSettingsSampleFormat",
        sol::meta_function::equal_to, [](trussc::AudioRecordSettings::SampleFormat a, trussc::AudioRecordSettings::SampleFormat b){ return a == b; },
        "S16", sol::var(trussc::AudioRecordSettings::SampleFormat::S16),
        "F32", sol::var(trussc::AudioRecordSettings::SampleFormat::F32));
    {
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["synced"] = &trussc::FpsSettings::synced;
        t["updateFps"] = &trussc::FpsSettings::updateFps;
    }
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::ChipSoundBundle::Entry> t = lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
