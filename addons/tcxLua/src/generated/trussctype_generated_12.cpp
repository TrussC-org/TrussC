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
        sol::usertype<trussc::Vec2> t = lua->new_usertype<trussc::Vec2>("Vec2",
            sol::constructors<trussc::Vec2(), trussc::Vec2(float, float), trussc::Vec2(float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec2(), trussc::Vec2(float, float), trussc::Vec2(float)>(),
            sol::meta_function::index, [](const trussc::Vec2& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec2& a){ return -a; },
            sol::meta_function::multiplication, sol::overload([](const trussc::Vec2& a, float b){ return a * b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a * b; }),
            sol::meta_function::division, sol::overload([](const trussc::Vec2& a, float b){ return a / b; }, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a / b; }),
            sol::meta_function::equal_to, [](const trussc::Vec2& a, const trussc::Vec2 & b){ return a == b; });
        t["x"] = &trussc::Vec2::x;
        t["y"] = &trussc::Vec2::y;
        t["set"] = sol::overload([](trussc::Vec2& self, float x_, float y_) -> decltype(auto) { return self.set(x_, y_); }, [](trussc::Vec2& self, const trussc::Vec2 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec2::length;
        t["lengthSquared"] = &trussc::Vec2::lengthSquared;
        t["normalized"] = &trussc::Vec2::normalized;
        t["normalize"] = &trussc::Vec2::normalize;
        t["limit"] = &trussc::Vec2::limit;
        t["dot"] = &trussc::Vec2::dot;
        t["cross"] = &trussc::Vec2::cross;
        t["distance"] = &trussc::Vec2::distance;
        t["distanceSquared"] = &trussc::Vec2::distanceSquared;
        t["angle"] = sol::overload([](trussc::Vec2& self) { return self.angle(); }, [](trussc::Vec2& self, const trussc::Vec2 & v) { return self.angle(v); });
        t["rotated"] = &trussc::Vec2::rotated;
        t["rotate"] = &trussc::Vec2::rotate;
        t["lerp"] = &trussc::Vec2::lerp;
        t["perpendicular"] = &trussc::Vec2::perpendicular;
        t["reflected"] = &trussc::Vec2::reflected;
        t["fromAngle"] = sol::overload([](float radians) { return trussc::Vec2::fromAngle(radians); }, [](float radians, float length) { return trussc::Vec2::fromAngle(radians, length); });
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
    {
        sol::usertype<trussc::Tween<float>> t = lua->new_usertype<trussc::Tween<float>>("Tween_float",
            sol::constructors<trussc::Tween<float>(), trussc::Tween<float>(float, float, float), trussc::Tween<float>(float, float, float, trussc::EaseType), trussc::Tween<float>(float, float, float, trussc::EaseType, trussc::EaseMode)>(),
            sol::call_constructor, sol::constructors<trussc::Tween<float>(), trussc::Tween<float>(float, float, float), trussc::Tween<float>(float, float, float, trussc::EaseType), trussc::Tween<float>(float, float, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::SerialDeviceInfo> t = lua->new_usertype<trussc::SerialDeviceInfo>("SerialDeviceInfo");
        t["deviceId"] = &trussc::SerialDeviceInfo::deviceId;
        t["devicePath"] = &trussc::SerialDeviceInfo::devicePath;
        t["deviceName"] = &trussc::SerialDeviceInfo::deviceName;
        t["getDeviceID"] = &trussc::SerialDeviceInfo::getDeviceID;
        t["getDevicePath"] = &trussc::SerialDeviceInfo::getDevicePath;
        t["getDeviceName"] = &trussc::SerialDeviceInfo::getDeviceName;
    }
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
    }
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
