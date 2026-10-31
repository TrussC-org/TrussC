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
void tcxLuaGenShard_06(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::TcpClient
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::TcpClient()
#endif
        >();
        sol::usertype<trussc::TcpClient> t = lua->new_usertype<trussc::TcpClient>("TcpClient", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onConnect"] = &trussc::TcpClient::onConnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onDisconnect"] = &trussc::TcpClient::onDisconnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onError"] = &trussc::TcpClient::onError;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onReceive"] = &trussc::TcpClient::onReceive;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["onSendComplete"] = &trussc::TcpClient::onSendComplete;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["connect"] = &trussc::TcpClient::connect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["connectAsync"] = &trussc::TcpClient::connectAsync;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["disconnect"] = &trussc::TcpClient::disconnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRemoteHost"] = &trussc::TcpClient::getRemoteHost;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRemotePort"] = &trussc::TcpClient::getRemotePort;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSendAsyncBufferSize"] = &trussc::TcpClient::getSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSendAsyncPendingBytes"] = &trussc::TcpClient::getSendAsyncPendingBytes;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isConnected"] = &trussc::TcpClient::isConnected;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isConnecting"] = &trussc::TcpClient::isConnecting;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isUsingThread"] = &trussc::TcpClient::isUsingThread;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["processNetwork"] = &trussc::TcpClient::processNetwork;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["send"] = sol::overload([](trussc::TcpClient& self, const std::vector<char> & data) { return self.send(data); }, [](trussc::TcpClient& self, const std::string & message) { return self.send(message); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["sendAsync"] = [](trussc::TcpClient& self, const std::string & message) { return self.sendAsync(message); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setBlocking"] = &trussc::TcpClient::setBlocking;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setConnectTimeout"] = &trussc::TcpClient::setConnectTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setReceiveBufferSize"] = &trussc::TcpClient::setReceiveBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setSendAsyncBufferSize"] = &trussc::TcpClient::setSendAsyncBufferSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setSendTimeout"] = &trussc::TcpClient::setSendTimeout;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["setUseThread"] = &trussc::TcpClient::setUseThread;
#endif
    }
#endif
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
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
        t["success"] = &trussc::LoadResult::success;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::OnceGate
            , trussc::OnceGate()
            , trussc::OnceGate(double)
        >();
        sol::usertype<trussc::OnceGate> t = lua->new_usertype<trussc::OnceGate>("OnceGate", constructors, sol::call_constructor, constructors);
        t["isFirstTime"] = &trussc::OnceGate::isFirstTime;
        t["reset"] = &trussc::OnceGate::reset;
    }
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::SoundBuffer::Kind>("SoundBufferKind",
        sol::meta_function::equal_to, [](trussc::SoundBuffer::Kind a, trussc::SoundBuffer::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundBuffer::Kind::Eager),
        "Stream", sol::var(trussc::SoundBuffer::Kind::Stream));
    lua->new_usertype<trussc::ImageType>("ImageType",
        sol::meta_function::equal_to, [](trussc::ImageType a, trussc::ImageType b){ return a == b; },
        "Color", sol::var(trussc::ImageType::Color),
        "Grayscale", sol::var(trussc::ImageType::Grayscale));
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
        t["name"] = &trussc::AudioDeviceInfo::name;
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
