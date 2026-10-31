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
void tcxLuaGenShard_14(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        auto constructors = tcxLuaConstructors<trussc::Serial
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
            , trussc::Serial()
#endif
        >();
        sol::usertype<trussc::Serial> t = lua->new_usertype<trussc::Serial>("Serial", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["onDisconnect"] = &trussc::Serial::onDisconnect;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["available"] = &trussc::Serial::available;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["close"] = &trussc::Serial::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["drain"] = &trussc::Serial::drain;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["flush"] = &trussc::Serial::flush;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["flushInput"] = &trussc::Serial::flushInput;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["flushOutput"] = &trussc::Serial::flushOutput;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getDeviceList"] = &trussc::Serial::getDeviceList;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getDevicePath"] = &trussc::Serial::getDevicePath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["getDroppedByteCount"] = &trussc::Serial::getDroppedByteCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["isConnected"] = &trussc::Serial::isConnected;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["isInitialized"] = &trussc::Serial::isInitialized;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["readByte"] = &trussc::Serial::readByte;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["setup"] = sol::overload([](trussc::Serial& self, const std::string & portName, int baudRate) { return self.setup(portName, baudRate); }, [](trussc::Serial& self, int deviceIndex, int baudRate) { return self.setup(deviceIndex, baudRate); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["writeByte"] = &trussc::Serial::writeByte;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["writeBytes"] = [](trussc::Serial& self, const std::string & buffer) { return self.writeBytes(buffer); };
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["listDevices"] = &trussc::Serial::listDevices;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
        t["printDevices"] = &trussc::Serial::printDevices;
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
        auto constructors = tcxLuaConstructors<trussc::ColorLinear
            , trussc::ColorLinear()
            , trussc::ColorLinear(float, float, float)
            , trussc::ColorLinear(float, float, float, float)
            , trussc::ColorLinear(float)
            , trussc::ColorLinear(float, float)
        >();
        sol::usertype<trussc::ColorLinear> t = lua->new_usertype<trussc::ColorLinear>("ColorLinear", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::ColorLinear& a, float b){ return a * b; }, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a - b; };
        t[sol::meta_function::division] = [](const trussc::ColorLinear& a, float b){ return a / b; };
        t[sol::meta_function::equal_to] = [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a == b; };
        t["a"] = &trussc::ColorLinear::a;
        t["b"] = &trussc::ColorLinear::b;
        t["g"] = &trussc::ColorLinear::g;
        t["r"] = &trussc::ColorLinear::r;
        t["clamped"] = &trussc::ColorLinear::clamped;
        t["clampedLDR"] = &trussc::ColorLinear::clampedLDR;
        t["lerp"] = &trussc::ColorLinear::lerp;
        t["toHSB"] = &trussc::ColorLinear::toHSB;
        t["toOKLCH"] = &trussc::ColorLinear::toOKLCH;
        t["toOKLab"] = &trussc::ColorLinear::toOKLab;
        t["toSRGB"] = &trussc::ColorLinear::toSRGB;
    }
    {
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["pickable"] = &trussc::CameraContext::pickable;
        t["projection"] = &trussc::CameraContext::projection;
        t["view"] = &trussc::CameraContext::view;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
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
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    lua->new_usertype<trussc::WritingMode>("WritingMode",
        sol::meta_function::equal_to, [](trussc::WritingMode a, trussc::WritingMode b){ return a == b; },
        "Horizontal", sol::var(trussc::WritingMode::Horizontal),
        "VerticalRL", sol::var(trussc::WritingMode::VerticalRL));
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["message"] = &trussc::TcpConnectEventArgs::message;
        t["success"] = &trussc::TcpConnectEventArgs::success;
    }
    {
        sol::usertype<trussc::Ray::Hit> t = lua->new_usertype<trussc::Ray::Hit>("RayHit");
        t["hit"] = &trussc::Ray::Hit::hit;
        t["point"] = &trussc::Ray::Hit::point;
        t["t"] = &trussc::Ray::Hit::t;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
