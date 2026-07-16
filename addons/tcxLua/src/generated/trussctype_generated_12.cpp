// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_12(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Vec4> t = lua->new_usertype<trussc::Vec4>("Vec4",
            sol::constructors<trussc::Vec4(), trussc::Vec4(float, float, float, float), trussc::Vec4(float), trussc::Vec4(const trussc::Vec3 &), trussc::Vec4(const trussc::Vec3 &, float), trussc::Vec4(const trussc::Vec2 &), trussc::Vec4(const trussc::Vec2 &, float), trussc::Vec4(const trussc::Vec2 &, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec4(), trussc::Vec4(float, float, float, float), trussc::Vec4(float), trussc::Vec4(const trussc::Vec3 &), trussc::Vec4(const trussc::Vec3 &, float), trussc::Vec4(const trussc::Vec2 &), trussc::Vec4(const trussc::Vec2 &, float), trussc::Vec4(const trussc::Vec2 &, float, float)>(),
            sol::meta_function::index, [](const trussc::Vec4& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec4& a){ return -a; },
            sol::meta_function::multiplication, sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a * b; }, [](const trussc::Vec4& a, float b){ return a * b; }),
            sol::meta_function::division, sol::overload([](const trussc::Vec4& a, const trussc::Vec4 & b){ return a / b; }, [](const trussc::Vec4& a, float b){ return a / b; }),
            sol::meta_function::equal_to, [](const trussc::Vec4& a, const trussc::Vec4 & b){ return a == b; });
        t["x"] = &trussc::Vec4::x;
        t["y"] = &trussc::Vec4::y;
        t["z"] = &trussc::Vec4::z;
        t["w"] = &trussc::Vec4::w;
        t["set"] = sol::overload([](trussc::Vec4& self, float x_, float y_, float z_, float w_) -> decltype(auto) { return self.set(x_, y_, z_, w_); }, [](trussc::Vec4& self, const trussc::Vec4 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec4::length;
        t["lengthSquared"] = &trussc::Vec4::lengthSquared;
        t["normalized"] = &trussc::Vec4::normalized;
        t["normalize"] = &trussc::Vec4::normalize;
        t["dot"] = &trussc::Vec4::dot;
        t["lerp"] = &trussc::Vec4::lerp;
        t["xy"] = &trussc::Vec4::xy;
        t["xyz"] = &trussc::Vec4::xyz;
    }
    {
        sol::usertype<trussc::ScrollContainer> t = lua->new_usertype<trussc::ScrollContainer>("ScrollContainer",
            sol::constructors<trussc::ScrollContainer()>(),
            sol::call_constructor, sol::constructors<trussc::ScrollContainer()>());
        t["setContent"] = &trussc::ScrollContainer::setContent;
        t["getContent"] = &trussc::ScrollContainer::getContent;
        t["getContentRect"] = &trussc::ScrollContainer::getContentRect;
        t["getScrollX"] = &trussc::ScrollContainer::getScrollX;
        t["getScrollY"] = &trussc::ScrollContainer::getScrollY;
        t["getScroll"] = &trussc::ScrollContainer::getScroll;
        t["setScrollX"] = &trussc::ScrollContainer::setScrollX;
        t["setScrollY"] = &trussc::ScrollContainer::setScrollY;
        t["setScroll"] = sol::overload([](trussc::ScrollContainer& self, float x, float y) { return self.setScroll(x, y); }, [](trussc::ScrollContainer& self, trussc::Vec2 pos) { return self.setScroll(pos); });
        t["getMaxScrollX"] = &trussc::ScrollContainer::getMaxScrollX;
        t["getMaxScrollY"] = &trussc::ScrollContainer::getMaxScrollY;
        t["updateScrollBounds"] = &trussc::ScrollContainer::updateScrollBounds;
        t["isHorizontalScrollEnabled"] = &trussc::ScrollContainer::isHorizontalScrollEnabled;
        t["isVerticalScrollEnabled"] = &trussc::ScrollContainer::isVerticalScrollEnabled;
        t["setHorizontalScrollEnabled"] = &trussc::ScrollContainer::setHorizontalScrollEnabled;
        t["setVerticalScrollEnabled"] = &trussc::ScrollContainer::setVerticalScrollEnabled;
        t["getScrollSpeed"] = &trussc::ScrollContainer::getScrollSpeed;
        t["setScrollSpeed"] = &trussc::ScrollContainer::setScrollSpeed;
    }
    {
        sol::usertype<trussc::HasTexture> t = lua->new_usertype<trussc::HasTexture>("HasTexture");
        t["getTexture"] = [](trussc::HasTexture& self) -> decltype(auto) { return self.getTexture(); };
        t["hasTexture"] = &trussc::HasTexture::hasTexture;
        t["draw"] = sol::overload([](trussc::HasTexture& self, float x, float y) { return self.draw(x, y); }, [](trussc::HasTexture& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["setMinFilter"] = &trussc::HasTexture::setMinFilter;
        t["setMagFilter"] = &trussc::HasTexture::setMagFilter;
        t["setFilter"] = &trussc::HasTexture::setFilter;
        t["getMinFilter"] = &trussc::HasTexture::getMinFilter;
        t["getMagFilter"] = &trussc::HasTexture::getMagFilter;
        t["setWrapU"] = &trussc::HasTexture::setWrapU;
        t["setWrapV"] = &trussc::HasTexture::setWrapV;
        t["setWrap"] = &trussc::HasTexture::setWrap;
        t["getWrapU"] = &trussc::HasTexture::getWrapU;
        t["getWrapV"] = &trussc::HasTexture::getWrapV;
        t["save"] = &trussc::HasTexture::save;
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec2>> t = lua->new_usertype<trussc::Tween<trussc::Vec2>>("Tween_Vec2",
            sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::PlayingSound> t = lua->new_usertype<trussc::PlayingSound>("PlayingSound");
        t["buffer"] = &trussc::PlayingSound::buffer;
        t["volume"] = &trussc::PlayingSound::volume;
        t["pan"] = &trussc::PlayingSound::pan;
        t["speed"] = &trussc::PlayingSound::speed;
        t["loop"] = &trussc::PlayingSound::loop;
        t["playing"] = &trussc::PlayingSound::playing;
        t["paused"] = &trussc::PlayingSound::paused;
        t["mixMode"] = &trussc::PlayingSound::mixMode;
        t["positionF"] = &trussc::PlayingSound::positionF;
        t["rateRatio"] = &trussc::PlayingSound::rateRatio;
    }
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
    {
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream",
            sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>(),
            sol::call_constructor, sol::constructors<trussc::LogStream(trussc::LogLevel), trussc::LogStream(trussc::LogLevel, const std::string &)>());
    }
    {
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader",
            sol::constructors<trussc::FullscreenShader()>(),
            sol::call_constructor, sol::constructors<trussc::FullscreenShader()>());
        t["draw"] = &trussc::FullscreenShader::draw;
    }
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
