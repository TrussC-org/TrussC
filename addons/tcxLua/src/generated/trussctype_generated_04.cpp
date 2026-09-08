// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_04(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Font> t = lua->new_usertype<trussc::Font>("Font",
            sol::constructors<trussc::Font()>(),
            sol::call_constructor, sol::constructors<trussc::Font()>());
        t["setOversampling"] = &trussc::Font::setOversampling;
        t["getOversampling"] = &trussc::Font::getOversampling;
        t["setGridFit"] = &trussc::Font::setGridFit;
        t["getGridFit"] = &trussc::Font::getGridFit;
        t["setMipmaps"] = &trussc::Font::setMipmaps;
        t["getMipmaps"] = &trussc::Font::getMipmaps;
        t["load"] = &trussc::Font::load;
        t["isLoaded"] = &trussc::Font::isLoaded;
        t["setAlign"] = sol::overload([](trussc::Font& self, trussc::Direction h, trussc::Direction v) { return self.setAlign(h, v); }, [](trussc::Font& self, trussc::Direction h) { return self.setAlign(h); });
        t["getAlignH"] = &trussc::Font::getAlignH;
        t["getAlignV"] = &trussc::Font::getAlignV;
        t["setLineHeight"] = &trussc::Font::setLineHeight;
        t["setLineHeightEm"] = &trussc::Font::setLineHeightEm;
        t["resetLineHeight"] = &trussc::Font::resetLineHeight;
        t["drawString"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y) { return self.drawString(text, x, y); }, [](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.drawString(text, x, y, h, v); });
        t["getGlyphPath"] = &trussc::Font::getGlyphPath;
        t["getStringPath"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v) { return self.getStringPath(text, x, y, h, v); }, [](trussc::Font& self, const std::string & text, float x, float y) { return self.getStringPath(text, x, y); });
        t["setWritingMode"] = &trussc::Font::setWritingMode;
        t["getWritingMode"] = &trussc::Font::getWritingMode;
        t["setTcyDigits"] = &trussc::Font::setTcyDigits;
        t["setTcyLatin"] = &trussc::Font::setTcyLatin;
        t["getTcyLatinMode"] = &trussc::Font::getTcyLatinMode;
        t["getTcyDigitMax"] = &trussc::Font::getTcyDigitMax;
        t["enableWrap"] = &trussc::Font::enableWrap;
        t["isWrapEnabled"] = &trussc::Font::isWrapEnabled;
        t["setMaxLineLength"] = &trussc::Font::setMaxLineLength;
        t["getMaxLineLength"] = &trussc::Font::getMaxLineLength;
        t["setLatinHyphenation"] = &trussc::Font::setLatinHyphenation;
        t["getLatinHyphenation"] = &trussc::Font::getLatinHyphenation;
        t["setHangingPunctuation"] = &trussc::Font::setHangingPunctuation;
        t["getHangingPunctuation"] = &trussc::Font::getHangingPunctuation;
        t["setKinsoku"] = &trussc::Font::setKinsoku;
        t["getKinsoku"] = &trussc::Font::getKinsoku;
        t["forEachGlyph"] = sol::overload([](trussc::Font& self, const std::string & text, float x, float y, trussc::Direction h, trussc::Direction v, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, h, v, visitor); }, [](trussc::Font& self, const std::string & text, float x, float y, const trussc::Font::GlyphVisitor & visitor) { return self.forEachGlyph(text, x, y, visitor); });
        t["getWidth"] = &trussc::Font::getWidth;
        t["stringWidth"] = &trussc::Font::stringWidth;
        t["getHeight"] = &trussc::Font::getHeight;
        t["getBBox"] = &trussc::Font::getBBox;
        t["getLineHeight"] = &trussc::Font::getLineHeight;
        t["getDefaultLineHeight"] = &trussc::Font::getDefaultLineHeight;
        t["getAscent"] = &trussc::Font::getAscent;
        t["getDescent"] = &trussc::Font::getDescent;
        t["getSize"] = &trussc::Font::getSize;
        t["getMemoryUsage"] = &trussc::Font::getMemoryUsage;
        t["getAtlasMemoryUsage"] = &trussc::Font::getAtlasMemoryUsage;
        t["clearAtlas"] = &trussc::Font::clearAtlas;
        t["getAtlasCount"] = &trussc::Font::getAtlasCount;
        t["getSampler"] = &trussc::Font::getSampler;
        t["getLoadedGlyphCount"] = &trussc::Font::getLoadedGlyphCount;
        t["setDefaultOversampling"] = &trussc::Font::setDefaultOversampling;
        t["getDefaultOversampling"] = &trussc::Font::getDefaultOversampling;
        t["getTotalCacheMemoryUsage"] = &trussc::Font::getTotalCacheMemoryUsage;
    }
    {
        sol::usertype<trussc::ChipSoundNote> t = lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote",
            sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>());
        t["wave"] = &trussc::ChipSoundNote::wave;
        t["hz"] = &trussc::ChipSoundNote::hz;
        t["volume"] = &trussc::ChipSoundNote::volume;
        t["duration"] = &trussc::ChipSoundNote::duration;
        t["attack"] = &trussc::ChipSoundNote::attack;
        t["decay"] = &trussc::ChipSoundNote::decay;
        t["sustain"] = &trussc::ChipSoundNote::sustain;
        t["release"] = &trussc::ChipSoundNote::release;
        t["build"] = &trussc::ChipSoundNote::build;
        t["generateBuffer"] = &trussc::ChipSoundNote::generateBuffer;
        t["getTotalDuration"] = &trussc::ChipSoundNote::getTotalDuration;
    }
    {
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab",
            sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["b"] = &trussc::ColorOKLab::b;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["lerp"] = &trussc::ColorOKLab::lerp;
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
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
    }
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["id"] = &trussc::TouchPoint::id;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["changed"] = &trussc::TouchPoint::changed;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["success"] = &trussc::TcpConnectEventArgs::success;
        t["message"] = &trussc::TcpConnectEventArgs::message;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
