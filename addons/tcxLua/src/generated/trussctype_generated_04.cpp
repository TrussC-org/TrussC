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
    {
        sol::usertype<trussc::Tween<trussc::Vec2>> t = lua->new_usertype<trussc::Tween<trussc::Vec2>>("Tween_Vec2",
            sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>());
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
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["success"] = &trussc::TcpConnectEventArgs::success;
        t["message"] = &trussc::TcpConnectEventArgs::message;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
