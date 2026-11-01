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
        t["localMatrixChanged"] = &trussc::ScrollBar::localMatrixChanged;
        t["mouseDragged"] = &trussc::ScrollBar::mouseDragged;
        t["mousePressed"] = &trussc::ScrollBar::mousePressed;
        t["mouseReleased"] = &trussc::ScrollBar::mouseReleased;
        t["mouseScrolled"] = &trussc::ScrollBar::mouseScrolled;
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
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["clipboardSize"] = &trussc::WindowSettings::clipboardSize;
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["height"] = &trussc::WindowSettings::height;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["pipelinePoolSize"] = &trussc::WindowSettings::pipelinePoolSize;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["quitOnClose"] = &trussc::WindowSettings::quitOnClose;
        t["resourcePoolSize"] = &trussc::WindowSettings::resourcePoolSize;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["shaderPoolSize"] = &trussc::WindowSettings::shaderPoolSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["title"] = &trussc::WindowSettings::title;
        t["uniformBufferReserve"] = &trussc::WindowSettings::uniformBufferReserve;
        t["width"] = &trussc::WindowSettings::width;
        t["reserveUniformBuffer"] = &trussc::WindowSettings::reserveUniformBuffer;
        t["setClipboardSize"] = &trussc::WindowSettings::setClipboardSize;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setPipelinePoolSize"] = &trussc::WindowSettings::setPipelinePoolSize;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setQuitOnClose"] = &trussc::WindowSettings::setQuitOnClose;
        t["setResourcePoolSize"] = &trussc::WindowSettings::setResourcePoolSize;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setShaderPoolSize"] = &trussc::WindowSettings::setShaderPoolSize;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::FileWriter
            , trussc::FileWriter()
        >();
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter", constructors, sol::call_constructor, constructors);
        t["close"] = &trussc::FileWriter::close;
        t["flush"] = &trussc::FileWriter::flush;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["open"] = sol::overload([](trussc::FileWriter& self, const fs::path & path) { return self.open(path); }, [](trussc::FileWriter& self, const fs::path & path, bool append) { return self.open(path, append); });
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
    }
    {
        auto constructors = tcxLuaConstructors<trussc::ColorOKLab
            , trussc::ColorOKLab()
            , trussc::ColorOKLab(float, float, float)
            , trussc::ColorOKLab(float, float, float, float)
        >();
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab", constructors, sol::call_constructor, constructors);
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["b"] = &trussc::ColorOKLab::b;
        t["lerp"] = &trussc::ColorOKLab::lerp;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
    }
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning),
        "AllocationFailed", sol::var(trussc::SendError::AllocationFailed));
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    {
        sol::usertype<trussc::SerialDisconnectEventArgs> t = lua->new_usertype<trussc::SerialDisconnectEventArgs>("SerialDisconnectEventArgs");
        t["baudRate"] = &trussc::SerialDisconnectEventArgs::baudRate;
        t["portName"] = &trussc::SerialDisconnectEventArgs::portName;
        t["reason"] = &trussc::SerialDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::SerialDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::TcpClientConnectEventArgs> t = lua->new_usertype<trussc::TcpClientConnectEventArgs>("TcpClientConnectEventArgs");
        t["clientId"] = &trussc::TcpClientConnectEventArgs::clientId;
        t["host"] = &trussc::TcpClientConnectEventArgs::host;
        t["port"] = &trussc::TcpClientConnectEventArgs::port;
    }
    lua->new_usertype<trussc::StrokeCap>("StrokeCap",
        sol::meta_function::equal_to, [](trussc::StrokeCap a, trussc::StrokeCap b){ return a == b; },
        "Butt", sol::var(trussc::StrokeCap::Butt),
        "Round", sol::var(trussc::StrokeCap::Round),
        "Square", sol::var(trussc::StrokeCap::Square));
    lua->new_usertype<trussc::AudioBackend>("AudioBackend",
        sol::meta_function::equal_to, [](trussc::AudioBackend a, trussc::AudioBackend b){ return a == b; },
        "Default", sol::var(trussc::AudioBackend::Default),
        "Null", sol::var(trussc::AudioBackend::Null));
    {
        sol::usertype<trussc::MicStartResult> t = lua->new_usertype<trussc::MicStartResult>("MicStartResult");
        t["message"] = &trussc::MicStartResult::message;
        t["success"] = &trussc::MicStartResult::success;
    }
    {
        sol::usertype<trussc::TcpReceiveEventArgs> t = lua->new_usertype<trussc::TcpReceiveEventArgs>("TcpReceiveEventArgs");
        t["data"] = &trussc::TcpReceiveEventArgs::data;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
