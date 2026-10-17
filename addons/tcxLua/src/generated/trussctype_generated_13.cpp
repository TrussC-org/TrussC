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
        auto constructors = tcxLuaConstructors<trussc::Rect
            , trussc::Rect()
            , trussc::Rect(float, float, float, float)
            , trussc::Rect(const trussc::Vec2 &, float, float)
            , trussc::Rect(const trussc::Vec3 &, float, float)
            , trussc::Rect(float, float, float, float, float)
        >();
        sol::usertype<trussc::Rect> t = lua->new_usertype<trussc::Rect>("Rect", constructors, sol::call_constructor, constructors);
        t["height"] = &trussc::Rect::height;
        t["width"] = &trussc::Rect::width;
        t["x"] = &trussc::Rect::x;
        t["y"] = &trussc::Rect::y;
        t["contains"] = &trussc::Rect::contains;
        t["getBottom"] = &trussc::Rect::getBottom;
        t["getCenter"] = &trussc::Rect::getCenter;
        t["getCenterX"] = &trussc::Rect::getCenterX;
        t["getCenterY"] = &trussc::Rect::getCenterY;
        t["getRight"] = &trussc::Rect::getRight;
        t["intersects"] = &trussc::Rect::intersects;
        t["set"] = sol::overload([](trussc::Rect& self, float x_, float y_, float w_, float h_) -> decltype(auto) { return self.set(x_, y_, w_, h_); }, [](trussc::Rect& self, const trussc::Vec2 & pos, float w_, float h_) -> decltype(auto) { return self.set(pos, w_, h_); });
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
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isApple"] = &trussc::Platform::isApple;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["name"] = &trussc::Platform::name;
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
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
    }
    lua->new_usertype<trussc::CurveStyle::Mode>("CurveStyleMode",
        sol::meta_function::equal_to, [](trussc::CurveStyle::Mode a, trussc::CurveStyle::Mode b){ return a == b; },
        "Tolerance", sol::var(trussc::CurveStyle::Mode::Tolerance),
        "Resolution", sol::var(trussc::CurveStyle::Mode::Resolution));
    {
        sol::usertype<trussc::AudioRecordSettings> t = lua->new_usertype<trussc::AudioRecordSettings>("AudioRecordSettings");
        t["channelMap"] = &trussc::AudioRecordSettings::channelMap;
        t["format"] = &trussc::AudioRecordSettings::format;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
