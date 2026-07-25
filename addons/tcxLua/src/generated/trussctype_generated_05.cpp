// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_05(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Color> t = lua->new_usertype<trussc::Color>("Color",
            sol::constructors<trussc::Color(), trussc::Color(float, float, float), trussc::Color(float, float, float, float), trussc::Color(float), trussc::Color(float, float)>(),
            sol::call_constructor, sol::constructors<trussc::Color(), trussc::Color(float, float, float), trussc::Color(float, float, float, float), trussc::Color(float), trussc::Color(float, float)>(),
            sol::meta_function::addition, [](const trussc::Color& a, const trussc::Color & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Color& a, const trussc::Color & b){ return a - b; },
            sol::meta_function::multiplication, [](const trussc::Color& a, float b){ return a * b; },
            sol::meta_function::division, [](const trussc::Color& a, float b){ return a / b; },
            sol::meta_function::equal_to, [](const trussc::Color& a, const trussc::Color & b){ return a == b; });
        t["r"] = &trussc::Color::r;
        t["g"] = &trussc::Color::g;
        t["b"] = &trussc::Color::b;
        t["a"] = &trussc::Color::a;
        t["set"] = sol::overload([](trussc::Color& self, float r_, float g_, float b_) -> decltype(auto) { return self.set(r_, g_, b_); }, [](trussc::Color& self, float r_, float g_, float b_, float a_) -> decltype(auto) { return self.set(r_, g_, b_, a_); }, [](trussc::Color& self, float gray) -> decltype(auto) { return self.set(gray); }, [](trussc::Color& self, float gray, float a_) -> decltype(auto) { return self.set(gray, a_); }, [](trussc::Color& self, const trussc::Color & c) -> decltype(auto) { return self.set(c); });
        t["toHex"] = sol::overload([](trussc::Color& self) { return self.toHex(); }, [](trussc::Color& self, bool includeAlpha) { return self.toHex(includeAlpha); });
        t["toLinear"] = &trussc::Color::toLinear;
        t["toHSB"] = &trussc::Color::toHSB;
        t["toOKLab"] = &trussc::Color::toOKLab;
        t["toOKLCH"] = &trussc::Color::toOKLCH;
        t["clamped"] = &trussc::Color::clamped;
        t["lerpRGB"] = &trussc::Color::lerpRGB;
        t["lerpLinear"] = &trussc::Color::lerpLinear;
        t["lerpHSB"] = &trussc::Color::lerpHSB;
        t["lerpOKLab"] = &trussc::Color::lerpOKLab;
        t["lerpOKLCH"] = &trussc::Color::lerpOKLCH;
        t["lerp"] = &trussc::Color::lerp;
        t["fromBytes"] = sol::overload([](int r, int g, int b) { return trussc::Color::fromBytes(r, g, b); }, [](int r, int g, int b, int a) { return trussc::Color::fromBytes(r, g, b, a); });
        t["fromHex"] = sol::overload([](uint32_t hex) { return trussc::Color::fromHex(hex); }, [](uint32_t hex, bool hasAlpha) { return trussc::Color::fromHex(hex, hasAlpha); });
        t["fromHSB"] = sol::overload([](float h, float s, float b) { return trussc::Color::fromHSB(h, s, b); }, [](float h, float s, float b, float a) { return trussc::Color::fromHSB(h, s, b, a); });
        t["fromOKLCH"] = sol::overload([](float L, float C, float H) { return trussc::Color::fromOKLCH(L, C, H); }, [](float L, float C, float H, float a) { return trussc::Color::fromOKLCH(L, C, H, a); });
        t["fromOKLab"] = sol::overload([](float L, float a_lab, float b_lab) { return trussc::Color::fromOKLab(L, a_lab, b_lab); }, [](float L, float a_lab, float b_lab, float alpha) { return trussc::Color::fromOKLab(L, a_lab, b_lab, alpha); });
        t["fromLinear"] = sol::overload([](float r, float g, float b) { return trussc::Color::fromLinear(r, g, b); }, [](float r, float g, float b, float a) { return trussc::Color::fromLinear(r, g, b, a); });
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__)
    {
        sol::usertype<trussc::Serial> t = lua->new_usertype<trussc::Serial>("Serial",
            sol::constructors<trussc::Serial()>(),
            sol::call_constructor, sol::constructors<trussc::Serial()>());
        t["getDeviceList"] = &trussc::Serial::getDeviceList;
        t["setup"] = sol::overload([](trussc::Serial& self, const std::string & portName, int baudRate) { return self.setup(portName, baudRate); }, [](trussc::Serial& self, int deviceIndex, int baudRate) { return self.setup(deviceIndex, baudRate); });
        t["close"] = &trussc::Serial::close;
        t["isInitialized"] = &trussc::Serial::isInitialized;
        t["getDevicePath"] = &trussc::Serial::getDevicePath;
        t["available"] = &trussc::Serial::available;
        t["readByte"] = &trussc::Serial::readByte;
        t["writeBytes"] = [](trussc::Serial& self, const std::string & buffer) { return self.writeBytes(buffer); };
        t["writeByte"] = &trussc::Serial::writeByte;
        t["flushInput"] = &trussc::Serial::flushInput;
        t["flushOutput"] = &trussc::Serial::flushOutput;
        t["flush"] = &trussc::Serial::flush;
        t["drain"] = &trussc::Serial::drain;
        t["printDevices"] = &trussc::Serial::printDevices;
        t["listDevices"] = &trussc::Serial::listDevices;
    }
#endif
    {
        sol::usertype<trussc::MouseMoveEventArgs> t = lua->new_usertype<trussc::MouseMoveEventArgs>("MouseMoveEventArgs");
        t["x"] = &trussc::MouseMoveEventArgs::x;
        t["y"] = &trussc::MouseMoveEventArgs::y;
        t["deltaX"] = &trussc::MouseMoveEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseMoveEventArgs::deltaY;
        t["shift"] = &trussc::MouseMoveEventArgs::shift;
        t["ctrl"] = &trussc::MouseMoveEventArgs::ctrl;
        t["alt"] = &trussc::MouseMoveEventArgs::alt;
        t["super"] = &trussc::MouseMoveEventArgs::super;
        t["pos"] = &trussc::MouseMoveEventArgs::pos;
        t["globalPos"] = &trussc::MouseMoveEventArgs::globalPos;
        t["delta"] = &trussc::MouseMoveEventArgs::delta;
        t["globalDelta"] = &trussc::MouseMoveEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseMoveEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseMoveEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader",
            sol::constructors<trussc::FileReader()>(),
            sol::call_constructor, sol::constructors<trussc::FileReader()>());
        t["open"] = &trussc::FileReader::open;
        t["close"] = &trussc::FileReader::close;
        t["isOpen"] = &trussc::FileReader::isOpen;
        t["eof"] = &trussc::FileReader::eof;
        t["readLine"] = [](trussc::FileReader& self) { return self.readLine(); };
        t["readChar"] = &trussc::FileReader::readChar;
        t["seek"] = &trussc::FileReader::seek;
        t["tell"] = &trussc::FileReader::tell;
        t["remaining"] = &trussc::FileReader::remaining;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
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
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
