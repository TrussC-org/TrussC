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
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH",
            sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLCH::L;
        t["C"] = &trussc::ColorOKLCH::C;
        t["H"] = &trussc::ColorOKLCH::H;
        t["alpha"] = &trussc::ColorOKLCH::alpha;
        t["toOKLab"] = &trussc::ColorOKLCH::toOKLab;
        t["toLinear"] = &trussc::ColorOKLCH::toLinear;
        t["toRGB"] = &trussc::ColorOKLCH::toRGB;
        t["toHSB"] = &trussc::ColorOKLCH::toHSB;
        t["lerp"] = sol::overload([](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t) { return self.lerp(target, t); }, [](trussc::ColorOKLCH& self, const trussc::ColorOKLCH & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    lua->new_usertype<trussc::EaseType>("EaseType",
        sol::meta_function::equal_to, [](trussc::EaseType a, trussc::EaseType b){ return a == b; },
        "Linear", sol::var(trussc::EaseType::Linear),
        "Quad", sol::var(trussc::EaseType::Quad),
        "Cubic", sol::var(trussc::EaseType::Cubic),
        "Quart", sol::var(trussc::EaseType::Quart),
        "Quint", sol::var(trussc::EaseType::Quint),
        "Sine", sol::var(trussc::EaseType::Sine),
        "Expo", sol::var(trussc::EaseType::Expo),
        "Circ", sol::var(trussc::EaseType::Circ),
        "Back", sol::var(trussc::EaseType::Back),
        "Elastic", sol::var(trussc::EaseType::Elastic),
        "Bounce", sol::var(trussc::EaseType::Bounce),
        "Custom", sol::var(trussc::EaseType::Custom));
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
        sol::usertype<trussc::ShaderVertex> t = lua->new_usertype<trussc::ShaderVertex>("ShaderVertex");
        t["x"] = &trussc::ShaderVertex::x;
        t["y"] = &trussc::ShaderVertex::y;
        t["z"] = &trussc::ShaderVertex::z;
        t["u"] = &trussc::ShaderVertex::u;
        t["v"] = &trussc::ShaderVertex::v;
        t["r"] = &trussc::ShaderVertex::r;
        t["g"] = &trussc::ShaderVertex::g;
        t["b"] = &trussc::ShaderVertex::b;
        t["a"] = &trussc::ShaderVertex::a;
    }
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    lua->new_usertype<trussc::LightType>("LightType",
        sol::meta_function::equal_to, [](trussc::LightType a, trussc::LightType b){ return a == b; },
        "Directional", sol::var(trussc::LightType::Directional),
        "Point", sol::var(trussc::LightType::Point),
        "Spot", sol::var(trussc::LightType::Spot));
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
