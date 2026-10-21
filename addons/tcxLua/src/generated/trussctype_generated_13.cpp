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
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    {
        auto constructors = tcxLuaConstructors<trussc::Window
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
            , trussc::Window()
#endif
        >();
        sol::usertype<trussc::Window> t = lua->new_usertype<trussc::Window>("Window", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["close"] = &trussc::Window::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchKeyPressToTree"] = &trussc::Window::dispatchKeyPressToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchKeyReleaseToTree"] = &trussc::Window::dispatchKeyReleaseToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMousePressToTree"] = &trussc::Window::dispatchMousePressToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMouseReleaseToTree"] = &trussc::Window::dispatchMouseReleaseToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["dispatchMouseScrollToTree"] = &trussc::Window::dispatchMouseScrollToTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["drawTreeNow"] = &trussc::Window::drawTreeNow;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["events"] = &trussc::Window::events;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getApp"] = &trussc::Window::getApp;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getFps"] = &trussc::Window::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getHeight"] = &trussc::Window::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getTitle"] = &trussc::Window::getTitle;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getWidth"] = &trussc::Window::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isFullscreen"] = &trussc::Window::isFullscreen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isOccluded"] = &trussc::Window::isOccluded;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["isOpen"] = &trussc::Window::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setApp"] = &trussc::Window::setApp;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setClearColor"] = &trussc::Window::setClearColor;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFps"] = &trussc::Window::setFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFullscreen"] = &trussc::Window::setFullscreen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setSize"] = &trussc::Window::setSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setTitle"] = &trussc::Window::setTitle;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["syncRootSize"] = &trussc::Window::syncRootSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["tickTree"] = &trussc::Window::tickTree;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["toggleFullscreen"] = &trussc::Window::toggleFullscreen;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::VideoPlayerBase
            , trussc::VideoPlayerBase()
        >();
        sol::usertype<trussc::VideoPlayerBase> t = lua->new_usertype<trussc::VideoPlayerBase>("VideoPlayerBase", constructors, sol::call_constructor, constructors);
        t["onError"] = &trussc::VideoPlayerBase::onError;
        t["close"] = &trussc::VideoPlayerBase::close;
        t["draw"] = sol::overload([](trussc::VideoPlayerBase& self, float x, float y) { return self.draw(x, y); }, [](trussc::VideoPlayerBase& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["firstFrame"] = &trussc::VideoPlayerBase::firstFrame;
        t["getAudioChannels"] = &trussc::VideoPlayerBase::getAudioChannels;
        t["getAudioCodec"] = &trussc::VideoPlayerBase::getAudioCodec;
        t["getAudioData"] = &trussc::VideoPlayerBase::getAudioData;
        t["getAudioSampleRate"] = &trussc::VideoPlayerBase::getAudioSampleRate;
        t["getCurrentFrame"] = &trussc::VideoPlayerBase::getCurrentFrame;
        t["getCurrentTime"] = &trussc::VideoPlayerBase::getCurrentTime;
        t["getDuration"] = &trussc::VideoPlayerBase::getDuration;
        t["getErrorMessage"] = &trussc::VideoPlayerBase::getErrorMessage;
        t["getFrameRate"] = &trussc::VideoPlayerBase::getFrameRate;
        t["getHeight"] = &trussc::VideoPlayerBase::getHeight;
        t["getHwAccelName"] = &trussc::VideoPlayerBase::getHwAccelName;
        t["getMagFilter"] = &trussc::VideoPlayerBase::getMagFilter;
        t["getMinFilter"] = &trussc::VideoPlayerBase::getMinFilter;
        t["getPan"] = &trussc::VideoPlayerBase::getPan;
        t["getPixels"] = [](trussc::VideoPlayerBase& self) { return self.getPixels(); };
        t["getPosition"] = &trussc::VideoPlayerBase::getPosition;
        t["getResyncThreshold"] = &trussc::VideoPlayerBase::getResyncThreshold;
        t["getSpeed"] = &trussc::VideoPlayerBase::getSpeed;
        t["getTexture"] = [](trussc::VideoPlayerBase& self) -> decltype(auto) { return self.getTexture(); };
        t["getTotalFrames"] = &trussc::VideoPlayerBase::getTotalFrames;
        t["getVolume"] = &trussc::VideoPlayerBase::getVolume;
        t["getWidth"] = &trussc::VideoPlayerBase::getWidth;
        t["getWrapU"] = &trussc::VideoPlayerBase::getWrapU;
        t["getWrapV"] = &trussc::VideoPlayerBase::getWrapV;
        t["hasAudio"] = &trussc::VideoPlayerBase::hasAudio;
        t["hasError"] = &trussc::VideoPlayerBase::hasError;
        t["hasTexture"] = &trussc::VideoPlayerBase::hasTexture;
        t["isDone"] = &trussc::VideoPlayerBase::isDone;
        t["isFrameNew"] = &trussc::VideoPlayerBase::isFrameNew;
        t["isLoaded"] = &trussc::VideoPlayerBase::isLoaded;
        t["isLoop"] = &trussc::VideoPlayerBase::isLoop;
        t["isPaused"] = &trussc::VideoPlayerBase::isPaused;
        t["isPlaying"] = &trussc::VideoPlayerBase::isPlaying;
        t["isReady"] = &trussc::VideoPlayerBase::isReady;
        t["isUsingHwAccel"] = &trussc::VideoPlayerBase::isUsingHwAccel;
        t["load"] = &trussc::VideoPlayerBase::load;
        t["nextFrame"] = &trussc::VideoPlayerBase::nextFrame;
        t["play"] = &trussc::VideoPlayerBase::play;
        t["previousFrame"] = &trussc::VideoPlayerBase::previousFrame;
        t["save"] = &trussc::VideoPlayerBase::save;
        t["setCurrentTime"] = &trussc::VideoPlayerBase::setCurrentTime;
        t["setFilter"] = &trussc::VideoPlayerBase::setFilter;
        t["setFrame"] = &trussc::VideoPlayerBase::setFrame;
        t["setLoop"] = &trussc::VideoPlayerBase::setLoop;
        t["setMagFilter"] = &trussc::VideoPlayerBase::setMagFilter;
        t["setMinFilter"] = &trussc::VideoPlayerBase::setMinFilter;
        t["setPan"] = &trussc::VideoPlayerBase::setPan;
        t["setPaused"] = &trussc::VideoPlayerBase::setPaused;
        t["setPosition"] = &trussc::VideoPlayerBase::setPosition;
        t["setResyncThreshold"] = &trussc::VideoPlayerBase::setResyncThreshold;
        t["setSpeed"] = &trussc::VideoPlayerBase::setSpeed;
        t["setVolume"] = &trussc::VideoPlayerBase::setVolume;
        t["setWrap"] = &trussc::VideoPlayerBase::setWrap;
        t["setWrapU"] = &trussc::VideoPlayerBase::setWrapU;
        t["setWrapV"] = &trussc::VideoPlayerBase::setWrapV;
        t["stop"] = &trussc::VideoPlayerBase::stop;
        t["togglePause"] = &trussc::VideoPlayerBase::togglePause;
        t["update"] = &trussc::VideoPlayerBase::update;
    }
    {
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["clipboardSize"] = &trussc::WindowSettings::clipboardSize;
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["height"] = &trussc::WindowSettings::height;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["title"] = &trussc::WindowSettings::title;
        t["uniformBufferReserve"] = &trussc::WindowSettings::uniformBufferReserve;
        t["width"] = &trussc::WindowSettings::width;
        t["reserveUniformBuffer"] = &trussc::WindowSettings::reserveUniformBuffer;
        t["setClipboardSize"] = &trussc::WindowSettings::setClipboardSize;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
    }
    lua->new_usertype<trussc::TextureFormat>("TextureFormat",
        sol::meta_function::equal_to, [](trussc::TextureFormat a, trussc::TextureFormat b){ return a == b; },
        "RGBA8", sol::var(trussc::TextureFormat::RGBA8),
        "RGBA16F", sol::var(trussc::TextureFormat::RGBA16F),
        "RGBA32F", sol::var(trussc::TextureFormat::RGBA32F),
        "R8", sol::var(trussc::TextureFormat::R8),
        "R16F", sol::var(trussc::TextureFormat::R16F),
        "R32F", sol::var(trussc::TextureFormat::R32F),
        "RG8", sol::var(trussc::TextureFormat::RG8),
        "RG16F", sol::var(trussc::TextureFormat::RG16F),
        "RG32F", sol::var(trussc::TextureFormat::RG32F),
        "BGRA8", sol::var(trussc::TextureFormat::BGRA8),
        "RGBA16", sol::var(trussc::TextureFormat::RGBA16));
    {
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["level"] = &trussc::PlayingSound::level;
        t["loop"] = &trussc::PlayingSound::loop;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["pan"] = &trussc::PlayingSound::pan;
        t["paused"] = &trussc::PlayingSound::paused;
        t["playing"] = &trussc::PlayingSound::playing;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
        t["speed"] = &trussc::PlayingSound::speed;
        t["volume"] = &trussc::PlayingSound::volume;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::LogEventArgs
            , trussc::LogEventArgs(trussc::LogLevel, const std::string &)
        >();
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs", constructors, sol::call_constructor, constructors);
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
    }
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    lua->new_usertype<trussc::ImageType>("ImageType",
        sol::meta_function::equal_to, [](trussc::ImageType a, trussc::ImageType b){ return a == b; },
        "Color", sol::var(trussc::ImageType::Color),
        "Grayscale", sol::var(trussc::ImageType::Grayscale));
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
        t["message"] = &trussc::UdpErrorEventArgs::message;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
