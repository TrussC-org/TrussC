// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLua::setGeneratedTypeBindings(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::EnumLabelSpan> t = lua->new_usertype<trussc::EnumLabelSpan>("EnumLabelSpan");
        t["count"] = &trussc::EnumLabelSpan::count;
    }
    {
        sol::usertype<trussc::Reflector> t = lua->new_usertype<trussc::Reflector>("Reflector");
        t["isReadOnly"] = &trussc::Reflector::isReadOnly;
        t["pushReadOnly"] = &trussc::Reflector::pushReadOnly;
        t["popReadOnly"] = &trussc::Reflector::popReadOnly;
        t["endGroup"] = &trussc::Reflector::endGroup;
    }
    {
        sol::usertype<trussc::Ray> t = lua->new_usertype<trussc::Ray>("Ray",
            sol::constructors<trussc::Ray(), trussc::Ray(const trussc::Vec3 &, const trussc::Vec3 &)>());
        t["origin"] = &trussc::Ray::origin;
        t["direction"] = &trussc::Ray::direction;
        t["at"] = &trussc::Ray::at;
        t["transformed"] = &trussc::Ray::transformed;
        t["fromScreenPoint2D"] = sol::overload([](float screenX, float screenY) { return trussc::Ray::fromScreenPoint2D(screenX, screenY); }, [](float screenX, float screenY, float startZ) { return trussc::Ray::fromScreenPoint2D(screenX, screenY, startZ); });
    }
    {
        sol::usertype<trussc::CameraContext> t = lua->new_usertype<trussc::CameraContext>("CameraContext");
        t["view"] = &trussc::CameraContext::view;
        t["projection"] = &trussc::CameraContext::projection;
        t["viewW"] = &trussc::CameraContext::viewW;
        t["viewH"] = &trussc::CameraContext::viewH;
        t["pickable"] = &trussc::CameraContext::pickable;
        t["screenPointToRay"] = &trussc::CameraContext::screenPointToRay;
        t["worldToScreen"] = &trussc::CameraContext::worldToScreen;
    }
    {
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener",
            sol::constructors<trussc::EventListener()>());
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    {
        sol::usertype<trussc::LogEventArgs> t = lua->new_usertype<trussc::LogEventArgs>("LogEventArgs",
            sol::constructors<trussc::LogEventArgs(trussc::LogLevel, const std::string &)>());
        t["level"] = &trussc::LogEventArgs::level;
        t["message"] = &trussc::LogEventArgs::message;
        t["timestamp"] = &trussc::LogEventArgs::timestamp;
    }
    {
        sol::usertype<trussc::Logger> t = lua->new_usertype<trussc::Logger>("Logger",
            sol::constructors<trussc::Logger()>());
        t["onLog"] = &trussc::Logger::onLog;
        t["log"] = &trussc::Logger::log;
        t["setConsoleLogLevel"] = &trussc::Logger::setConsoleLogLevel;
        t["getConsoleLogLevel"] = &trussc::Logger::getConsoleLogLevel;
        t["setLogFile"] = &trussc::Logger::setLogFile;
        t["closeFile"] = &trussc::Logger::closeFile;
        t["setFileLogLevel"] = &trussc::Logger::setFileLogLevel;
        t["getFileLogLevel"] = &trussc::Logger::getFileLogLevel;
        t["getLogFilePath"] = &trussc::Logger::getLogFilePath;
        t["isFileOpen"] = &trussc::Logger::isFileOpen;
    }
    {
        sol::usertype<trussc::LogStream> t = lua->new_usertype<trussc::LogStream>("LogStream",
            sol::constructors<trussc::LogStream(trussc::LogLevel, const std::string &)>());
    }
    {
        sol::usertype<trussc::ColorLinear> t = lua->new_usertype<trussc::ColorLinear>("ColorLinear",
            sol::constructors<trussc::ColorLinear(), trussc::ColorLinear(float, float, float, float), trussc::ColorLinear(float, float)>(),
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
        sol::usertype<trussc::ColorHSB> t = lua->new_usertype<trussc::ColorHSB>("ColorHSB",
            sol::constructors<trussc::ColorHSB(), trussc::ColorHSB(float, float, float, float)>());
        t["h"] = &trussc::ColorHSB::h;
        t["s"] = &trussc::ColorHSB::s;
        t["b"] = &trussc::ColorHSB::b;
        t["a"] = &trussc::ColorHSB::a;
        t["toRGB"] = &trussc::ColorHSB::toRGB;
        t["toLinear"] = &trussc::ColorHSB::toLinear;
        t["toOKLab"] = &trussc::ColorHSB::toOKLab;
        t["toOKLCH"] = &trussc::ColorHSB::toOKLCH;
        t["lerp"] = sol::overload([](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t) { return self.lerp(target, t); }, [](trussc::ColorHSB& self, const trussc::ColorHSB & target, float t, bool shortestPath) { return self.lerp(target, t, shortestPath); });
    }
    {
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab",
            sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float, float)>());
        t["L"] = &trussc::ColorOKLab::L;
        t["a"] = &trussc::ColorOKLab::a;
        t["b"] = &trussc::ColorOKLab::b;
        t["alpha"] = &trussc::ColorOKLab::alpha;
        t["toLinear"] = &trussc::ColorOKLab::toLinear;
        t["toRGB"] = &trussc::ColorOKLab::toRGB;
        t["toHSB"] = &trussc::ColorOKLab::toHSB;
        t["toOKLCH"] = &trussc::ColorOKLab::toOKLCH;
        t["lerp"] = &trussc::ColorOKLab::lerp;
    }
    {
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH",
            sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float, float)>());
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
    {
        sol::usertype<trussc::Platform> t = lua->new_usertype<trussc::Platform>("Platform");
        t["isWeb"] = &trussc::Platform::isWeb;
        t["isMacOS"] = &trussc::Platform::isMacOS;
        t["isIOS"] = &trussc::Platform::isIOS;
        t["isWindows"] = &trussc::Platform::isWindows;
        t["isAndroid"] = &trussc::Platform::isAndroid;
        t["isLinux"] = &trussc::Platform::isLinux;
        t["isApple"] = &trussc::Platform::isApple;
        t["isMobile"] = &trussc::Platform::isMobile;
        t["isDesktop"] = &trussc::Platform::isDesktop;
        t["name"] = &trussc::Platform::name;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
        t["altitude"] = &trussc::Location::altitude;
        t["accuracy"] = &trussc::Location::accuracy;
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
        sol::usertype<trussc::BuildInfo> t = lua->new_usertype<trussc::BuildInfo>("BuildInfo");
        t["date"] = &trussc::BuildInfo::date;
        t["time"] = &trussc::BuildInfo::time;
        t["dateTime"] = &trussc::BuildInfo::dateTime;
        t["timestamp"] = &trussc::BuildInfo::timestamp;
        t["year"] = &trussc::BuildInfo::year;
        t["month"] = &trussc::BuildInfo::month;
        t["day"] = &trussc::BuildInfo::day;
        t["hour"] = &trussc::BuildInfo::hour;
        t["minute"] = &trussc::BuildInfo::minute;
        t["second"] = &trussc::BuildInfo::second;
    }
    {
        sol::usertype<trussc::KeyEventArgs> t = lua->new_usertype<trussc::KeyEventArgs>("KeyEventArgs");
        t["key"] = &trussc::KeyEventArgs::key;
        t["isRepeat"] = &trussc::KeyEventArgs::isRepeat;
        t["shift"] = &trussc::KeyEventArgs::shift;
        t["ctrl"] = &trussc::KeyEventArgs::ctrl;
        t["alt"] = &trussc::KeyEventArgs::alt;
        t["super"] = &trussc::KeyEventArgs::super;
        t["consumed"] = &trussc::KeyEventArgs::consumed;
    }
    {
        sol::usertype<trussc::MouseEventArgs> t = lua->new_usertype<trussc::MouseEventArgs>("MouseEventArgs");
        t["x"] = &trussc::MouseEventArgs::x;
        t["y"] = &trussc::MouseEventArgs::y;
        t["button"] = &trussc::MouseEventArgs::button;
        t["shift"] = &trussc::MouseEventArgs::shift;
        t["ctrl"] = &trussc::MouseEventArgs::ctrl;
        t["alt"] = &trussc::MouseEventArgs::alt;
        t["super"] = &trussc::MouseEventArgs::super;
        t["pos"] = &trussc::MouseEventArgs::pos;
        t["globalPos"] = &trussc::MouseEventArgs::globalPos;
        t["consumed"] = &trussc::MouseEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseEventArgs::syncLegacy;
    }
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
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::ScrollEventArgs> t = lua->new_usertype<trussc::ScrollEventArgs>("ScrollEventArgs");
        t["scrollX"] = &trussc::ScrollEventArgs::scrollX;
        t["scrollY"] = &trussc::ScrollEventArgs::scrollY;
        t["shift"] = &trussc::ScrollEventArgs::shift;
        t["ctrl"] = &trussc::ScrollEventArgs::ctrl;
        t["alt"] = &trussc::ScrollEventArgs::alt;
        t["super"] = &trussc::ScrollEventArgs::super;
        t["pos"] = &trussc::ScrollEventArgs::pos;
        t["globalPos"] = &trussc::ScrollEventArgs::globalPos;
        t["scroll"] = &trussc::ScrollEventArgs::scroll;
        t["consumed"] = &trussc::ScrollEventArgs::consumed;
        t["syncLegacy"] = &trussc::ScrollEventArgs::syncLegacy;
    }
    {
        sol::usertype<trussc::ResizeEventArgs> t = lua->new_usertype<trussc::ResizeEventArgs>("ResizeEventArgs");
        t["width"] = &trussc::ResizeEventArgs::width;
        t["height"] = &trussc::ResizeEventArgs::height;
    }
    {
        sol::usertype<trussc::DragDropEventArgs> t = lua->new_usertype<trussc::DragDropEventArgs>("DragDropEventArgs");
        t["files"] = &trussc::DragDropEventArgs::files;
        t["x"] = &trussc::DragDropEventArgs::x;
        t["y"] = &trussc::DragDropEventArgs::y;
    }
    {
        sol::usertype<trussc::ClipboardPastedEventArgs> t = lua->new_usertype<trussc::ClipboardPastedEventArgs>("ClipboardPastedEventArgs");
        t["text"] = &trussc::ClipboardPastedEventArgs::text;
    }
    {
        sol::usertype<trussc::TouchPoint> t = lua->new_usertype<trussc::TouchPoint>("TouchPoint");
        t["id"] = &trussc::TouchPoint::id;
        t["x"] = &trussc::TouchPoint::x;
        t["y"] = &trussc::TouchPoint::y;
        t["pressure"] = &trussc::TouchPoint::pressure;
        t["changed"] = &trussc::TouchPoint::changed;
    }
    {
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
        t["id"] = &trussc::TouchEventArgs::id;
    }
    {
        sol::usertype<trussc::ConsoleEventArgs> t = lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        t["raw"] = &trussc::ConsoleEventArgs::raw;
        t["args"] = &trussc::ConsoleEventArgs::args;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
    }
    {
        sol::usertype<trussc::CoreEvents> t = lua->new_usertype<trussc::CoreEvents>("CoreEvents");
        t["setup"] = &trussc::CoreEvents::setup;
        t["update"] = &trussc::CoreEvents::update;
        t["draw"] = &trussc::CoreEvents::draw;
        t["onRender"] = &trussc::CoreEvents::onRender;
        t["afterFrame"] = &trussc::CoreEvents::afterFrame;
        t["exit"] = &trussc::CoreEvents::exit;
        t["exitRequested"] = &trussc::CoreEvents::exitRequested;
        t["keyPressed"] = &trussc::CoreEvents::keyPressed;
        t["keyReleased"] = &trussc::CoreEvents::keyReleased;
        t["mousePressed"] = &trussc::CoreEvents::mousePressed;
        t["mouseReleased"] = &trussc::CoreEvents::mouseReleased;
        t["mouseMoved"] = &trussc::CoreEvents::mouseMoved;
        t["mouseDragged"] = &trussc::CoreEvents::mouseDragged;
        t["mouseScrolled"] = &trussc::CoreEvents::mouseScrolled;
        t["windowResized"] = &trussc::CoreEvents::windowResized;
        t["filesDropped"] = &trussc::CoreEvents::filesDropped;
        t["clipboardPasted"] = &trussc::CoreEvents::clipboardPasted;
        t["console"] = &trussc::CoreEvents::console;
        t["touchPressed"] = &trussc::CoreEvents::touchPressed;
        t["touchMoved"] = &trussc::CoreEvents::touchMoved;
        t["touchReleased"] = &trussc::CoreEvents::touchReleased;
        t["rawEvent"] = &trussc::CoreEvents::rawEvent;
    }
    {
        sol::usertype<trussc::AudioSettings> t = lua->new_usertype<trussc::AudioSettings>("AudioSettings");
        t["sampleRate"] = &trussc::AudioSettings::sampleRate;
        t["channels"] = &trussc::AudioSettings::channels;
        t["bufferSize"] = &trussc::AudioSettings::bufferSize;
        t["maxPolyphony"] = &trussc::AudioSettings::maxPolyphony;
        t["deviceName"] = &trussc::AudioSettings::deviceName;
    }
    {
        sol::usertype<trussc::AudioDeviceInfo> t = lua->new_usertype<trussc::AudioDeviceInfo>("AudioDeviceInfo");
        t["name"] = &trussc::AudioDeviceInfo::name;
        t["isDefault"] = &trussc::AudioDeviceInfo::isDefault;
    }
    {
        sol::usertype<trussc::AudioDeviceChangedArgs> t = lua->new_usertype<trussc::AudioDeviceChangedArgs>("AudioDeviceChangedArgs");
        t["deviceName"] = &trussc::AudioDeviceChangedArgs::deviceName;
        t["isDefaultDevice"] = &trussc::AudioDeviceChangedArgs::isDefaultDevice;
        t["sampleRate"] = &trussc::AudioDeviceChangedArgs::sampleRate;
        t["channels"] = &trussc::AudioDeviceChangedArgs::channels;
        t["bufferSize"] = &trussc::AudioDeviceChangedArgs::bufferSize;
        t["maxPolyphony"] = &trussc::AudioDeviceChangedArgs::maxPolyphony;
    }
    {
        sol::usertype<trussc::AudioOutBuffer> t = lua->new_usertype<trussc::AudioOutBuffer>("AudioOutBuffer");
        t["frameCount"] = &trussc::AudioOutBuffer::frameCount;
        t["channels"] = &trussc::AudioOutBuffer::channels;
        t["sampleRate"] = &trussc::AudioOutBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioOutBuffer::framePosition;
    }
    {
        sol::usertype<trussc::AudioInBuffer> t = lua->new_usertype<trussc::AudioInBuffer>("AudioInBuffer");
        t["frameCount"] = &trussc::AudioInBuffer::frameCount;
        t["channels"] = &trussc::AudioInBuffer::channels;
        t["sampleRate"] = &trussc::AudioInBuffer::sampleRate;
        t["framePosition"] = &trussc::AudioInBuffer::framePosition;
    }
    {
        sol::usertype<trussc::FileDialogResult> t = lua->new_usertype<trussc::FileDialogResult>("FileDialogResult");
        t["filePath"] = &trussc::FileDialogResult::filePath;
        t["fileName"] = &trussc::FileDialogResult::fileName;
        t["success"] = &trussc::FileDialogResult::success;
    }
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["members"] = &trussc::JsonWriteReflector::members;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
    }
    {
        sol::usertype<trussc::JsonReadReflector> t = lua->new_usertype<trussc::JsonReadReflector>("JsonReadReflector",
            sol::constructors<trussc::JsonReadReflector(trussc::Json)>());
        t["applied"] = &trussc::JsonReadReflector::applied;
        t["skipped"] = &trussc::JsonReadReflector::skipped;
        t["readOnly"] = &trussc::JsonReadReflector::readOnly;
        t["unknownKeys"] = &trussc::JsonReadReflector::unknownKeys;
        t["endGroup"] = &trussc::JsonReadReflector::endGroup;
    }
    {
        sol::usertype<trussc::FileWriter> t = lua->new_usertype<trussc::FileWriter>("FileWriter",
            sol::constructors<trussc::FileWriter()>());
        t["open"] = sol::overload([](trussc::FileWriter& self, const std::string & path) { return self.open(path); }, [](trussc::FileWriter& self, const std::string & path, bool append) { return self.open(path, append); });
        t["close"] = &trussc::FileWriter::close;
        t["isOpen"] = &trussc::FileWriter::isOpen;
        t["write"] = sol::overload([](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.write(text); }, [](trussc::FileWriter& self, char c) -> decltype(auto) { return self.write(c); });
        t["writeLine"] = sol::overload([](trussc::FileWriter& self) -> decltype(auto) { return self.writeLine(); }, [](trussc::FileWriter& self, const std::string & text) -> decltype(auto) { return self.writeLine(text); });
        t["flush"] = &trussc::FileWriter::flush;
    }
    {
        sol::usertype<trussc::FileReader> t = lua->new_usertype<trussc::FileReader>("FileReader",
            sol::constructors<trussc::FileReader()>());
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
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    {
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["updateFps"] = &trussc::FpsSettings::updateFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["synced"] = &trussc::FpsSettings::synced;
    }
    {
        sol::usertype<trussc::WindowSettings> t = lua->new_usertype<trussc::WindowSettings>("WindowSettings");
        t["width"] = &trussc::WindowSettings::width;
        t["height"] = &trussc::WindowSettings::height;
        t["title"] = &trussc::WindowSettings::title;
        t["highDpi"] = &trussc::WindowSettings::highDpi;
        t["pixelPerfect"] = &trussc::WindowSettings::pixelPerfect;
        t["sampleCount"] = &trussc::WindowSettings::sampleCount;
        t["fullscreen"] = &trussc::WindowSettings::fullscreen;
        t["decorated"] = &trussc::WindowSettings::decorated;
        t["clipboardSize"] = &trussc::WindowSettings::clipboardSize;
        t["swapInterval"] = &trussc::WindowSettings::swapInterval;
        t["setSize"] = &trussc::WindowSettings::setSize;
        t["setTitle"] = &trussc::WindowSettings::setTitle;
        t["setHighDpi"] = &trussc::WindowSettings::setHighDpi;
        t["setPixelPerfect"] = &trussc::WindowSettings::setPixelPerfect;
        t["setSampleCount"] = &trussc::WindowSettings::setSampleCount;
        t["setFullscreen"] = &trussc::WindowSettings::setFullscreen;
        t["setDecorated"] = &trussc::WindowSettings::setDecorated;
        t["setClipboardSize"] = &trussc::WindowSettings::setClipboardSize;
        t["setSwapInterval"] = &trussc::WindowSettings::setSwapInterval;
    }
    {
        sol::usertype<trussc::IesProfile> t = lua->new_usertype<trussc::IesProfile>("IesProfile",
            sol::constructors<trussc::IesProfile()>());
        t["load"] = &trussc::IesProfile::load;
        t["loadFromString"] = &trussc::IesProfile::loadFromString;
        t["isLoaded"] = &trussc::IesProfile::isLoaded;
        t["getMaxVerticalAngle"] = &trussc::IesProfile::getMaxVerticalAngle;
        t["getMaxCandela"] = &trussc::IesProfile::getMaxCandela;
        t["getTextureWidth"] = &trussc::IesProfile::getTextureWidth;
        t["getView"] = &trussc::IesProfile::getView;
        t["getSampler"] = &trussc::IesProfile::getSampler;
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
        sol::usertype<trussc::StrokeMesh> t = lua->new_usertype<trussc::StrokeMesh>("StrokeMesh",
            sol::constructors<trussc::StrokeMesh(), trussc::StrokeMesh(const trussc::Path &)>());
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
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader",
            sol::constructors<trussc::FullscreenShader()>());
        t["draw"] = &trussc::FullscreenShader::draw;
    }
    {
        sol::usertype<trussc::VideoDeviceInfo> t = lua->new_usertype<trussc::VideoDeviceInfo>("VideoDeviceInfo");
        t["deviceId"] = &trussc::VideoDeviceInfo::deviceId;
        t["deviceName"] = &trussc::VideoDeviceInfo::deviceName;
        t["uniqueId"] = &trussc::VideoDeviceInfo::uniqueId;
        t["getDeviceID"] = &trussc::VideoDeviceInfo::getDeviceID;
        t["getDeviceName"] = &trussc::VideoDeviceInfo::getDeviceName;
        t["getUniqueId"] = &trussc::VideoDeviceInfo::getUniqueId;
    }
    {
        sol::usertype<trussc::VideoGrabber> t = lua->new_usertype<trussc::VideoGrabber>("VideoGrabber",
            sol::constructors<trussc::VideoGrabber()>());
        t["listDevices"] = &trussc::VideoGrabber::listDevices;
        t["setDeviceID"] = &trussc::VideoGrabber::setDeviceID;
        t["getDeviceID"] = &trussc::VideoGrabber::getDeviceID;
        t["setDesiredFrameRate"] = &trussc::VideoGrabber::setDesiredFrameRate;
        t["getDesiredFrameRate"] = &trussc::VideoGrabber::getDesiredFrameRate;
        t["setVerbose"] = &trussc::VideoGrabber::setVerbose;
        t["isVerbose"] = &trussc::VideoGrabber::isVerbose;
        t["setup"] = sol::overload([](trussc::VideoGrabber& self) { return self.setup(); }, [](trussc::VideoGrabber& self, int width) { return self.setup(width); }, [](trussc::VideoGrabber& self, int width, int height) { return self.setup(width, height); });
        t["close"] = &trussc::VideoGrabber::close;
        t["update"] = &trussc::VideoGrabber::update;
        t["isFrameNew"] = &trussc::VideoGrabber::isFrameNew;
        t["isInitialized"] = &trussc::VideoGrabber::isInitialized;
        t["isPendingPermission"] = &trussc::VideoGrabber::isPendingPermission;
        t["getWidth"] = &trussc::VideoGrabber::getWidth;
        t["getHeight"] = &trussc::VideoGrabber::getHeight;
        t["getDeviceName"] = &trussc::VideoGrabber::getDeviceName;
        t["getPixels"] = [](trussc::VideoGrabber& self) { return self.getPixels(); };
        t["copyToImage"] = &trussc::VideoGrabber::copyToImage;
        t["getTexture"] = [](trussc::VideoGrabber& self) -> decltype(auto) { return self.getTexture(); };
        t["checkCameraPermission"] = &trussc::VideoGrabber::checkCameraPermission;
        t["requestCameraPermission"] = &trussc::VideoGrabber::requestCameraPermission;
    }
    {
        sol::usertype<trussc::VideoPlayer> t = lua->new_usertype<trussc::VideoPlayer>("VideoPlayer",
            sol::constructors<trussc::VideoPlayer()>());
        t["load"] = &trussc::VideoPlayer::load;
        t["close"] = &trussc::VideoPlayer::close;
        t["update"] = &trussc::VideoPlayer::update;
        t["draw"] = sol::overload([](trussc::VideoPlayer& self, float x, float y) { return self.draw(x, y); }, [](trussc::VideoPlayer& self, float x, float y, float w, float h) { return self.draw(x, y, w, h); });
        t["getDuration"] = &trussc::VideoPlayer::getDuration;
        t["getPosition"] = &trussc::VideoPlayer::getPosition;
        t["getCurrentFrame"] = &trussc::VideoPlayer::getCurrentFrame;
        t["getTotalFrames"] = &trussc::VideoPlayer::getTotalFrames;
        t["setFrame"] = &trussc::VideoPlayer::setFrame;
        t["nextFrame"] = &trussc::VideoPlayer::nextFrame;
        t["previousFrame"] = &trussc::VideoPlayer::previousFrame;
        t["setGammaCorrection"] = &trussc::VideoPlayer::setGammaCorrection;
        t["getGammaCorrection"] = &trussc::VideoPlayer::getGammaCorrection;
        t["setUseHwAccel"] = &trussc::VideoPlayer::setUseHwAccel;
        t["getUseHwAccel"] = &trussc::VideoPlayer::getUseHwAccel;
        t["getPixels"] = [](trussc::VideoPlayer& self) { return self.getPixels(); };
        t["getPixelsY"] = &trussc::VideoPlayer::getPixelsY;
        t["getPixelsUV"] = &trussc::VideoPlayer::getPixelsUV;
        t["hasAudio"] = &trussc::VideoPlayer::hasAudio;
        t["getAudioCodec"] = &trussc::VideoPlayer::getAudioCodec;
        t["getAudioData"] = &trussc::VideoPlayer::getAudioData;
        t["getAudioSampleRate"] = &trussc::VideoPlayer::getAudioSampleRate;
        t["getAudioChannels"] = &trussc::VideoPlayer::getAudioChannels;
        t["isUsingHwAccel"] = &trussc::VideoPlayer::isUsingHwAccel;
        t["getHwAccelName"] = &trussc::VideoPlayer::getHwAccelName;
    }
    {
        sol::usertype<trussc::VideoRecordSettings> t = lua->new_usertype<trussc::VideoRecordSettings>("VideoRecordSettings");
        t["codec"] = &trussc::VideoRecordSettings::codec;
        t["fps"] = &trussc::VideoRecordSettings::fps;
        t["bitrate"] = &trussc::VideoRecordSettings::bitrate;
        t["keyframeInterval"] = &trussc::VideoRecordSettings::keyframeInterval;
    }
    {
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter",
            sol::constructors<trussc::VideoWriter()>());
        t["open"] = sol::overload([](trussc::VideoWriter& self, const std::string & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const std::string & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
        t["close"] = &trussc::VideoWriter::close;
        t["isOpen"] = &trussc::VideoWriter::isOpen;
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
        t["getWidth"] = &trussc::VideoWriter::getWidth;
        t["getHeight"] = &trussc::VideoWriter::getHeight;
        t["getFps"] = &trussc::VideoWriter::getFps;
        t["getPath"] = &trussc::VideoWriter::getPath;
        t["getSettings"] = &trussc::VideoWriter::getSettings;
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
    }
    {
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder",
            sol::constructors<trussc::ScreenRecorder()>());
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const std::string & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const std::string & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const std::string & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const std::string & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); });
        t["stop"] = &trussc::ScreenRecorder::stop;
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
        t["getPath"] = &trussc::ScreenRecorder::getPath;
        t["writer"] = &trussc::ScreenRecorder::writer;
    }
    {
        sol::usertype<trussc::UdpReceiveEventArgs> t = lua->new_usertype<trussc::UdpReceiveEventArgs>("UdpReceiveEventArgs");
        t["data"] = &trussc::UdpReceiveEventArgs::data;
        t["remoteHost"] = &trussc::UdpReceiveEventArgs::remoteHost;
        t["remotePort"] = &trussc::UdpReceiveEventArgs::remotePort;
    }
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["message"] = &trussc::UdpErrorEventArgs::message;
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
    }
    {
        sol::usertype<trussc::UdpSocket> t = lua->new_usertype<trussc::UdpSocket>("UdpSocket",
            sol::constructors<trussc::UdpSocket()>());
        t["onReceive"] = &trussc::UdpSocket::onReceive;
        t["onError"] = &trussc::UdpSocket::onError;
        t["create"] = &trussc::UdpSocket::create;
        t["bind"] = sol::overload([](trussc::UdpSocket& self, int port) { return self.bind(port); }, [](trussc::UdpSocket& self, int port, bool startReceiving) { return self.bind(port, startReceiving); });
        t["connect"] = &trussc::UdpSocket::connect;
        t["close"] = &trussc::UdpSocket::close;
        t["sendTo"] = [](trussc::UdpSocket& self, const std::string & host, int port, const std::string & message) { return self.sendTo(host, port, message); };
        t["send"] = [](trussc::UdpSocket& self, const std::string & message) { return self.send(message); };
        t["startReceiving"] = &trussc::UdpSocket::startReceiving;
        t["stopReceiving"] = &trussc::UdpSocket::stopReceiving;
        t["isReceiving"] = &trussc::UdpSocket::isReceiving;
        t["setNonBlocking"] = &trussc::UdpSocket::setNonBlocking;
        t["setBroadcast"] = &trussc::UdpSocket::setBroadcast;
        t["setReuseAddress"] = &trussc::UdpSocket::setReuseAddress;
        t["setReusePort"] = &trussc::UdpSocket::setReusePort;
        t["joinMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.joinMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.joinMulticastGroup(groupAddr, interfaceAddr); });
        t["leaveMulticastGroup"] = sol::overload([](trussc::UdpSocket& self, const std::string & groupAddr) { return self.leaveMulticastGroup(groupAddr); }, [](trussc::UdpSocket& self, const std::string & groupAddr, const std::string & interfaceAddr) { return self.leaveMulticastGroup(groupAddr, interfaceAddr); });
        t["setMulticastTTL"] = &trussc::UdpSocket::setMulticastTTL;
        t["setMulticastLoopback"] = &trussc::UdpSocket::setMulticastLoopback;
        t["setMulticastInterface"] = &trussc::UdpSocket::setMulticastInterface;
        t["setReceiveBufferSize"] = &trussc::UdpSocket::setReceiveBufferSize;
        t["setSendBufferSize"] = &trussc::UdpSocket::setSendBufferSize;
        t["setReceiveTimeout"] = &trussc::UdpSocket::setReceiveTimeout;
        t["setUseThread"] = &trussc::UdpSocket::setUseThread;
        t["getLocalPort"] = &trussc::UdpSocket::getLocalPort;
        t["processNetwork"] = &trussc::UdpSocket::processNetwork;
        t["isValid"] = &trussc::UdpSocket::isValid;
        t["getConnectedHost"] = &trussc::UdpSocket::getConnectedHost;
        t["getConnectedPort"] = &trussc::UdpSocket::getConnectedPort;
    }
    {
        sol::usertype<trussc::TcpConnectEventArgs> t = lua->new_usertype<trussc::TcpConnectEventArgs>("TcpConnectEventArgs");
        t["success"] = &trussc::TcpConnectEventArgs::success;
        t["message"] = &trussc::TcpConnectEventArgs::message;
    }
    {
        sol::usertype<trussc::TcpReceiveEventArgs> t = lua->new_usertype<trussc::TcpReceiveEventArgs>("TcpReceiveEventArgs");
        t["data"] = &trussc::TcpReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::TcpDisconnectEventArgs> t = lua->new_usertype<trussc::TcpDisconnectEventArgs>("TcpDisconnectEventArgs");
        t["reason"] = &trussc::TcpDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::TcpErrorEventArgs> t = lua->new_usertype<trussc::TcpErrorEventArgs>("TcpErrorEventArgs");
        t["message"] = &trussc::TcpErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpErrorEventArgs::errorCode;
    }
    {
        sol::usertype<trussc::TcpClient> t = lua->new_usertype<trussc::TcpClient>("TcpClient",
            sol::constructors<trussc::TcpClient()>());
        t["onConnect"] = &trussc::TcpClient::onConnect;
        t["onReceive"] = &trussc::TcpClient::onReceive;
        t["onDisconnect"] = &trussc::TcpClient::onDisconnect;
        t["onError"] = &trussc::TcpClient::onError;
        t["connect"] = &trussc::TcpClient::connect;
        t["connectAsync"] = &trussc::TcpClient::connectAsync;
        t["disconnect"] = &trussc::TcpClient::disconnect;
        t["isConnected"] = &trussc::TcpClient::isConnected;
        t["send"] = sol::overload([](trussc::TcpClient& self, const std::vector<char> & data) { return self.send(data); }, [](trussc::TcpClient& self, const std::string & message) { return self.send(message); });
        t["setReceiveBufferSize"] = &trussc::TcpClient::setReceiveBufferSize;
        t["setBlocking"] = &trussc::TcpClient::setBlocking;
        t["setUseThread"] = &trussc::TcpClient::setUseThread;
        t["isUsingThread"] = &trussc::TcpClient::isUsingThread;
        t["processNetwork"] = &trussc::TcpClient::processNetwork;
        t["getRemoteHost"] = &trussc::TcpClient::getRemoteHost;
        t["getRemotePort"] = &trussc::TcpClient::getRemotePort;
    }
    {
        sol::usertype<trussc::TcpServerClient> t = lua->new_usertype<trussc::TcpServerClient>("TcpServerClient");
        t["getId"] = &trussc::TcpServerClient::getId;
        t["getHost"] = &trussc::TcpServerClient::getHost;
        t["getPort"] = &trussc::TcpServerClient::getPort;
    }
    {
        sol::usertype<trussc::TcpClientConnectEventArgs> t = lua->new_usertype<trussc::TcpClientConnectEventArgs>("TcpClientConnectEventArgs");
        t["clientId"] = &trussc::TcpClientConnectEventArgs::clientId;
        t["host"] = &trussc::TcpClientConnectEventArgs::host;
        t["port"] = &trussc::TcpClientConnectEventArgs::port;
    }
    {
        sol::usertype<trussc::TcpServerReceiveEventArgs> t = lua->new_usertype<trussc::TcpServerReceiveEventArgs>("TcpServerReceiveEventArgs");
        t["clientId"] = &trussc::TcpServerReceiveEventArgs::clientId;
        t["data"] = &trussc::TcpServerReceiveEventArgs::data;
    }
    {
        sol::usertype<trussc::TcpClientDisconnectEventArgs> t = lua->new_usertype<trussc::TcpClientDisconnectEventArgs>("TcpClientDisconnectEventArgs");
        t["clientId"] = &trussc::TcpClientDisconnectEventArgs::clientId;
        t["reason"] = &trussc::TcpClientDisconnectEventArgs::reason;
        t["wasClean"] = &trussc::TcpClientDisconnectEventArgs::wasClean;
    }
    {
        sol::usertype<trussc::TcpServerErrorEventArgs> t = lua->new_usertype<trussc::TcpServerErrorEventArgs>("TcpServerErrorEventArgs");
        t["message"] = &trussc::TcpServerErrorEventArgs::message;
        t["errorCode"] = &trussc::TcpServerErrorEventArgs::errorCode;
        t["clientId"] = &trussc::TcpServerErrorEventArgs::clientId;
    }
    {
        sol::usertype<trussc::TcpServer> t = lua->new_usertype<trussc::TcpServer>("TcpServer",
            sol::constructors<trussc::TcpServer()>());
        t["onClientConnect"] = &trussc::TcpServer::onClientConnect;
        t["onReceive"] = &trussc::TcpServer::onReceive;
        t["onClientDisconnect"] = &trussc::TcpServer::onClientDisconnect;
        t["onError"] = &trussc::TcpServer::onError;
        t["start"] = sol::overload([](trussc::TcpServer& self, int port) { return self.start(port); }, [](trussc::TcpServer& self, int port, int maxClients) { return self.start(port, maxClients); });
        t["stop"] = &trussc::TcpServer::stop;
        t["isRunning"] = &trussc::TcpServer::isRunning;
        t["disconnectClient"] = &trussc::TcpServer::disconnectClient;
        t["disconnectAllClients"] = &trussc::TcpServer::disconnectAllClients;
        t["getClientCount"] = &trussc::TcpServer::getClientCount;
        t["getClientIds"] = &trussc::TcpServer::getClientIds;
        t["getClient"] = &trussc::TcpServer::getClient;
        t["send"] = sol::overload([](trussc::TcpServer& self, int clientId, const std::vector<char> & data) { return self.send(clientId, data); }, [](trussc::TcpServer& self, int clientId, const std::string & message) { return self.send(clientId, message); });
        t["broadcast"] = sol::overload([](trussc::TcpServer& self, const std::vector<char> & data) { return self.broadcast(data); }, [](trussc::TcpServer& self, const std::string & message) { return self.broadcast(message); });
        t["setReceiveBufferSize"] = &trussc::TcpServer::setReceiveBufferSize;
        t["getPort"] = &trussc::TcpServer::getPort;
    }
    {
        sol::usertype<trussc::NetworkInterface> t = lua->new_usertype<trussc::NetworkInterface>("NetworkInterface");
        t["name"] = &trussc::NetworkInterface::name;
        t["address"] = &trussc::NetworkInterface::address;
        t["netmask"] = &trussc::NetworkInterface::netmask;
        t["mac"] = &trussc::NetworkInterface::mac;
        t["isIPv4"] = &trussc::NetworkInterface::isIPv4;
        t["isLoopback"] = &trussc::NetworkInterface::isLoopback;
        t["isUp"] = &trussc::NetworkInterface::isUp;
        t["getName"] = &trussc::NetworkInterface::getName;
        t["getAddress"] = &trussc::NetworkInterface::getAddress;
        t["getNetmask"] = &trussc::NetworkInterface::getNetmask;
        t["getMac"] = &trussc::NetworkInterface::getMac;
        t["getIsIPv4"] = &trussc::NetworkInterface::getIsIPv4;
        t["getIsLoopback"] = &trussc::NetworkInterface::getIsLoopback;
        t["getIsUp"] = &trussc::NetworkInterface::getIsUp;
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
        sol::usertype<trussc::ChipSoundNote> t = lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote",
            sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float, float)>());
        t["wave"] = &trussc::ChipSoundNote::wave;
        t["hz"] = &trussc::ChipSoundNote::hz;
        t["volume"] = &trussc::ChipSoundNote::volume;
        t["duration"] = &trussc::ChipSoundNote::duration;
        t["attack"] = &trussc::ChipSoundNote::attack;
        t["decay"] = &trussc::ChipSoundNote::decay;
        t["sustain"] = &trussc::ChipSoundNote::sustain;
        t["release"] = &trussc::ChipSoundNote::release;
        t["build"] = &trussc::ChipSoundNote::build;
        t["generateBuffer"] = &trussc::ChipSoundNote::generateBuffer;
        t["getTotalDuration"] = &trussc::ChipSoundNote::getTotalDuration;
    }
    {
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
        t["build"] = &trussc::ChipSoundBundle::build;
    }
    {
        sol::usertype<trussc::Tween<float>> t = lua->new_usertype<trussc::Tween<float>>("Tween_float",
            sol::constructors<trussc::Tween<float>(), trussc::Tween<float>(float, float, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec2>> t = lua->new_usertype<trussc::Tween<trussc::Vec2>>("Tween_Vec2",
            sol::constructors<trussc::Tween<trussc::Vec2>(), trussc::Tween<trussc::Vec2>(trussc::Vec2, trussc::Vec2, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::Tween<trussc::Vec3>> t = lua->new_usertype<trussc::Tween<trussc::Vec3>>("Tween_Vec3",
            sol::constructors<trussc::Tween<trussc::Vec3>(), trussc::Tween<trussc::Vec3>(trussc::Vec3, trussc::Vec3, float, trussc::EaseType, trussc::EaseMode)>());
    }
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
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
        sol::usertype<trussc::RectNodeButton> t = lua->new_usertype<trussc::RectNodeButton>("RectNodeButton",
            sol::constructors<trussc::RectNodeButton()>());
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["draw"] = &trussc::RectNodeButton::draw;
    }
    {
        sol::usertype<trussc::LayoutMod> t = lua->new_usertype<trussc::LayoutMod>("LayoutMod",
            sol::constructors<trussc::LayoutMod(trussc::LayoutDirection, float)>());
        t["getDirection"] = &trussc::LayoutMod::getDirection;
        t["setDirection"] = &trussc::LayoutMod::setDirection;
        t["getSpacing"] = &trussc::LayoutMod::getSpacing;
        t["setSpacing"] = &trussc::LayoutMod::setSpacing;
        t["getCrossAxis"] = &trussc::LayoutMod::getCrossAxis;
        t["setCrossAxis"] = &trussc::LayoutMod::setCrossAxis;
        t["getMainAxis"] = &trussc::LayoutMod::getMainAxis;
        t["setMainAxis"] = &trussc::LayoutMod::setMainAxis;
        t["getPaddingLeft"] = &trussc::LayoutMod::getPaddingLeft;
        t["getPaddingTop"] = &trussc::LayoutMod::getPaddingTop;
        t["getPaddingRight"] = &trussc::LayoutMod::getPaddingRight;
        t["getPaddingBottom"] = &trussc::LayoutMod::getPaddingBottom;
        t["setPadding"] = sol::overload([](trussc::LayoutMod& self, float padding) -> decltype(auto) { return self.setPadding(padding); }, [](trussc::LayoutMod& self, float vertical, float horizontal) -> decltype(auto) { return self.setPadding(vertical, horizontal); }, [](trussc::LayoutMod& self, float top, float right, float bottom, float left) -> decltype(auto) { return self.setPadding(top, right, bottom, left); });
        t["setPaddingLeft"] = &trussc::LayoutMod::setPaddingLeft;
        t["setPaddingTop"] = &trussc::LayoutMod::setPaddingTop;
        t["setPaddingRight"] = &trussc::LayoutMod::setPaddingRight;
        t["setPaddingBottom"] = &trussc::LayoutMod::setPaddingBottom;
        t["updateLayout"] = &trussc::LayoutMod::updateLayout;
    }
    {
        sol::usertype<trussc::ScrollContainer> t = lua->new_usertype<trussc::ScrollContainer>("ScrollContainer",
            sol::constructors<trussc::ScrollContainer()>());
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
        sol::usertype<trussc::ScrollBar> t = lua->new_usertype<trussc::ScrollBar>("ScrollBar");
        t["getBarColor"] = &trussc::ScrollBar::getBarColor;
        t["setBarColor"] = &trussc::ScrollBar::setBarColor;
        t["getBarWidth"] = &trussc::ScrollBar::getBarWidth;
        t["setBarWidth"] = &trussc::ScrollBar::setBarWidth;
        t["getMargin"] = &trussc::ScrollBar::getMargin;
        t["setMargin"] = &trussc::ScrollBar::setMargin;
        t["getOffset"] = &trussc::ScrollBar::getOffset;
        t["updateFromContainer"] = &trussc::ScrollBar::updateFromContainer;
    }
    {
        sol::usertype<trussc::TweenMod> t = lua->new_usertype<trussc::TweenMod>("TweenMod",
            sol::constructors<trussc::TweenMod()>());
        t["complete"] = &trussc::TweenMod::complete;
        t["moveTo"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveTo(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveTo(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveTo(pos); }, [](trussc::TweenMod& self, const trussc::Vec2 & pos) -> decltype(auto) { return self.moveTo(pos); });
        t["moveBy"] = sol::overload([](trussc::TweenMod& self, float dx, float dy) -> decltype(auto) { return self.moveBy(dx, dy); }, [](trussc::TweenMod& self, float dx, float dy, float dz) -> decltype(auto) { return self.moveBy(dx, dy, dz); }, [](trussc::TweenMod& self, const trussc::Vec3 & delta) -> decltype(auto) { return self.moveBy(delta); }, [](trussc::TweenMod& self, const trussc::Vec2 & delta) -> decltype(auto) { return self.moveBy(delta); });
        t["moveFrom"] = sol::overload([](trussc::TweenMod& self, float x, float y) -> decltype(auto) { return self.moveFrom(x, y); }, [](trussc::TweenMod& self, float x, float y, float z) -> decltype(auto) { return self.moveFrom(x, y, z); }, [](trussc::TweenMod& self, const trussc::Vec3 & pos) -> decltype(auto) { return self.moveFrom(pos); });
        t["scaleTo"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleTo(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleTo(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleTo(sx, sy, sz); }, [](trussc::TweenMod& self, const trussc::Vec3 & s) -> decltype(auto) { return self.scaleTo(s); });
        t["scaleBy"] = sol::overload([](trussc::TweenMod& self, float factor) -> decltype(auto) { return self.scaleBy(factor); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleBy(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleBy(sx, sy, sz); });
        t["scaleFrom"] = sol::overload([](trussc::TweenMod& self, float uniform) -> decltype(auto) { return self.scaleFrom(uniform); }, [](trussc::TweenMod& self, float sx, float sy) -> decltype(auto) { return self.scaleFrom(sx, sy); }, [](trussc::TweenMod& self, float sx, float sy, float sz) -> decltype(auto) { return self.scaleFrom(sx, sy, sz); });
        t["rotateTo"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateTo(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateTo(q); });
        t["rotateBy"] = &trussc::TweenMod::rotateBy;
        t["rotateFrom"] = sol::overload([](trussc::TweenMod& self, float radians) -> decltype(auto) { return self.rotateFrom(radians); }, [](trussc::TweenMod& self, const trussc::Quaternion & q) -> decltype(auto) { return self.rotateFrom(q); });
        t["rotateXTo"] = &trussc::TweenMod::rotateXTo;
        t["rotateXBy"] = &trussc::TweenMod::rotateXBy;
        t["rotateYTo"] = &trussc::TweenMod::rotateYTo;
        t["rotateYBy"] = &trussc::TweenMod::rotateYBy;
        t["rotateZTo"] = &trussc::TweenMod::rotateZTo;
        t["rotateZBy"] = &trussc::TweenMod::rotateZBy;
        t["rotateXFrom"] = &trussc::TweenMod::rotateXFrom;
        t["rotateYFrom"] = &trussc::TweenMod::rotateYFrom;
        t["duration"] = &trussc::TweenMod::duration;
        t["ease"] = sol::overload([](trussc::TweenMod& self, trussc::EaseType type) -> decltype(auto) { return self.ease(type); }, [](trussc::TweenMod& self, trussc::EaseType type, trussc::EaseMode mode) -> decltype(auto) { return self.ease(type, mode); });
        t["delay"] = &trussc::TweenMod::delay;
        t["start"] = &trussc::TweenMod::start;
        t["pause"] = &trussc::TweenMod::pause;
        t["resume"] = &trussc::TweenMod::resume;
        t["reset"] = &trussc::TweenMod::reset;
        t["isPlaying"] = &trussc::TweenMod::isPlaying;
        t["isComplete"] = &trussc::TweenMod::isComplete;
        t["getProgress"] = &trussc::TweenMod::getProgress;
        t["getDuration"] = &trussc::TweenMod::getDuration;
        t["getDelay"] = &trussc::TweenMod::getDelay;
        t["getEaseType"] = &trussc::TweenMod::getEaseType;
        t["getEaseMode"] = &trussc::TweenMod::getEaseMode;
    }
    {
        sol::usertype<trussc::App> t = lua->new_usertype<trussc::App>("App",
            sol::constructors<trussc::App()>());
        t["setSize"] = &trussc::App::setSize;
        t["requestExit"] = &trussc::App::requestExit;
        t["isExitRequested"] = &trussc::App::isExitRequested;
        t["keyPressed"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyPressed(e); }, [](trussc::App& self, int key) { return self.keyPressed(key); });
        t["keyReleased"] = sol::overload([](trussc::App& self, const trussc::KeyEventArgs & e) { return self.keyReleased(e); }, [](trussc::App& self, int key) { return self.keyReleased(key); });
        t["mousePressed"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mousePressed(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mousePressed(pos, button); });
        t["mouseReleased"] = sol::overload([](trussc::App& self, const trussc::MouseEventArgs & e) { return self.mouseReleased(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseReleased(pos, button); });
        t["mouseMoved"] = sol::overload([](trussc::App& self, const trussc::MouseMoveEventArgs & e) { return self.mouseMoved(e); }, [](trussc::App& self, trussc::Vec2 pos) { return self.mouseMoved(pos); });
        t["mouseDragged"] = sol::overload([](trussc::App& self, const trussc::MouseDragEventArgs & e) { return self.mouseDragged(e); }, [](trussc::App& self, trussc::Vec2 pos, int button) { return self.mouseDragged(pos, button); });
        t["mouseScrolled"] = sol::overload([](trussc::App& self, const trussc::ScrollEventArgs & e) { return self.mouseScrolled(e); }, [](trussc::App& self, trussc::Vec2 delta) { return self.mouseScrolled(delta); });
        t["touchPressed"] = &trussc::App::touchPressed;
        t["touchMoved"] = &trussc::App::touchMoved;
        t["touchReleased"] = &trussc::App::touchReleased;
        t["windowResized"] = &trussc::App::windowResized;
        t["filesDropped"] = &trussc::App::filesDropped;
        t["exit"] = &trussc::App::exit;
        t["audioOut"] = &trussc::App::audioOut;
        t["audioIn"] = &trussc::App::audioIn;
        t["handleKeyPressed"] = &trussc::App::handleKeyPressed;
        t["handleKeyReleased"] = &trussc::App::handleKeyReleased;
        t["handleMousePressed"] = &trussc::App::handleMousePressed;
        t["handleMouseReleased"] = &trussc::App::handleMouseReleased;
        t["handleMouseScrolled"] = &trussc::App::handleMouseScrolled;
        t["handleWindowResized"] = &trussc::App::handleWindowResized;
        t["handleUpdate"] = &trussc::App::handleUpdate;
        t["handleDraw"] = &trussc::App::handleDraw;
    }
    {
        sol::usertype<trussc::HeadlessSettings> t = lua->new_usertype<trussc::HeadlessSettings>("HeadlessSettings");
        t["targetFps"] = &trussc::HeadlessSettings::targetFps;
        t["setFps"] = &trussc::HeadlessSettings::setFps;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
