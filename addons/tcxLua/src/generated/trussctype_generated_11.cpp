// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_11(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
    {
        sol::usertype<trussc::VideoGrabber> t = lua->new_usertype<trussc::VideoGrabber>("VideoGrabber",
            sol::constructors<trussc::VideoGrabber()>(),
            sol::call_constructor, sol::constructors<trussc::VideoGrabber()>());
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
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["setFrameQueueSize"] = &trussc::VideoGrabber::setFrameQueueSize;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
        t["getFrameQueueSize"] = &trussc::VideoGrabber::getFrameQueueSize;
#endif
        t["copyToImage"] = &trussc::VideoGrabber::copyToImage;
        t["getTexture"] = [](trussc::VideoGrabber& self) -> decltype(auto) { return self.getTexture(); };
        t["checkCameraPermission"] = &trussc::VideoGrabber::checkCameraPermission;
        t["requestCameraPermission"] = &trussc::VideoGrabber::requestCameraPermission;
    }
#endif
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
        sol::usertype<trussc::ColorOKLCH> t = lua->new_usertype<trussc::ColorOKLCH>("ColorOKLCH",
            sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLCH(), trussc::ColorOKLCH(float, float, float), trussc::ColorOKLCH(float, float, float, float)>());
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
        sol::usertype<trussc::AudioRecorder> t = lua->new_usertype<trussc::AudioRecorder>("AudioRecorder",
            sol::constructors<trussc::AudioRecorder()>(),
            sol::call_constructor, sol::constructors<trussc::AudioRecorder()>());
        t["start"] = sol::overload([](trussc::AudioRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::AudioRecorder& self, const fs::path & path, const trussc::AudioRecordSettings & settings) { return self.start(path, settings); });
        t["stop"] = &trussc::AudioRecorder::stop;
        t["isRecording"] = &trussc::AudioRecorder::isRecording;
        t["getRecordedSeconds"] = &trussc::AudioRecorder::getRecordedSeconds;
        t["getDroppedFrames"] = &trussc::AudioRecorder::getDroppedFrames;
        t["getPath"] = &trussc::AudioRecorder::getPath;
    }
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
        sol::usertype<trussc::TouchEventArgs> t = lua->new_usertype<trussc::TouchEventArgs>("TouchEventArgs");
        t["numTouches"] = &trussc::TouchEventArgs::numTouches;
        t["cancelled"] = &trussc::TouchEventArgs::cancelled;
        t["x"] = &trussc::TouchEventArgs::x;
        t["y"] = &trussc::TouchEventArgs::y;
        t["id"] = &trussc::TouchEventArgs::id;
    }
    {
        sol::usertype<trussc::Location> t = lua->new_usertype<trussc::Location>("Location");
        t["latitude"] = &trussc::Location::latitude;
        t["longitude"] = &trussc::Location::longitude;
        t["altitude"] = &trussc::Location::altitude;
        t["accuracy"] = &trussc::Location::accuracy;
    }
    {
        sol::usertype<trussc::CurveStyle> t = lua->new_usertype<trussc::CurveStyle>("CurveStyle");
        t["mode"] = &trussc::CurveStyle::mode;
        t["tolerance"] = &trussc::CurveStyle::tolerance;
        t["resolution"] = &trussc::CurveStyle::resolution;
    }
    {
        sol::usertype<trussc::ExitRequestEventArgs> t = lua->new_usertype<trussc::ExitRequestEventArgs>("ExitRequestEventArgs");
        t["cancel"] = &trussc::ExitRequestEventArgs::cancel;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
