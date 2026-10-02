// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_14(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::Vec3> t = lua->new_usertype<trussc::Vec3>("Vec3",
            sol::constructors<trussc::Vec3(), trussc::Vec3(float, float, float), trussc::Vec3(float), trussc::Vec3(const trussc::Vec2 &), trussc::Vec3(const trussc::Vec2 &, float)>(),
            sol::call_constructor, sol::constructors<trussc::Vec3(), trussc::Vec3(float, float, float), trussc::Vec3(float), trussc::Vec3(const trussc::Vec2 &), trussc::Vec3(const trussc::Vec2 &, float)>(),
            sol::meta_function::index, [](const trussc::Vec3& a, int b){ return a[b]; },
            sol::meta_function::addition, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a + b; },
            sol::meta_function::subtraction, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a - b; },
            sol::meta_function::unary_minus, [](const trussc::Vec3& a){ return -a; },
            sol::meta_function::multiplication, sol::overload([](const trussc::Vec3& a, float b){ return a * b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a * b; }),
            sol::meta_function::division, sol::overload([](const trussc::Vec3& a, float b){ return a / b; }, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a / b; }),
            sol::meta_function::equal_to, [](const trussc::Vec3& a, const trussc::Vec3 & b){ return a == b; });
        t["x"] = &trussc::Vec3::x;
        t["y"] = &trussc::Vec3::y;
        t["z"] = &trussc::Vec3::z;
        t["set"] = sol::overload([](trussc::Vec3& self, float x_, float y_, float z_) -> decltype(auto) { return self.set(x_, y_, z_); }, [](trussc::Vec3& self, const trussc::Vec3 & v) -> decltype(auto) { return self.set(v); });
        t["length"] = &trussc::Vec3::length;
        t["lengthSquared"] = &trussc::Vec3::lengthSquared;
        t["normalized"] = &trussc::Vec3::normalized;
        t["normalize"] = &trussc::Vec3::normalize;
        t["limit"] = &trussc::Vec3::limit;
        t["dot"] = &trussc::Vec3::dot;
        t["cross"] = &trussc::Vec3::cross;
        t["distance"] = &trussc::Vec3::distance;
        t["distanceSquared"] = &trussc::Vec3::distanceSquared;
        t["lerp"] = &trussc::Vec3::lerp;
        t["reflected"] = &trussc::Vec3::reflected;
        t["xy"] = &trussc::Vec3::xy;
    }
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    {
        sol::usertype<trussc::Window> t = lua->new_usertype<trussc::Window>("Window",
            sol::constructors<trussc::Window()>(),
            sol::call_constructor, sol::constructors<trussc::Window()>());
        t["setApp"] = &trussc::Window::setApp;
        t["getApp"] = &trussc::Window::getApp;
        t["events"] = &trussc::Window::events;
        t["close"] = &trussc::Window::close;
        t["isOpen"] = &trussc::Window::isOpen;
        t["setTitle"] = &trussc::Window::setTitle;
        t["getTitle"] = &trussc::Window::getTitle;
        t["getWidth"] = &trussc::Window::getWidth;
        t["getHeight"] = &trussc::Window::getHeight;
        t["isOccluded"] = &trussc::Window::isOccluded;
        t["setSize"] = &trussc::Window::setSize;
        t["setFullscreen"] = &trussc::Window::setFullscreen;
        t["isFullscreen"] = &trussc::Window::isFullscreen;
        t["toggleFullscreen"] = &trussc::Window::toggleFullscreen;
        t["setClearColor"] = &trussc::Window::setClearColor;
        t["setFps"] = &trussc::Window::setFps;
        t["getFps"] = &trussc::Window::getFps;
        t["dispatchMousePressToTree"] = &trussc::Window::dispatchMousePressToTree;
        t["dispatchMouseReleaseToTree"] = &trussc::Window::dispatchMouseReleaseToTree;
        t["dispatchMouseScrollToTree"] = &trussc::Window::dispatchMouseScrollToTree;
        t["dispatchKeyPressToTree"] = &trussc::Window::dispatchKeyPressToTree;
        t["dispatchKeyReleaseToTree"] = &trussc::Window::dispatchKeyReleaseToTree;
        t["tickTree"] = &trussc::Window::tickTree;
        t["drawTreeNow"] = &trussc::Window::drawTreeNow;
        t["syncRootSize"] = &trussc::Window::syncRootSize;
    }
#endif
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
        sol::usertype<trussc::ColorOKLab> t = lua->new_usertype<trussc::ColorOKLab>("ColorOKLab",
            sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ColorOKLab(), trussc::ColorOKLab(float, float, float), trussc::ColorOKLab(float, float, float, float)>());
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
    lua->new_usertype<trussc::Orientation>("Orientation",
        sol::meta_function::equal_to, [](trussc::Orientation a, trussc::Orientation b){ return a == b; },
        "Portrait", sol::var(trussc::Orientation::Portrait),
        "PortraitUpsideDown", sol::var(trussc::Orientation::PortraitUpsideDown),
        "LandscapeLeft", sol::var(trussc::Orientation::LandscapeLeft),
        "LandscapeRight", sol::var(trussc::Orientation::LandscapeRight),
        "Landscape", sol::var(trussc::Orientation::Landscape),
        "All", sol::var(trussc::Orientation::All),
        "AllButUpsideDown", sol::var(trussc::Orientation::AllButUpsideDown));
    lua->new_usertype<trussc::SendError>("SendError",
        sol::meta_function::equal_to, [](trussc::SendError a, trussc::SendError b){ return a == b; },
        "None", sol::var(trussc::SendError::None),
        "ClientNotFound", sol::var(trussc::SendError::ClientNotFound),
        "Disconnected", sol::var(trussc::SendError::Disconnected),
        "Timeout", sol::var(trussc::SendError::Timeout),
        "QueueFull", sol::var(trussc::SendError::QueueFull),
        "NotRunning", sol::var(trussc::SendError::NotRunning));
    {
        sol::usertype<trussc::JsonWriteReflector> t = lua->new_usertype<trussc::JsonWriteReflector>("JsonWriteReflector");
        t["members"] = &trussc::JsonWriteReflector::members;
        t["includeDerived"] = &trussc::JsonWriteReflector::includeDerived;
        t["derived"] = &trussc::JsonWriteReflector::derived;
        t["endGroup"] = &trussc::JsonWriteReflector::endGroup;
    }
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
    {
        sol::usertype<trussc::UdpErrorEventArgs> t = lua->new_usertype<trussc::UdpErrorEventArgs>("UdpErrorEventArgs");
        t["message"] = &trussc::UdpErrorEventArgs::message;
        t["errorCode"] = &trussc::UdpErrorEventArgs::errorCode;
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
