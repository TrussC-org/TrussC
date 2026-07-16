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
        t["getTotalCacheMemoryUsage"] = &trussc::Font::getTotalCacheMemoryUsage;
    }
    {
        sol::usertype<trussc::Rect> t = lua->new_usertype<trussc::Rect>("Rect",
            sol::constructors<trussc::Rect(), trussc::Rect(float, float, float, float), trussc::Rect(const trussc::Vec2 &, float, float), trussc::Rect(const trussc::Vec3 &, float, float), trussc::Rect(float, float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Rect(), trussc::Rect(float, float, float, float), trussc::Rect(const trussc::Vec2 &, float, float), trussc::Rect(const trussc::Vec3 &, float, float), trussc::Rect(float, float, float, float, float)>());
        t["x"] = &trussc::Rect::x;
        t["y"] = &trussc::Rect::y;
        t["width"] = &trussc::Rect::width;
        t["height"] = &trussc::Rect::height;
        t["set"] = sol::overload([](trussc::Rect& self, float x_, float y_, float w_, float h_) -> decltype(auto) { return self.set(x_, y_, w_, h_); }, [](trussc::Rect& self, const trussc::Vec2 & pos, float w_, float h_) -> decltype(auto) { return self.set(pos, w_, h_); });
        t["getRight"] = &trussc::Rect::getRight;
        t["getBottom"] = &trussc::Rect::getBottom;
        t["getCenter"] = &trussc::Rect::getCenter;
        t["getCenterX"] = &trussc::Rect::getCenterX;
        t["getCenterY"] = &trussc::Rect::getCenterY;
        t["contains"] = &trussc::Rect::contains;
        t["intersects"] = &trussc::Rect::intersects;
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
        sol::usertype<trussc::RectNodeButton> t = lua->new_usertype<trussc::RectNodeButton>("RectNodeButton",
            sol::constructors<trussc::RectNodeButton()>(),
            sol::call_constructor, sol::constructors<trussc::RectNodeButton()>());
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["draw"] = &trussc::RectNodeButton::draw;
    }
    {
        sol::usertype<trussc::GraphicsBackend> t = lua->new_usertype<trussc::GraphicsBackend>("GraphicsBackend");
        t["isWebGPU"] = &trussc::GraphicsBackend::isWebGPU;
        t["isWebGL2"] = &trussc::GraphicsBackend::isWebGL2;
        t["isMetal"] = &trussc::GraphicsBackend::isMetal;
        t["isD3D11"] = &trussc::GraphicsBackend::isD3D11;
        t["isVulkan"] = &trussc::GraphicsBackend::isVulkan;
        t["isOpenGL"] = &trussc::GraphicsBackend::isOpenGL;
        t["name"] = &trussc::GraphicsBackend::name;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
        t["altitude"] = &trussc::Location::altitude;
        t["accuracy"] = &trussc::Location::accuracy;
    }
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
