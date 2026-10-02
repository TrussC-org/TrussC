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
        sol::usertype<trussc::RectNodeButton> t = lua->new_usertype<trussc::RectNodeButton>("RectNodeButton",
            sol::constructors<trussc::RectNodeButton()>(),
            sol::call_constructor, sol::constructors<trussc::RectNodeButton()>());
        t["normalColor"] = &trussc::RectNodeButton::normalColor;
        t["hoverColor"] = &trussc::RectNodeButton::hoverColor;
        t["pressColor"] = &trussc::RectNodeButton::pressColor;
        t["label"] = &trussc::RectNodeButton::label;
        t["isPressed"] = &trussc::RectNodeButton::isPressed;
        t["draw"] = &trussc::RectNodeButton::draw;
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
    lua->new_usertype<trussc::TextureUsage>("TextureUsage",
        sol::meta_function::equal_to, [](trussc::TextureUsage a, trussc::TextureUsage b){ return a == b; },
        "Immutable", sol::var(trussc::TextureUsage::Immutable),
        "Dynamic", sol::var(trussc::TextureUsage::Dynamic),
        "Stream", sol::var(trussc::TextureUsage::Stream),
        "RenderTarget", sol::var(trussc::TextureUsage::RenderTarget));
    {
        sol::usertype<trussc::TcpClientConnectEventArgs> t = lua->new_usertype<trussc::TcpClientConnectEventArgs>("TcpClientConnectEventArgs");
        t["clientId"] = &trussc::TcpClientConnectEventArgs::clientId;
        t["host"] = &trussc::TcpClientConnectEventArgs::host;
        t["port"] = &trussc::TcpClientConnectEventArgs::port;
    }
    lua->new_usertype<trussc::StrokeCap>("StrokeCap",
        sol::meta_function::equal_to, [](trussc::StrokeCap a, trussc::StrokeCap b){ return a == b; },
        "Butt", sol::var(trussc::StrokeCap::Butt),
        "Round", sol::var(trussc::StrokeCap::Round),
        "Square", sol::var(trussc::StrokeCap::Square));
    lua->new_usertype<trussc::MixMode>("MixMode",
        sol::meta_function::equal_to, [](trussc::MixMode a, trussc::MixMode b){ return a == b; },
        "Auto", sol::var(trussc::MixMode::Auto),
        "DownmixMono", sol::var(trussc::MixMode::DownmixMono));
    {
        sol::usertype<trussc::Mod> t = lua->new_usertype<trussc::Mod>("Mod");
        t["getOwner"] = [](trussc::Mod& self) { return self.getOwner(); };
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
