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
        sol::usertype<trussc::RectNode> t = lua->new_usertype<trussc::RectNode>("RectNode");
        t["mousePressed"] = &trussc::RectNode::mousePressed;
        t["mouseReleased"] = &trussc::RectNode::mouseReleased;
        t["mouseDragged"] = &trussc::RectNode::mouseDragged;
        t["mouseScrolled"] = &trussc::RectNode::mouseScrolled;
        t["getWidth"] = &trussc::RectNode::getWidth;
        t["getHeight"] = &trussc::RectNode::getHeight;
        t["getSize"] = &trussc::RectNode::getSize;
        t["setWidth"] = &trussc::RectNode::setWidth;
        t["setHeight"] = &trussc::RectNode::setHeight;
        t["setSize"] = sol::overload([](trussc::RectNode& self, float w, float h) { return self.setSize(w, h); }, [](trussc::RectNode& self, float size) { return self.setSize(size); }, [](trussc::RectNode& self, const trussc::Vec2 & s) { return self.setSize(s); });
        t["setRect"] = &trussc::RectNode::setRect;
        t["setClipping"] = &trussc::RectNode::setClipping;
        t["isClipping"] = &trussc::RectNode::isClipping;
        t["getLeft"] = &trussc::RectNode::getLeft;
        t["getRight"] = &trussc::RectNode::getRight;
        t["getTop"] = &trussc::RectNode::getTop;
        t["getBottom"] = &trussc::RectNode::getBottom;
        t["hitTest"] = [](trussc::RectNode& self, trussc::Vec2 local) { return self.hitTest(local); };
        t["draw"] = &trussc::RectNode::draw;
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec2>> t = lua->new_usertype<trussc::Tween<trussc::Vec2>>("Tween_Vec2",
            sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream",
            sol::constructors<trussc::SoundStream()>(),
            sol::call_constructor, sol::constructors<trussc::SoundStream()>());
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
    }
    lua->new_usertype<trussc::VideoCodec>("VideoCodec",
        sol::meta_function::equal_to, [](trussc::VideoCodec a, trussc::VideoCodec b){ return a == b; },
        "H264", sol::var(trussc::VideoCodec::H264),
        "HEVC", sol::var(trussc::VideoCodec::HEVC),
        "ProRes422", sol::var(trussc::VideoCodec::ProRes422),
        "ProRes4444", sol::var(trussc::VideoCodec::ProRes4444));
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["success"] = &trussc::TcpConnectEventArgs::success;
        t["message"] = &trussc::TcpConnectEventArgs::message;
    }
    {
        sol::usertype<trussc::GrabberFrame> t = lua->new_usertype<trussc::GrabberFrame>("GrabberFrame");
        t["pixels"] = &trussc::GrabberFrame::pixels;
        t["timestampUs"] = &trussc::GrabberFrame::timestampUs;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
