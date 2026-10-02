// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
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
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh",
            sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>(),
            sol::call_constructor, sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>());
        t["setWidth"] = &trussc::StrokeMesh::setWidth;
        t["setColor"] = &trussc::StrokeMesh::setColor;
        t["setCapType"] = &trussc::StrokeMesh::setCapType;
        t["setJoinType"] = &trussc::StrokeMesh::setJoinType;
        t["setMiterLimit"] = &trussc::StrokeMesh::setMiterLimit;
        t["addVertex"] = sol::overload([](trussc::StrokeMesh& self, float x, float y) -> decltype(auto) { return self.addVertex(x, y); }, [](trussc::StrokeMesh& self, float x, float y, float z) -> decltype(auto) { return self.addVertex(x, y, z); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p) -> decltype(auto) { return self.addVertex(p); }, [](trussc::StrokeMesh& self, const trussc::Vec2 & p) -> decltype(auto) { return self.addVertex(p); });
        t["addVertexWithWidth"] = sol::overload([](trussc::StrokeMesh& self, float x, float y, float width) -> decltype(auto) { return self.addVertexWithWidth(x, y, width); }, [](trussc::StrokeMesh& self, const trussc::Vec3 & p, float width) -> decltype(auto) { return self.addVertexWithWidth(p, width); });
        t["setWidths"] = &trussc::StrokeMesh::setWidths;
        t["setShape"] = &trussc::StrokeMesh::setShape;
        t["setClosed"] = &trussc::StrokeMesh::setClosed;
        t["clear"] = &trussc::StrokeMesh::clear;
        t["update"] = &trussc::StrokeMesh::update;
        t["draw"] = &trussc::StrokeMesh::draw;
        t["getMesh"] = &trussc::StrokeMesh::getMesh;
        t["getPolylines"] = &trussc::StrokeMesh::getPolylines;
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
        sol::usertype<trussc::PlayingSoundInfo> t = lua->new_usertype<trussc::PlayingSoundInfo>("PlayingSoundInfo");
        t["slot"] = &trussc::PlayingSoundInfo::slot;
        t["path"] = &trussc::PlayingSoundInfo::path;
        t["streaming"] = &trussc::PlayingSoundInfo::streaming;
        t["paused"] = &trussc::PlayingSoundInfo::paused;
        t["loop"] = &trussc::PlayingSoundInfo::loop;
        t["position"] = &trussc::PlayingSoundInfo::position;
        t["duration"] = &trussc::PlayingSoundInfo::duration;
        t["volume"] = &trussc::PlayingSoundInfo::volume;
        t["pan"] = &trussc::PlayingSoundInfo::pan;
        t["speed"] = &trussc::PlayingSoundInfo::speed;
        t["level"] = &trussc::PlayingSoundInfo::level;
    }
    lua->new_usertype<trussc::Beep>("Beep",
        sol::meta_function::equal_to, [](trussc::Beep a, trussc::Beep b){ return a == b; },
        "ping", sol::var(trussc::Beep::ping),
        "success", sol::var(trussc::Beep::success),
        "complete", sol::var(trussc::Beep::complete),
        "coin", sol::var(trussc::Beep::coin),
        "error", sol::var(trussc::Beep::error),
        "warning", sol::var(trussc::Beep::warning),
        "cancel", sol::var(trussc::Beep::cancel),
        "click", sol::var(trussc::Beep::click),
        "typing", sol::var(trussc::Beep::typing),
        "notify", sol::var(trussc::Beep::notify),
        "sweep", sol::var(trussc::Beep::sweep));
    lua->new_usertype<trussc::Direction>("Direction",
        sol::meta_function::equal_to, [](trussc::Direction a, trussc::Direction b){ return a == b; },
        "Left", sol::var(trussc::Direction::Left),
        "Center", sol::var(trussc::Direction::Center),
        "Right", sol::var(trussc::Direction::Right),
        "Top", sol::var(trussc::Direction::Top),
        "Bottom", sol::var(trussc::Direction::Bottom),
        "Baseline", sol::var(trussc::Direction::Baseline));
    {
        sol::usertype<trussc::TcpSendCompleteEventArgs> t = lua->new_usertype<trussc::TcpSendCompleteEventArgs>("TcpSendCompleteEventArgs");
        t["clientId"] = &trussc::TcpSendCompleteEventArgs::clientId;
        t["sendId"] = &trussc::TcpSendCompleteEventArgs::sendId;
        t["error"] = &trussc::TcpSendCompleteEventArgs::error;
        t["bytesSent"] = &trussc::TcpSendCompleteEventArgs::bytesSent;
    }
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["id"] = &trussc::TouchPoint::id;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["changed"] = &trussc::TouchPoint::changed;
    }
    lua->new_usertype<trussc::EaseMode>("EaseMode",
        sol::meta_function::equal_to, [](trussc::EaseMode a, trussc::EaseMode b){ return a == b; },
        "In", sol::var(trussc::EaseMode::In),
        "Out", sol::var(trussc::EaseMode::Out),
        "InOut", sol::var(trussc::EaseMode::InOut));
    lua->new_usertype<trussc::PixelFormat>("PixelFormat",
        sol::meta_function::equal_to, [](trussc::PixelFormat a, trussc::PixelFormat b){ return a == b; },
        "U8", sol::var(trussc::PixelFormat::U8),
        "F32", sol::var(trussc::PixelFormat::F32));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
