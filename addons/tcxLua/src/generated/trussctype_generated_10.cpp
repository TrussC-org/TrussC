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
void tcxLuaGenShard_10(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        auto constructors = tcxLuaConstructors<trussc::TcpServer
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
            , trussc::TcpServer()
#endif
        >();
        sol::usertype<trussc::TcpServer> t = lua->new_usertype<trussc::TcpServer>("TcpServer", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onClientConnect"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onClientConnect);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onClientDisconnect"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onClientDisconnect);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onError"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onError);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onReceive"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onReceive);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onSendComplete"] = tcx_lua_detail::eventProperty<trussc::TcpServer>(&trussc::TcpServer::onSendComplete);
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["broadcast"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcast(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcast(message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["broadcastAsync"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcastAsync(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcastAsync(message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["disconnectAllClients"] = &trussc::TcpServer::disconnectAllClients;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["disconnectClient"] = &trussc::TcpServer::disconnectClient;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClient"] = &trussc::TcpServer::getClient;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClientCount"] = &trussc::TcpServer::getClientCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getClientIds"] = &trussc::TcpServer::getClientIds;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getPort"] = &trussc::TcpServer::getPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getSendAsyncBufferSize"] = &trussc::TcpServer::getSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getSendAsyncPendingBytes"] = &trussc::TcpServer::getSendAsyncPendingBytes;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["isRunning"] = &trussc::TcpServer::isRunning;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["send"] = sol::overload([](trussc::TcpServer& self, int clientId, const std::vector<char> & data) { return self.send(clientId, data); }, [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.send(clientId, message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["sendAsync"] = [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.sendAsync(clientId, message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setBindAddress"] = &trussc::TcpServer::setBindAddress;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setReceiveBufferSize"] = &trussc::TcpServer::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setSendAsyncBufferSize"] = &trussc::TcpServer::setSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setSendTimeout"] = &trussc::TcpServer::setSendTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["start"] = sol::overload([](trussc::TcpServer& self, int port) { return self.start(port); }, [](trussc::TcpServer& self, int port, int maxClients) { return self.start(port, maxClients); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["stop"] = &trussc::TcpServer::stop;
#endif
    }
#endif
    {
        sol::usertype<trussc::CoreEvents> t = lua->new_usertype<trussc::CoreEvents>("CoreEvents");
        t["afterFrame"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::afterFrame);
        t["clipboardPasted"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::clipboardPasted);
        t["closeRequested"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::closeRequested);
        t["deviceLost"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::deviceLost);
        t["draw"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::draw);
        t["exit"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::exit);
        t["exitRequested"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::exitRequested);
        t["filesDropped"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::filesDropped);
        t["focusChanged"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::focusChanged);
        t["hotReloadUnload"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::hotReloadUnload);
        t["keyPressed"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::keyPressed);
        t["keyReleased"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::keyReleased);
        t["mouseDragged"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::mouseDragged);
        t["mouseMoved"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::mouseMoved);
        t["mousePressed"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::mousePressed);
        t["mouseReleased"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::mouseReleased);
        t["mouseScrolled"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::mouseScrolled);
        t["occludedChanged"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::occludedChanged);
        t["onRender"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::onRender);
        t["rawEvent"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::rawEvent);
        t["touchMoved"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::touchMoved);
        t["touchPressed"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::touchPressed);
        t["touchReleased"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::touchReleased);
        t["update"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::update);
        t["windowResized"] = tcx_lua_detail::eventProperty<trussc::CoreEvents>(&trussc::CoreEvents::windowResized);
    }
    {
        auto constructors = tcxLuaConstructors<trussc::Quaternion
            , trussc::Quaternion()
            , trussc::Quaternion(float, float, float, float)
        >();
        sol::usertype<trussc::Quaternion> t = lua->new_usertype<trussc::Quaternion>("Quaternion", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a * b; };
        t[sol::meta_function::equal_to] = [](const trussc::Quaternion& a, const trussc::Quaternion & b){ return a == b; };
        t["w"] = &trussc::Quaternion::w;
        t["x"] = &trussc::Quaternion::x;
        t["y"] = &trussc::Quaternion::y;
        t["z"] = &trussc::Quaternion::z;
        t["conjugate"] = &trussc::Quaternion::conjugate;
        t["length"] = &trussc::Quaternion::length;
        t["lengthSquared"] = &trussc::Quaternion::lengthSquared;
        t["normalize"] = &trussc::Quaternion::normalize;
        t["normalized"] = &trussc::Quaternion::normalized;
        t["rotate"] = &trussc::Quaternion::rotate;
        t["toEuler"] = &trussc::Quaternion::toEuler;
        t["toMatrix"] = &trussc::Quaternion::toMatrix;
        t["fromAxisAngle"] = &trussc::Quaternion::fromAxisAngle;
        t["fromEuler"] = sol::overload([](float pitch, float yaw, float roll) { return trussc::Quaternion::fromEuler(pitch, yaw, roll); }, [](const trussc::Vec3 & euler) { return trussc::Quaternion::fromEuler(euler); });
        t["identity"] = &trussc::Quaternion::identity;
        t["slerp"] = &trussc::Quaternion::slerp;
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
        sol::usertype<trussc::RuntimeEvents> t = lua->new_usertype<trussc::RuntimeEvents>("RuntimeEvents");
        t["console"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::console);
        t["deviceLost"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::deviceLost);
        t["exitRequested"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::exitRequested);
        t["filesOpened"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::filesOpened);
        t["hotReloadUnload"] = tcx_lua_detail::eventProperty<trussc::RuntimeEvents>(&trussc::RuntimeEvents::hotReloadUnload);
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
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["key"] = &trussc::KeyEventArgs::key;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["super"] = &trussc::KeyEventArgs::super;
    }
    lua->new_usertype<trussc::MouseButton>("MouseButton",
        sol::meta_function::equal_to, [](trussc::MouseButton a, trussc::MouseButton b){ return a == b; },
        "Left", sol::var(trussc::MouseButton::Left),
        "Right", sol::var(trussc::MouseButton::Right),
        "Middle", sol::var(trussc::MouseButton::Middle),
        "None", sol::var(trussc::MouseButton::None));
    lua->new_usertype<trussc::CurveStyle::Mode>("CurveStyleMode",
        sol::meta_function::equal_to, [](trussc::CurveStyle::Mode a, trussc::CurveStyle::Mode b){ return a == b; },
        "Tolerance", sol::var(trussc::CurveStyle::Mode::Tolerance),
        "Resolution", sol::var(trussc::CurveStyle::Mode::Resolution));
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
        t["reason"] = &trussc::ExitRequestEventArgs::reason;
    }
    {
        sol::usertype<trussc::DeviceDisconnectEventArgs> t = lua->new_usertype<trussc::DeviceDisconnectEventArgs>("DeviceDisconnectEventArgs");
        t["reason"] = &trussc::DeviceDisconnectEventArgs::reason;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
