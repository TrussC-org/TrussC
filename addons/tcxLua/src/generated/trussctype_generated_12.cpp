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
        sol::usertype<trussc::Ray> t = lua->new_usertype<trussc::Ray>("Ray",
            sol::constructors<trussc::Ray(), trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)>(),
            sol::call_constructor, sol::constructors<trussc::Ray(), trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)>());
        t["origin"] = &trussc::Ray::origin;
        t["direction"] = &trussc::Ray::direction;
        t["at"] = &trussc::Ray::at;
        t["transformed"] = &trussc::Ray::transformed;
        t["intersectZPlane"] = [](trussc::Ray& self) { return self.intersectZPlane(); };
        t["intersectPlane"] = [](trussc::Ray& self, const trussc::Vec3 & planeNormal, float planeD) { return self.intersectPlane(planeNormal, planeD); };
        t["intersectSphere"] = [](trussc::Ray& self, float radius) { return self.intersectSphere(radius); };
        t["intersectAABB"] = [](trussc::Ray& self, const trussc::Vec3 & boxMin, const trussc::Vec3 & boxMax) { return self.intersectAABB(boxMin, boxMax); };
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
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
    lua->new_usertype<trussc::ChipSoundNote::Wave>("ChipSoundNoteWave",
        sol::meta_function::equal_to, [](trussc::ChipSoundNote::Wave a, trussc::ChipSoundNote::Wave b){ return a == b; },
        "Sin", sol::var(trussc::ChipSoundNote::Wave::Sin),
        "Square", sol::var(trussc::ChipSoundNote::Wave::Square),
        "Triangle", sol::var(trussc::ChipSoundNote::Wave::Triangle),
        "Sawtooth", sol::var(trussc::ChipSoundNote::Wave::Sawtooth),
        "Noise", sol::var(trussc::ChipSoundNote::Wave::Noise),
        "PinkNoise", sol::var(trussc::ChipSoundNote::Wave::PinkNoise),
        "Silent", sol::var(trussc::ChipSoundNote::Wave::Silent));
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    lua->new_usertype<trussc::VideoCodec>("VideoCodec",
        sol::meta_function::equal_to, [](trussc::VideoCodec a, trussc::VideoCodec b){ return a == b; },
        "H264", sol::var(trussc::VideoCodec::H264),
        "HEVC", sol::var(trussc::VideoCodec::HEVC),
        "ProRes422", sol::var(trussc::VideoCodec::ProRes422),
        "ProRes4444", sol::var(trussc::VideoCodec::ProRes4444));
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
    }
    lua->new_usertype<trussc::AxisMode>("AxisMode",
        sol::meta_function::equal_to, [](trussc::AxisMode a, trussc::AxisMode b){ return a == b; },
        "None", sol::var(trussc::AxisMode::None),
        "Fill", sol::var(trussc::AxisMode::Fill),
        "Content", sol::var(trussc::AxisMode::Content));
    lua->new_usertype<trussc::Deliver>("Deliver",
        sol::meta_function::equal_to, [](trussc::Deliver a, trussc::Deliver b){ return a == b; },
        "Inline", sol::var(trussc::Deliver::Inline),
        "Main", sol::var(trussc::Deliver::Main));
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
