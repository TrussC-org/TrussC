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
    lua->new_usertype<trussc::Cursor>("Cursor",
        sol::meta_function::equal_to, [](trussc::Cursor a, trussc::Cursor b){ return a == b; },
        "Default", sol::var(trussc::Cursor::Default),
        "Arrow", sol::var(trussc::Cursor::Arrow),
        "IBeam", sol::var(trussc::Cursor::IBeam),
        "Crosshair", sol::var(trussc::Cursor::Crosshair),
        "Hand", sol::var(trussc::Cursor::Hand),
        "ResizeEW", sol::var(trussc::Cursor::ResizeEW),
        "ResizeNS", sol::var(trussc::Cursor::ResizeNS),
        "ResizeNWSE", sol::var(trussc::Cursor::ResizeNWSE),
        "ResizeNESW", sol::var(trussc::Cursor::ResizeNESW),
        "ResizeAll", sol::var(trussc::Cursor::ResizeAll),
        "NotAllowed", sol::var(trussc::Cursor::NotAllowed),
        "Custom0", sol::var(trussc::Cursor::Custom0),
        "Custom1", sol::var(trussc::Cursor::Custom1),
        "Custom2", sol::var(trussc::Cursor::Custom2),
        "Custom3", sol::var(trussc::Cursor::Custom3),
        "Custom4", sol::var(trussc::Cursor::Custom4),
        "Custom5", sol::var(trussc::Cursor::Custom5),
        "Custom6", sol::var(trussc::Cursor::Custom6),
        "Custom7", sol::var(trussc::Cursor::Custom7),
        "Custom8", sol::var(trussc::Cursor::Custom8),
        "Custom9", sol::var(trussc::Cursor::Custom9),
        "Custom10", sol::var(trussc::Cursor::Custom10),
        "Custom11", sol::var(trussc::Cursor::Custom11),
        "Custom12", sol::var(trussc::Cursor::Custom12),
        "Custom13", sol::var(trussc::Cursor::Custom13),
        "Custom14", sol::var(trussc::Cursor::Custom14),
        "Custom15", sol::var(trussc::Cursor::Custom15));
    {
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["build"] = &trussc::ChipSoundBundle::build;
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
    }
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
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["a"] = &trussc::ShaderVertex::a;
        t["b"] = &trussc::ShaderVertex::b;
        t["g"] = &trussc::ShaderVertex::g;
        t["r"] = &trussc::ShaderVertex::r;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
    }
    lua->new_usertype<trussc::AudioRecordSettings::SampleFormat>("AudioRecordSettingsSampleFormat",
        sol::meta_function::equal_to, [](trussc::AudioRecordSettings::SampleFormat a, trussc::AudioRecordSettings::SampleFormat b){ return a == b; },
        "S16", sol::var(trussc::AudioRecordSettings::SampleFormat::S16),
        "F32", sol::var(trussc::AudioRecordSettings::SampleFormat::F32));
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    lua->new_usertype<trussc::SoundStream::Kind>("SoundStreamKind",
        sol::meta_function::equal_to, [](trussc::SoundStream::Kind a, trussc::SoundStream::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundStream::Kind::Eager),
        "Stream", sol::var(trussc::SoundStream::Kind::Stream));
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::ResizeEventArgs> t = lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        t["height"] = &trussc::ResizeEventArgs::height;
        t["width"] = &trussc::ResizeEventArgs::width;
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
