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
void tcxLuaGenShard_09(const std::shared_ptr<sol::state>& lua) {
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::RectNode>::user_metatable()];
        sol::usertype<trussc::RectNode> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::RectNode>>()
            : lua->new_usertype<trussc::RectNode>("RectNode");
        (*lua)["RectNode"] = t;
        t["mousePressed"] = &trussc::RectNode::mousePressed;
        t["mouseReleased"] = &trussc::RectNode::mouseReleased;
        t["mouseDragged"] = &trussc::RectNode::mouseDragged;
        t["mouseScrolled"] = &trussc::RectNode::mouseScrolled;
        t["localMatrixChanged"] = &trussc::RectNode::localMatrixChanged;
        t["getWidth"] = &trussc::RectNode::getWidth;
        t["getHeight"] = &trussc::RectNode::getHeight;
        t["getSize"] = &trussc::RectNode::getSize;
        t["setWidth"] = &trussc::RectNode::setWidth;
        t["setHeight"] = &trussc::RectNode::setHeight;
        t["setSize"] = sol::overload([](trussc::RectNode& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNode& self, float size) { return self.setSize(size); }, [](trussc::RectNode& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::RectNode::setRect;
        t["setClipping"] = &trussc::RectNode::setClipping;
        t["isClipping"] = &trussc::RectNode::isClipping;
        t["getLeft"] = &trussc::RectNode::getLeft;
        t["getRight"] = &trussc::RectNode::getRight;
        t["getTop"] = &trussc::RectNode::getTop;
        t["getBottom"] = &trussc::RectNode::getBottom;
        t["hitTest"] = [](trussc::RectNode& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["draw"] = &trussc::RectNode::draw;
        t["setup"] = &trussc::RectNode::setup;
        t["update"] = &trussc::RectNode::update;
        t["cleanup"] = &trussc::RectNode::cleanup;
        t["addChild"] = sol::overload([](trussc::RectNode& self, trussc::RectNode::Ptr child) { return self.addChild(child); }, [](trussc::RectNode& self, trussc::RectNode::Ptr child, bool keepGlobalPosition) { return self.addChild(child, keepGlobalPosition); });
        t["insertChild"] = sol::overload([](trussc::RectNode& self, size_t index, trussc::RectNode::Ptr child) { return self.insertChild(index, child); }, [](trussc::RectNode& self, size_t index, trussc::RectNode::Ptr child, bool keepGlobalPosition) { return self.insertChild(index, child, keepGlobalPosition); });
        t["removeChild"] = &trussc::RectNode::removeChild;
        t["removeAllChildren"] = &trussc::RectNode::removeAllChildren;
        t["onChildAdded"] = &trussc::RectNode::onChildAdded;
        t["onChildRemoved"] = &trussc::RectNode::onChildRemoved;
        t["getParent"] = &trussc::RectNode::getParent;
        t["getChildren"] = &trussc::RectNode::getChildren;
        t["getChildCount"] = &trussc::RectNode::getChildCount;
        t["getChildIndex"] = &trussc::RectNode::getChildIndex;
        t["moveToFront"] = &trussc::RectNode::moveToFront;
        t["moveToBack"] = &trussc::RectNode::moveToBack;
        t["isActive"] = &trussc::RectNode::isActive;
        t["setActive"] = &trussc::RectNode::setActive;
        t["isVisible"] = &trussc::RectNode::isVisible;
        t["setVisible"] = &trussc::RectNode::setVisible;
        t["getActive"] = &trussc::RectNode::getActive;
        t["getVisible"] = &trussc::RectNode::getVisible;
        t["setIsActive"] = &trussc::RectNode::setIsActive;
        t["setIsVisible"] = &trussc::RectNode::setIsVisible;
        t["destroy"] = &trussc::RectNode::destroy;
        t["isDead"] = &trussc::RectNode::isDead;
        t["enableEvents"] = &trussc::RectNode::enableEvents;
        t["disableEvents"] = &trussc::RectNode::disableEvents;
        t["isEventsEnabled"] = &trussc::RectNode::isEventsEnabled;
        t["isMouseOver"] = &trussc::RectNode::isMouseOver;
        t["setName"] = &trussc::RectNode::setName;
        t["getName"] = &trussc::RectNode::getName;
        t["hasName"] = &trussc::RectNode::hasName;
        t["getTypeName"] = &trussc::RectNode::getTypeName;
        t["getDisplayName"] = &trussc::RectNode::getDisplayName;
        t["getInstanceId"] = &trussc::RectNode::getInstanceId;
        t["findByInstanceId"] = &trussc::RectNode::findByInstanceId;
        t["getPos"] = &trussc::RectNode::getPos;
        t["getX"] = &trussc::RectNode::getX;
        t["getY"] = &trussc::RectNode::getY;
        t["getZ"] = &trussc::RectNode::getZ;
        t["setPos"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & pos) { return self.setPos(pos); }, [](trussc::RectNode& self, float x, float y) { return self.setPos(x, y); }, [](trussc::RectNode& self, float x, float y, float z) { return self.setPos(x, y, z); });
        t["setX"] = &trussc::RectNode::setX;
        t["setY"] = &trussc::RectNode::setY;
        t["setZ"] = &trussc::RectNode::setZ;
        t["getQuaternion"] = &trussc::RectNode::getQuaternion;
        t["setQuaternion"] = &trussc::RectNode::setQuaternion;
        t["getEuler"] = &trussc::RectNode::getEuler;
        t["setEuler"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & euler) { return self.setEuler(euler); }, [](trussc::RectNode& self, float pitch, float yaw, float roll) { return self.setEuler(pitch, yaw, roll); });
        t["getEulerDeg"] = &trussc::RectNode::getEulerDeg;
        t["setEulerDeg"] = &trussc::RectNode::setEulerDeg;
        t["getRot"] = &trussc::RectNode::getRot;
        t["setRot"] = &trussc::RectNode::setRot;
        t["getRotDeg"] = &trussc::RectNode::getRotDeg;
        t["setRotDeg"] = &trussc::RectNode::setRotDeg;
        t["getScale"] = &trussc::RectNode::getScale;
        t["getScaleX"] = &trussc::RectNode::getScaleX;
        t["getScaleY"] = &trussc::RectNode::getScaleY;
        t["getScaleZ"] = &trussc::RectNode::getScaleZ;
        t["setScale"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & s) { return self.setScale(s); }, [](trussc::RectNode& self, float uniform) { return self.setScale(uniform); }, [](trussc::RectNode& self, float sx, float sy) { return self.setScale(sx, sy); }, [](trussc::RectNode& self, float sx, float sy, float sz) { return self.setScale(sx, sy, sz); });
        t["setScaleX"] = &trussc::RectNode::setScaleX;
        t["setScaleY"] = &trussc::RectNode::setScaleY;
        t["setScaleZ"] = &trussc::RectNode::setScaleZ;
        t["getLocalMatrix"] = &trussc::RectNode::getLocalMatrix;
        t["getGlobalMatrix"] = &trussc::RectNode::getGlobalMatrix;
        t["getGlobalMatrixInverse"] = &trussc::RectNode::getGlobalMatrixInverse;
        t["globalToLocal"] = [](trussc::RectNode& self, const trussc::Vec3 & global) { return self.globalToLocal(global); };
        t["getGlobalPos"] = &trussc::RectNode::getGlobalPos;
        t["setGlobalPos"] = sol::overload([](trussc::RectNode& self, const trussc::Vec3 & global) { return self.setGlobalPos(global); }, [](trussc::RectNode& self, float x, float y) { return self.setGlobalPos(x, y); }, [](trussc::RectNode& self, float x, float y, float z) { return self.setGlobalPos(x, y, z); });
        t["localToGlobal"] = [](trussc::RectNode& self, const trussc::Vec3 & local) { return self.localToGlobal(local); };
        t["getMouseX"] = &trussc::RectNode::getMouseX;
        t["getMouseY"] = &trussc::RectNode::getMouseY;
        t["getPMouseX"] = &trussc::RectNode::getPMouseX;
        t["getPMouseY"] = &trussc::RectNode::getPMouseY;
        t["findHitNode"] = &trussc::RectNode::findHitNode;
        t["findHitNodeFromScreen"] = &trussc::RectNode::findHitNodeFromScreen;
        t["getCameraContext"] = &trussc::RectNode::getCameraContext;
        t["setCameraContext"] = &trussc::RectNode::setCameraContext;
        t["getModTypeNames"] = &trussc::RectNode::getModTypeNames;
        t["getMods"] = &trussc::RectNode::getMods;
        t["getModByTypeName"] = &trussc::RectNode::getModByTypeName;
        t["callAfter"] = &trussc::RectNode::callAfter;
        t["callEvery"] = &trussc::RectNode::callEvery;
        t["callEveryCatchUp"] = &trussc::RectNode::callEveryCatchUp;
        t["cancelTimer"] = &trussc::RectNode::cancelTimer;
        t["cancelAllTimers"] = &trussc::RectNode::cancelAllTimers;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Vec2
            , trussc::Vec2()
            , trussc::Vec2(float, float)
            , trussc::Vec2(float)
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::Vec2>::user_metatable()];
        sol::usertype<trussc::Vec2> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::Vec2>>()
            : lua->new_usertype<trussc::Vec2>("Vec2", constructors, sol::call_constructor, constructors);
        (*lua)["Vec2"] = t;
        t[sol::meta_function::index] = [](const trussc::Vec2& a, int b){ return a[b]; };
        t[sol::meta_function::addition] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec2& a){ return -a; };
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec2& a, float b){ return a * b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a * b; });
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec2& a, float b){ return a / b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a == b; };
        t["x"] = &trussc::Vec2::x;
        t["y"] = &trussc::Vec2::y;
        t["set"] = sol::overload([](trussc::Vec2& self, float x_, float y_) -> decltype(auto) { return self.set(x_, y_); }, [](trussc::Vec2& self, const trussc::Vec2 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec2::length;
        t["lengthSquared"] = &trussc::Vec2::lengthSquared;
        t["normalized"] = &trussc::Vec2::normalized;
        t["normalize"] = &trussc::Vec2::normalize;
        t["limit"] = &trussc::Vec2::limit;
        t["dot"] = &trussc::Vec2::dot;
        t["cross"] = &trussc::Vec2::cross;
        t["distance"] = &trussc::Vec2::distance;
        t["distanceSquared"] = &trussc::Vec2::distanceSquared;
        t["angle"] = sol::overload([](trussc::Vec2& self) { return self.angle(); }, [](trussc::Vec2& self, const trussc::Vec2 & v) { return self.angle(v); });
        t["rotated"] = &trussc::Vec2::rotated;
        t["rotate"] = &trussc::Vec2::rotate;
        t["lerp"] = &trussc::Vec2::lerp;
        t["perpendicular"] = &trussc::Vec2::perpendicular;
        t["reflected"] = &trussc::Vec2::reflected;
        t["fromAngle"] = sol::overload([](float radians) { return trussc::Vec2::fromAngle(radians); }, [](float radians, float length) { return trussc::Vec2::fromAngle(radians, length); });
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::HasTexture>::user_metatable()];
        sol::usertype<trussc::HasTexture> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::HasTexture>>()
            : lua->new_usertype<trussc::HasTexture>("HasTexture");
        (*lua)["HasTexture"] = t;
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["save"] = &trussc::HasTexture::save;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::AudioRecorder
            , trussc::AudioRecorder()
        >();
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::AudioRecorder>::user_metatable()];
        sol::usertype<trussc::AudioRecorder> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::AudioRecorder>>()
            : lua->new_usertype<trussc::AudioRecorder>("AudioRecorder", constructors, sol::call_constructor, constructors);
        (*lua)["AudioRecorder"] = t;
        t["start"] = sol::overload([](trussc::AudioRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::AudioRecorder& self, const fs::path & path, const trussc::AudioRecordSettings & settings) { return self.start(path, settings); });
        t["stop"] = &trussc::AudioRecorder::stop;
        t["isRecording"] = &trussc::AudioRecorder::isRecording;
        t["getRecordedSeconds"] = &trussc::AudioRecorder::getRecordedSeconds;
        t["getDroppedFrames"] = &trussc::AudioRecorder::getDroppedFrames;
        t["getPath"] = &trussc::AudioRecorder::getPath;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::GraphicsBackend>::user_metatable()];
        sol::usertype<trussc::GraphicsBackend> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::GraphicsBackend>>()
            : lua->new_usertype<trussc::GraphicsBackend>("GraphicsBackend");
        (*lua)["GraphicsBackend"] = t;
        t["isWebGPU"] = &trussc::GraphicsBackend::isWebGPU;
        t["isWebGL2"] = &trussc::GraphicsBackend::isWebGL2;
        t["isMetal"] = &trussc::GraphicsBackend::isMetal;
        t["isD3D11"] = &trussc::GraphicsBackend::isD3D11;
        t["isVulkan"] = &trussc::GraphicsBackend::isVulkan;
        t["isOpenGL"] = &trussc::GraphicsBackend::isOpenGL;
        t["name"] = &trussc::GraphicsBackend::name;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ShaderVertex>::user_metatable()];
        sol::usertype<trussc::ShaderVertex> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ShaderVertex>>()
            : lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        (*lua)["ShaderVertex"] = t;
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["r"] = &trussc::ShaderVertex::r;
        t["g"] = &trussc::ShaderVertex::g;
        t["b"] = &trussc::ShaderVertex::b;
        t["a"] = &trussc::ShaderVertex::a;
    }
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
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::ChipSoundBundle::Entry>::user_metatable()];
        sol::usertype<trussc::ChipSoundBundle::Entry> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::ChipSoundBundle::Entry>>()
            : lua->new_usertype<trussc::ChipSoundBundle::Entry>("ChipSoundBundleEntry");
        (*lua)["ChipSoundBundleEntry"] = t;
        t["note"] = &trussc::ChipSoundBundle::Entry::note;
        t["time"] = &trussc::ChipSoundBundle::Entry::time;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::GrabberFrame>::user_metatable()];
        sol::usertype<trussc::GrabberFrame> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::GrabberFrame>>()
            : lua->new_usertype<trussc::GrabberFrame>("GrabberFrame");
        (*lua)["GrabberFrame"] = t;
        t["pixels"] = &trussc::GrabberFrame::pixels;
        t["timestampUs"] = &trussc::GrabberFrame::timestampUs;
    }
    {
        sol::object registered = lua->registry()[sol::usertype_traits<trussc::App::HitResult>::user_metatable()];
        sol::usertype<trussc::App::HitResult> t = registered.get_type() == sol::type::table
            ? registered.as<sol::usertype<trussc::App::HitResult>>()
            : lua->new_usertype<trussc::App::HitResult>("AppHitResult");
        (*lua)["AppHitResult"] = t;
    }
    lua->new_usertype<trussc::LayoutDirection>("LayoutDirection",
        sol::meta_function::equal_to, [](trussc::LayoutDirection a, trussc::LayoutDirection b){ return a == b; },
        "Vertical", sol::var(trussc::LayoutDirection::Vertical),
        "Horizontal", sol::var(trussc::LayoutDirection::Horizontal));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
