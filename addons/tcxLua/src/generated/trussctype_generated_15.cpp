// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_15(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::LayoutMod> t = lua->new_usertype<trussc::LayoutMod>("LayoutMod",
            sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>(),
            sol::call_constructor, sol::constructors<trussc::LayoutMod(), trussc::LayoutMod(trussc::LayoutDirection), trussc::LayoutMod(trussc::LayoutDirection, float)>());
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
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter",
            sol::constructors<trussc::VideoWriter()>(),
            sol::call_constructor, sol::constructors<trussc::VideoWriter()>());
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
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
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
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
        sol::usertype<trussc::ChipSoundBundle> t = lua->new_usertype<trussc::ChipSoundBundle>("ChipSoundBundle");
        t["entries"] = &trussc::ChipSoundBundle::entries;
        t["volume"] = &trussc::ChipSoundBundle::volume;
        t["add"] = sol::overload([](trussc::ChipSoundBundle& self, const trussc::ChipSoundNote & note, float time) -> decltype(auto) { return self.add(note, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time) -> decltype(auto) { return self.add(wave, hz, duration, time); }, [](trussc::ChipSoundBundle& self, trussc::ChipSoundNote::Wave wave, float hz, float duration, float time, float vol) -> decltype(auto) { return self.add(wave, hz, duration, time, vol); });
        t["clear"] = &trussc::ChipSoundBundle::clear;
        t["getDuration"] = &trussc::ChipSoundBundle::getDuration;
        t["build"] = &trussc::ChipSoundBundle::build;
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
        sol::usertype<trussc::EventListener> t = lua->new_usertype<trussc::EventListener>("EventListener",
            sol::constructors<trussc::EventListener()>(),
            sol::call_constructor, sol::constructors<trussc::EventListener()>());
        t["disconnect"] = &trussc::EventListener::disconnect;
        t["isConnected"] = &trussc::EventListener::isConnected;
    }
    lua->new_usertype<trussc::TcyMode>("TcyMode",
        sol::meta_function::equal_to, [](trussc::TcyMode a, trussc::TcyMode b){ return a == b; },
        "Rotate", sol::var(trussc::TcyMode::Rotate),
        "Upright", sol::var(trussc::TcyMode::Upright),
        "Combine", sol::var(trussc::TcyMode::Combine));
    {
        sol::usertype<trussc::AudioRecordSettings> t = lua->new_usertype<trussc::AudioRecordSettings>("AudioRecordSettings");
        t["format"] = &trussc::AudioRecordSettings::format;
        t["channelMap"] = &trussc::AudioRecordSettings::channelMap;
    }
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
