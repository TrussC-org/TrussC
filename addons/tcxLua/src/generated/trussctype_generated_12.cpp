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
        sol::usertype<trussc::ColorLinear> t = lua->new_usertype<trussc::ColorLinear>("ColorLinear",
            sol::constructors<trussc::ColorLinear(), trussc::ColorLinear(float, float, float), trussc::ColorLinear(float, float, float, float), trussc::ColorLinear(float), trussc::ColorLinear(float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorLinear(), trussc::ColorLinear(float, float, float), trussc::ColorLinear(float, float, float, float), trussc::ColorLinear(float), trussc::ColorLinear(float, float)>(),
            sol::meta_function::addition, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a - b; },
            sol::meta_function::multiplication, sol::overload([](const trussc::ColorLinear& a, float b){ return a * b; }, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a * b; }),
            sol::meta_function::division, [](const trussc::ColorLinear& a, float b){ return a / b; },
            sol::meta_function::equal_to, [](const trussc::ColorLinear& a, const trussc::ColorLinear & b){ return a == b; });
        t["r"] = &trussc::ColorLinear::r;
        t["g"] = &trussc::ColorLinear::g;
        t["b"] = &trussc::ColorLinear::b;
        t["a"] = &trussc::ColorLinear::a;
        t["toSRGB"] = &trussc::ColorLinear::toSRGB;
        t["toHSB"] = &trussc::ColorLinear::toHSB;
        t["toOKLab"] = &trussc::ColorLinear::toOKLab;
        t["toOKLCH"] = &trussc::ColorLinear::toOKLCH;
        t["clamped"] = &trussc::ColorLinear::clamped;
        t["clampedLDR"] = &trussc::ColorLinear::clampedLDR;
        t["lerp"] = &trussc::ColorLinear::lerp;
    }
    {
        sol::usertype<trussc::IVec3> t = lua->new_usertype<trussc::IVec3>("IVec3",
            sol::constructors<trussc::IVec3(), trussc::IVec3(int, int, int), trussc::IVec3(int), trussc::IVec3(const trussc::IVec2 &), trussc::IVec3(const trussc::IVec2 &, int)>(),
            sol::call_constructor, sol::constructors<trussc::IVec3(), trussc::IVec3(int, int, int), trussc::IVec3(int), trussc::IVec3(const trussc::IVec2 &), trussc::IVec3(const trussc::IVec2 &, int)>(),
            sol::meta_function::addition, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::IVec3& a){ return -a; },
            sol::meta_function::multiplication, [](const trussc::IVec3& a, int b){ return a * b; },
            sol::meta_function::equal_to, [](const trussc::IVec3& a, const trussc::IVec3 & b){ return a == b; });
        t["x"] = &trussc::IVec3::x;
        t["y"] = &trussc::IVec3::y;
        t["z"] = &trussc::IVec3::z;
        t["toVec3"] = &trussc::IVec3::toVec3;
        t["xy"] = &trussc::IVec3::xy;
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
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
    }
    {
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener",
            sol::constructors<trussc::EventListener()>(),
            sol::call_constructor, sol::constructors<trussc::EventListener()>());
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    {
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader",
            sol::constructors<trussc::FullscreenShader()>(),
            sol::call_constructor, sol::constructors<trussc::FullscreenShader()>());
        t["draw"] = &trussc::FullscreenShader::draw;
    }
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
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
