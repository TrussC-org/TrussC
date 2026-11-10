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
        t["onClientConnect"] = &trussc::TcpServer::onClientConnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onClientDisconnect"] = &trussc::TcpServer::onClientDisconnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onError"] = &trussc::TcpServer::onError;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onReceive"] = &trussc::TcpServer::onReceive;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onSendComplete"] = &trussc::TcpServer::onSendComplete;
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
        auto constructors = tcxLuaConstructors<trussc::Vec2
            , trussc::Vec2()
            , trussc::Vec2(float, float)
            , trussc::Vec2(float)
        >();
        sol::usertype<trussc::Vec2> t = lua->new_usertype<trussc::Vec2>("Vec2", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec2& a, float b){ return a * b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec2& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec2& a, float b){ return a / b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec2& a, int b){ return a[b]; };
        t["x"] = &trussc::Vec2::x;
        t["y"] = &trussc::Vec2::y;
        t["angle"] = sol::overload([](trussc::Vec2& self) { return self.angle(); }, [](trussc::Vec2& self, const trussc::Vec2 & v) { return self.angle(v); });
        t["cross"] = &trussc::Vec2::cross;
        t["distance"] = &trussc::Vec2::distance;
        t["distanceSquared"] = &trussc::Vec2::distanceSquared;
        t["dot"] = &trussc::Vec2::dot;
        t["length"] = &trussc::Vec2::length;
        t["lengthSquared"] = &trussc::Vec2::lengthSquared;
        t["lerp"] = &trussc::Vec2::lerp;
        t["limit"] = &trussc::Vec2::limit;
        t["normalize"] = &trussc::Vec2::normalize;
        t["normalized"] = &trussc::Vec2::normalized;
        t["perpendicular"] = &trussc::Vec2::perpendicular;
        t["reflected"] = &trussc::Vec2::reflected;
        t["rotate"] = &trussc::Vec2::rotate;
        t["rotated"] = &trussc::Vec2::rotated;
        t["set"] = sol::overload([](trussc::Vec2& self, float x_, float y_) -> decltype(auto) { return self.set(x_, y_); }, [](trussc::Vec2& self, const trussc::Vec2 & v) -> decltype(auto) { return self.set(v); });
        t["fromAngle"] = sol::overload([](float radians) { return trussc::Vec2::fromAngle(radians); }, [](float radians, float length) { return trussc::Vec2::fromAngle(radians, length); });
    }
    {
        sol::usertype<trussc::CoreEvents> t = lua->new_usertype<trussc::CoreEvents>("CoreEvents");
        t["afterFrame"] = &trussc::CoreEvents::afterFrame;
        t["clipboardPasted"] = &trussc::CoreEvents::clipboardPasted;
        t["closeRequested"] = &trussc::CoreEvents::closeRequested;
        t["deviceLost"] = &trussc::CoreEvents::deviceLost;
        t["draw"] = &trussc::CoreEvents::draw;
        t["exit"] = &trussc::CoreEvents::exit;
        t["exitRequested"] = &trussc::CoreEvents::exitRequested;
        t["filesDropped"] = &trussc::CoreEvents::filesDropped;
        t["focusChanged"] = &trussc::CoreEvents::focusChanged;
        t["hotReloadUnload"] = &trussc::CoreEvents::hotReloadUnload;
        t["keyPressed"] = &trussc::CoreEvents::keyPressed;
        t["keyReleased"] = &trussc::CoreEvents::keyReleased;
        t["mouseDragged"] = &trussc::CoreEvents::mouseDragged;
        t["mouseMoved"] = &trussc::CoreEvents::mouseMoved;
        t["mousePressed"] = &trussc::CoreEvents::mousePressed;
        t["mouseReleased"] = &trussc::CoreEvents::mouseReleased;
        t["mouseScrolled"] = &trussc::CoreEvents::mouseScrolled;
        t["occludedChanged"] = &trussc::CoreEvents::occludedChanged;
        t["onRender"] = &trussc::CoreEvents::onRender;
        t["rawEvent"] = &trussc::CoreEvents::rawEvent;
        t["touchMoved"] = &trussc::CoreEvents::touchMoved;
        t["touchPressed"] = &trussc::CoreEvents::touchPressed;
        t["touchReleased"] = &trussc::CoreEvents::touchReleased;
        t["update"] = &trussc::CoreEvents::update;
        t["windowResized"] = &trussc::CoreEvents::windowResized;
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
        t["source"] = &trussc::MouseMoveEventArgs::source;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::FileReader
            , trussc::FileReader()
        >();
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader", constructors, sol::call_constructor, constructors);
        t["close"] = &trussc::FileReader::close;
        t["eof"] = &trussc::FileReader::eof;
        t["isOpen"] = &trussc::FileReader::isOpen;
        t["open"] = &trussc::FileReader::open;
        t["readChar"] = &trussc::FileReader::readChar;
        t["readLine"] = [](trussc::FileReader& self) { return self.readLine(); };
        t["remaining"] = &trussc::FileReader::remaining;
        t["seek"] = &trussc::FileReader::seek;
        t["tell"] = &trussc::FileReader::tell;
    }
    lua->new_usertype<trussc::PrimitiveType>("PrimitiveType",
        sol::meta_function::equal_to, [](trussc::PrimitiveType a, trussc::PrimitiveType b){ return a == b; },
        "Points", sol::var(trussc::PrimitiveType::Points),
        "Lines", sol::var(trussc::PrimitiveType::Lines),
        "LineStrip", sol::var(trussc::PrimitiveType::LineStrip),
        "Triangles", sol::var(trussc::PrimitiveType::Triangles),
        "TriangleStrip", sol::var(trussc::PrimitiveType::TriangleStrip),
        "Quads", sol::var(trussc::PrimitiveType::Quads));
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
    {
        sol::usertype<trussc::TcpServerClient> t = lua->new_usertype<trussc::TcpServerClient>("TcpServerClient");
        t["getHost"] = &trussc::TcpServerClient::getHost;
        t["getId"] = &trussc::TcpServerClient::getId;
        t["getPort"] = &trussc::TcpServerClient::getPort;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
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
