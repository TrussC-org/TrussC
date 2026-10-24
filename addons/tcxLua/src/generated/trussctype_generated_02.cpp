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
void tcxLuaGenShard_02(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::UdpSocket
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::UdpSocket()
#endif
        >();
        sol::usertype<trussc::UdpSocket> t = lua->new_usertype<trussc::UdpSocket>("UdpSocket", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onError"] = &trussc::UdpSocket::onError;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onReceive"] = &trussc::UdpSocket::onReceive;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["bind"] = sol::overload([](trussc::UdpSocket& self, int port) { return self.bind(port); }, [](trussc::UdpSocket& self, int port, bool startReceiving) { return self.bind(port, startReceiving); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::UdpSocket::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["connect"] = &trussc::UdpSocket::connect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["create"] = &trussc::UdpSocket::create;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedHost"] = &trussc::UdpSocket::getConnectedHost;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getConnectedPort"] = &trussc::UdpSocket::getConnectedPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getLocalPort"] = &trussc::UdpSocket::getLocalPort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isReceiving"] = &trussc::UdpSocket::isReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isValid"] = &trussc::UdpSocket::isValid;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["joinMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.joinMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.joinMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["leaveMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.leaveMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.leaveMulticastGroup(groupAddr, interfaceAddr); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["processNetwork"] = &trussc::UdpSocket::processNetwork;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["send"] = [](trussc::UdpSocket& self, const std::string & message) { return self.send(message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["sendTo"] = [](trussc::UdpSocket& self, const std::string & host, int port, const std::string & message) { return self.sendTo(host, port, message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setBroadcast"] = &trussc::UdpSocket::setBroadcast;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastInterface"] = &trussc::UdpSocket::setMulticastInterface;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastLoopback"] = &trussc::UdpSocket::setMulticastLoopback;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setMulticastTTL"] = &trussc::UdpSocket::setMulticastTTL;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setNonBlocking"] = &trussc::UdpSocket::setNonBlocking;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveBufferSize"] = &trussc::UdpSocket::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveTimeout"] = &trussc::UdpSocket::setReceiveTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReuseAddress"] = &trussc::UdpSocket::setReuseAddress;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReusePort"] = &trussc::UdpSocket::setReusePort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setSendBufferSize"] = &trussc::UdpSocket::setSendBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setUseThread"] = &trussc::UdpSocket::setUseThread;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["startReceiving"] = &trussc::UdpSocket::startReceiving;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stopReceiving"] = &trussc::UdpSocket::stopReceiving;
#endif
    }
#endif
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
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["derived"] = &trussc::JsonWriteReflector::derived;
        t["includeDerived"] = &trussc::JsonWriteReflector::includeDerived;
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
        t["isDerived"] = &trussc::JsonWriteReflector::isDerived;
        t["isReadOnly"] = &trussc::JsonWriteReflector::isReadOnly;
        t["popDerived"] = &trussc::JsonWriteReflector::popDerived;
        t["popReadOnly"] = &trussc::JsonWriteReflector::popReadOnly;
        t["pushDerived"] = &trussc::JsonWriteReflector::pushDerived;
        t["pushReadOnly"] = &trussc::JsonWriteReflector::pushReadOnly;
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
    lua->new_usertype<trussc::BlendMode>("BlendMode",
        sol::meta_function::equal_to, [](trussc::BlendMode a, trussc::BlendMode b){ return a == b; },
        "Alpha", sol::var(trussc::BlendMode::Alpha),
        "Add", sol::var(trussc::BlendMode::Add),
        "Multiply", sol::var(trussc::BlendMode::Multiply),
        "Screen", sol::var(trussc::BlendMode::Screen),
        "Subtract", sol::var(trussc::BlendMode::Subtract),
        "Disabled", sol::var(trussc::BlendMode::Disabled));
    lua->new_usertype<trussc::StrokeMesh::CapType>("StrokeMeshCapType",
        sol::meta_function::equal_to, [](trussc::StrokeMesh::CapType a, trussc::StrokeMesh::CapType b){ return a == b; },
        "CAP_BUTT", sol::var(trussc::StrokeMesh::CapType::CAP_BUTT),
        "CAP_ROUND", sol::var(trussc::StrokeMesh::CapType::CAP_ROUND),
        "CAP_SQUARE", sol::var(trussc::StrokeMesh::CapType::CAP_SQUARE));
    lua->new_usertype<trussc::TextureWrap>("TextureWrap",
        sol::meta_function::equal_to, [](trussc::TextureWrap a, trussc::TextureWrap b){ return a == b; },
        "Repeat", sol::var(trussc::TextureWrap::Repeat),
        "ClampToEdge", sol::var(trussc::TextureWrap::ClampToEdge),
        "MirroredRepeat", sol::var(trussc::TextureWrap::MirroredRepeat));
    lua->new_usertype<trussc::SoundStream::Kind>("SoundStreamKind",
        sol::meta_function::equal_to, [](trussc::SoundStream::Kind a, trussc::SoundStream::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundStream::Kind::Eager),
        "Stream", sol::var(trussc::SoundStream::Kind::Stream));
    {
        sol::usertype<trussc::ScreenshotSaveEventArgs> t = lua->new_usertype<trussc::ScreenshotSaveEventArgs>("ScreenshotSaveEventArgs");
        t["path"] = &trussc::ScreenshotSaveEventArgs::path;
        t["result"] = &trussc::ScreenshotSaveEventArgs::result;
    }
    {
        sol::usertype<trussc::MicStartResult> t = lua->new_usertype<trussc::MicStartResult>("MicStartResult");
        t["message"] = &trussc::MicStartResult::message;
        t["success"] = &trussc::MicStartResult::success;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
