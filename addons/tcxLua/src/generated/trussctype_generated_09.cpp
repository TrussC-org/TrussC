// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma clang diagnostic push
#endif
void tcxLuaGenShard_09(const std::shared_ptr<sol::state>& lua) {
    {
        sol::usertype<trussc::App> t = lua->new_usertype<trussc::App>("App",
            sol::constructors<trussc::App()>(),
            sol::call_constructor, sol::constructors<trussc::App()>());
        t["setSize"] = &trussc::App::setSize;
        t["requestExit"] = &trussc::App::requestExit;
        t["isExitRequested"] = &trussc::App::isExitRequested;
        t["getWindow"] = &trussc::App::getWindow;
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
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder",
            sol::constructors<trussc::ScreenRecorder()>(),
            sol::call_constructor, sol::constructors<trussc::ScreenRecorder()>());
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
        t["stop"] = &trussc::ScreenRecorder::stop;
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
        t["getPath"] = &trussc::ScreenRecorder::getPath;
        t["writer"] = &trussc::ScreenRecorder::writer;
    }
#endif
    {
        sol::usertype<trussc::ChipSoundNote> t = lua->new_usertype<trussc::ChipSoundNote>("ChipSoundNote",
            sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>(),
            sol::call_constructor, sol::constructors<trussc::ChipSoundNote(), trussc::ChipSoundNote(trussc::Wave, float, float), trussc::ChipSoundNote(trussc::Wave, float, float, float)>());
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
        sol::usertype<trussc::AudioStats> t = lua->new_usertype<trussc::AudioStats>("AudioStats");
        t["droppedPlays"] = &trussc::AudioStats::droppedPlays;
        t["droppedPolyphonyLimit"] = &trussc::AudioStats::droppedPolyphonyLimit;
        t["droppedStreamLimit"] = &trussc::AudioStats::droppedStreamLimit;
        t["droppedDecoderError"] = &trussc::AudioStats::droppedDecoderError;
        t["droppedNotRunning"] = &trussc::AudioStats::droppedNotRunning;
        t["clippedSamples"] = &trussc::AudioStats::clippedSamples;
        t["peak"] = &trussc::AudioStats::peak;
        t["rms"] = &trussc::AudioStats::rms;
        t["cpuUsage"] = &trussc::AudioStats::cpuUsage;
        t["cpuUsagePeak"] = &trussc::AudioStats::cpuUsagePeak;
    }
    {
        sol::usertype<trussc::SoundStream> t = lua->new_usertype<trussc::SoundStream>("SoundStream",
            sol::constructors<trussc::SoundStream()>(),
            sol::call_constructor, sol::constructors<trussc::SoundStream()>());
        t["loadStream"] = sol::overload([](trussc::SoundStream& self, const fs::path & path) { return self.loadStream(path); }, [](trussc::SoundStream& self, const fs::path & path, int maxPolyphony) { return self.loadStream(path, maxPolyphony); });
        t["getDuration"] = &trussc::SoundStream::getDuration;
        t["getPath"] = &trussc::SoundStream::getPath;
        t["getMaxPolyphony"] = &trussc::SoundStream::getMaxPolyphony;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["success"] = &trussc::LoadResult::success;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
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
        sol::usertype<trussc::FpsSettings> t = lua->new_usertype<trussc::FpsSettings>("FpsSettings");
        t["updateFps"] = &trussc::FpsSettings::updateFps;
        t["drawFps"] = &trussc::FpsSettings::drawFps;
        t["actualVsyncFps"] = &trussc::FpsSettings::actualVsyncFps;
        t["synced"] = &trussc::FpsSettings::synced;
    }
    lua->new_usertype<trussc::StrokeJoin>("StrokeJoin",
        sol::meta_function::equal_to, [](trussc::StrokeJoin a, trussc::StrokeJoin b){ return a == b; },
        "Miter", sol::var(trussc::StrokeJoin::Miter),
        "Round", sol::var(trussc::StrokeJoin::Round),
        "Bevel", sol::var(trussc::StrokeJoin::Bevel));
    {
        sol::usertype<trussc::SendResult> t = lua->new_usertype<trussc::SendResult>("SendResult");
        t["error"] = &trussc::SendResult::error;
        t["id"] = &trussc::SendResult::id;
        t["ok"] = &trussc::SendResult::ok;
    }
    lua->new_usertype<trussc::Codec>("Codec",
        sol::meta_function::equal_to, [](trussc::Codec a, trussc::Codec b){ return a == b; },
        "None", sol::var(trussc::Codec::None),
        "LZ4", sol::var(trussc::Codec::LZ4));
}
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
