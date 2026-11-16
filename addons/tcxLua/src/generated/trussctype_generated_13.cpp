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
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::VideoWriter
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::VideoWriter()
#endif
        >();
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::VideoWriter::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFps"] = &trussc::VideoWriter::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getHeight"] = &trussc::VideoWriter::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::VideoWriter::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::VideoWriter::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSettings"] = &trussc::VideoWriter::getSettings;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getWidth"] = &trussc::VideoWriter::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isOpen"] = &trussc::VideoWriter::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::ScreenRecorder
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::ScreenRecorder()
#endif
        >();
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::ScreenRecorder::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::ScreenRecorder::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stop"] = &trussc::ScreenRecorder::stop;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["writer"] = &trussc::ScreenRecorder::writer;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::Vec3
            , trussc::Vec3()
            , trussc::Vec3(float, float, float)
            , trussc::Vec3(float)
            , trussc::Vec3(const trussc::Vec2 &)
            , trussc::Vec3(const trussc::Vec2 &, float)
        >();
        sol::usertype<trussc::Vec3> t = lua->new_usertype<trussc::Vec3>("Vec3", constructors, sol::call_constructor, constructors);
        t[sol::meta_function::multiplication] = sol::overload([](const trussc::Vec3& a, float b){ return a * b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a * b; });
        t[sol::meta_function::addition] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a + b; };
        t[sol::meta_function::subtraction] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a - b; };
        t[sol::meta_function::unary_minus] = [](const trussc::Vec3& a){ return -a; };
        t[sol::meta_function::division] = sol::overload([](const trussc::Vec3& a, float b){ return a / b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a / b; });
        t[sol::meta_function::equal_to] = [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a == b; };
        t[sol::meta_function::index] = [](const trussc::Vec3& a, int b){ return a[b]; };
        t["x"] = &trussc::Vec3::x;
        t["y"] = &trussc::Vec3::y;
        t["z"] = &trussc::Vec3::z;
        t["cross"] = &trussc::Vec3::cross;
        t["distance"] = &trussc::Vec3::distance;
        t["distanceSquared"] = &trussc::Vec3::distanceSquared;
        t["dot"] = &trussc::Vec3::dot;
        t["length"] = &trussc::Vec3::length;
        t["lengthSquared"] = &trussc::Vec3::lengthSquared;
        t["lerp"] = &trussc::Vec3::lerp;
        t["limit"] = &trussc::Vec3::limit;
        t["normalize"] = &trussc::Vec3::normalize;
        t["normalized"] = &trussc::Vec3::normalized;
        t["reflected"] = &trussc::Vec3::reflected;
        t["set"] = sol::overload([](trussc::Vec3& self, float x_, float y_, float z_) -> decltype(auto) { return self.set(x_, y_, z_); }, [](trussc::Vec3& self, const trussc::Vec3 & v) -> decltype(auto) { return self.set(v); });
        t["xy"] = &trussc::Vec3::xy;
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
    lua->new_usertype<trussc::BlendFactor>("BlendFactor",
        sol::meta_function::equal_to, [](trussc::BlendFactor a, trussc::BlendFactor b){ return a == b; },
        "Zero", sol::var(trussc::BlendFactor::Zero),
        "One", sol::var(trussc::BlendFactor::One),
        "SrcColor", sol::var(trussc::BlendFactor::SrcColor),
        "OneMinusSrcColor", sol::var(trussc::BlendFactor::OneMinusSrcColor),
        "SrcAlpha", sol::var(trussc::BlendFactor::SrcAlpha),
        "OneMinusSrcAlpha", sol::var(trussc::BlendFactor::OneMinusSrcAlpha),
        "DstColor", sol::var(trussc::BlendFactor::DstColor),
        "OneMinusDstColor", sol::var(trussc::BlendFactor::OneMinusDstColor),
        "DstAlpha", sol::var(trussc::BlendFactor::DstAlpha),
        "OneMinusDstAlpha", sol::var(trussc::BlendFactor::OneMinusDstAlpha));
    lua->new_usertype<trussc::ChipSoundNote::Wave>("ChipSoundNoteWave",
        sol::meta_function::equal_to, [](trussc::ChipSoundNote::Wave a, trussc::ChipSoundNote::Wave b){ return a == b; },
        "Sin", sol::var(trussc::ChipSoundNote::Wave::Sin),
        "Square", sol::var(trussc::ChipSoundNote::Wave::Square),
        "Triangle", sol::var(trussc::ChipSoundNote::Wave::Triangle),
        "Sawtooth", sol::var(trussc::ChipSoundNote::Wave::Sawtooth),
        "Noise", sol::var(trussc::ChipSoundNote::Wave::Noise),
        "PinkNoise", sol::var(trussc::ChipSoundNote::Wave::PinkNoise),
        "Silent", sol::var(trussc::ChipSoundNote::Wave::Silent));
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
    }
    lua->new_usertype<trussc::AudioInitFailure>("AudioInitFailure",
        sol::meta_function::equal_to, [](trussc::AudioInitFailure a, trussc::AudioInitFailure b){ return a == b; },
        "None", sol::var(trussc::AudioInitFailure::None),
        "NoBackend", sol::var(trussc::AudioInitFailure::NoBackend),
        "DeviceOpen", sol::var(trussc::AudioInitFailure::DeviceOpen),
        "DeviceStart", sol::var(trussc::AudioInitFailure::DeviceStart));
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["accuracy"] = &trussc::Location::accuracy;
        t["altitude"] = &trussc::Location::altitude;
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
    }
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
        t["message"] = &trussc::UdpErrorEventArgs::message;
    }
    {
        sol::usertype<trussc::FilesOpenedEventArgs> t = lua->new_usertype<trussc::FilesOpenedEventArgs>("FilesOpenedEventArgs");
        t["files"] = &trussc::FilesOpenedEventArgs::files;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
