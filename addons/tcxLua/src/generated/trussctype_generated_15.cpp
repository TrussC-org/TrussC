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
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
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
        auto constructors = tcxLuaConstructors<trussc::Font
            , trussc::Font()
        >();
        sol::usertype<trussc::Font> t = lua->new_usertype<trussc::Font>("Font", constructors, sol::call_constructor, constructors);
        t["onLoad"] = &trussc::Font::onLoad;
        t["clearAtlas"] = &trussc::Font::clearAtlas;
        t["drawString"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y) { return self.drawString(text, x, y); }, [](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.drawString(text, x, y, h, v); });
        t["enableWrap"] = &trussc::Font::enableWrap;
        t["forEachGlyph"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, h, v, visitor); }, [](trussc::Font& self, const std::string & text, float x, float y, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, visitor); });
        t["getAlignH"] = &trussc::Font::getAlignH;
        t["getAlignV"] = &trussc::Font::getAlignV;
        t["getAscent"] = &trussc::Font::getAscent;
        t["getAtlasCount"] = &trussc::Font::getAtlasCount;
        t["getAtlasMemoryUsage"] = &trussc::Font::getAtlasMemoryUsage;
        t["getBBox"] = &trussc::Font::getBBox;
        t["getDefaultLineHeight"] = &trussc::Font::getDefaultLineHeight;
        t["getDescent"] = &trussc::Font::getDescent;
        t["getGlyphPath"] = &trussc::Font::getGlyphPath;
        t["getGridFit"] = &trussc::Font::getGridFit;
        t["getHangingPunctuation"] = &trussc::Font::getHangingPunctuation;
        t["getHeight"] = &trussc::Font::getHeight;
        t["getKinsoku"] = &trussc::Font::getKinsoku;
        t["getLatinHyphenation"] = &trussc::Font::getLatinHyphenation;
        t["getLineHeight"] = &trussc::Font::getLineHeight;
        t["getLoadedGlyphCount"] = &trussc::Font::getLoadedGlyphCount;
        t["getMaxLineLength"] = &trussc::Font::getMaxLineLength;
        t["getMemoryUsage"] = &trussc::Font::getMemoryUsage;
        t["getMipmaps"] = &trussc::Font::getMipmaps;
        t["getOversampling"] = &trussc::Font::getOversampling;
        t["getSampler"] = &trussc::Font::getSampler;
        t["getSize"] = &trussc::Font::getSize;
        t["getStringPath"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.getStringPath(text, x, y, h, v); }, [](trussc::Font& self, const std::string & text, float x, float y) { return self.getStringPath(text, x, y); });
        t["getTcyDigitMax"] = &trussc::Font::getTcyDigitMax;
        t["getTcyLatinMode"] = &trussc::Font::getTcyLatinMode;
        t["getWidth"] = &trussc::Font::getWidth;
        t["getWritingMode"] = &trussc::Font::getWritingMode;
        t["isLoaded"] = &trussc::Font::isLoaded;
        t["isLoading"] = &trussc::Font::isLoading;
        t["isWrapEnabled"] = &trussc::Font::isWrapEnabled;
        t["load"] = sol::overload([](trussc::Font& self, const fs::path & nameOrPath, int size) { return self.load(nameOrPath, size); }, [](trussc::Font& self, const fs::path & nameOrPath, int size, int faceIndex) { return self.load(nameOrPath, size, faceIndex); });
        t["loadAsync"] = sol::overload([](trussc::Font& self, const fs::path & nameOrPath, int size) { return self.loadAsync(nameOrPath, size); }, [](trussc::Font& self, const fs::path & nameOrPath, int size, int faceIndex) { return self.loadAsync(nameOrPath, size, faceIndex); });
        t["resetLineHeight"] = &trussc::Font::resetLineHeight;
        t["setAlign"] = sol::overload([](trussc::Font& self, trussc::Direction h, trussc::Direction v) { return self.setAlign(h, v); }, [](trussc::Font& self, trussc::Direction h) { return self.setAlign(h); });
        t["setGridFit"] = &trussc::Font::setGridFit;
        t["setHangingPunctuation"] = &trussc::Font::setHangingPunctuation;
        t["setKinsoku"] = &trussc::Font::setKinsoku;
        t["setLatinHyphenation"] = &trussc::Font::setLatinHyphenation;
        t["setLineHeight"] = &trussc::Font::setLineHeight;
        t["setLineHeightEm"] = &trussc::Font::setLineHeightEm;
        t["setMaxLineLength"] = &trussc::Font::setMaxLineLength;
        t["setMipmaps"] = &trussc::Font::setMipmaps;
        t["setOversampling"] = &trussc::Font::setOversampling;
        t["setTcyDigits"] = &trussc::Font::setTcyDigits;
        t["setTcyLatin"] = &trussc::Font::setTcyLatin;
        t["setWritingMode"] = &trussc::Font::setWritingMode;
        t["stringWidth"] = &trussc::Font::stringWidth;
        t["getDefaultOversampling"] = &trussc::Font::getDefaultOversampling;
        t["getTotalCacheMemoryUsage"] = &trussc::Font::getTotalCacheMemoryUsage;
        t["setDefaultOversampling"] = &trussc::Font::setDefaultOversampling;
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
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["address"] = &trussc::NetworkInterface::address;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["name"] = &trussc::NetworkInterface::name;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
    }
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    {
        sol::usertype<trussc::SaveResult> t = lua->new_usertype<trussc::SaveResult>("SaveResult");
        t["error"] = &trussc::SaveResult::error;
        t["message"] = &trussc::SaveResult::message;
        t["ok"] = &trussc::SaveResult::ok;
        t["fail"] = sol::overload([](trussc::SaveError error) { return trussc::SaveResult::fail(error); }, [](trussc::SaveError error, std::string message) { return trussc::SaveResult::fail(error, message); });
        t["success"] = &trussc::SaveResult::success;
    }
    {
        sol::usertype<trussc::TcpSendCompleteEventArgs> t = lua->new_usertype<trussc::TcpSendCompleteEventArgs>("TcpSendCompleteEventArgs");
        t["bytesSent"] = &trussc::TcpSendCompleteEventArgs::bytesSent;
        t["clientId"] = &trussc::TcpSendCompleteEventArgs::clientId;
        t["error"] = &trussc::TcpSendCompleteEventArgs::error;
        t["sendId"] = &trussc::TcpSendCompleteEventArgs::sendId;
    }
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
    }
    lua->new_usertype<trussc::SoundSource::Kind>("SoundSourceKind",
        sol::meta_function::equal_to, [](trussc::SoundSource::Kind a, trussc::SoundSource::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundSource::Kind::Eager),
        "Stream", sol::var(trussc::SoundSource::Kind::Stream));
    {
        sol::usertype<trussc::AudioRecordSettings> t = lua->new_usertype<trussc::AudioRecordSettings>("AudioRecordSettings");
        t["channelMap"] = &trussc::AudioRecordSettings::channelMap;
        t["format"] = &trussc::AudioRecordSettings::format;
    }
    {
        sol::usertype<trussc::DeviceLostEventArgs> t = lua->new_usertype<trussc::DeviceLostEventArgs>("DeviceLostEventArgs");
        t["cancel"] = &trussc::DeviceLostEventArgs::cancel;
        t["reason"] = &trussc::DeviceLostEventArgs::reason;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
