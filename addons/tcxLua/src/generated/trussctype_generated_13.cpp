// AUTO-GENERATED usertype bindings from reference-data.json by luagen-types.js
#include "tcxLua.h"
#include "TrussC.h"
using namespace trussc;
using namespace std;
// Abstract bases (e.g. VideoPlayerBase) expose members but cannot be constructed.
// An empty guarded constructor list must not enable sol2's automatic default ctor.
namespace {
template<typename Owner, typename... Signatures>
auto tcxLuaConstructors() {
    if constexpr (std::is_abstract_v<Owner> || sizeof...(Signatures) == 0) return sol::no_constructor;
    else return sol::constructors<Signatures...>();
}
}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma clang diagnostic push
// Lua retains deprecated compatibility names, including inherited ones.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
void tcxLuaGenShard_13(const std::shared_ptr<sol::state>& lua) {
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::VideoWriter
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::VideoWriter()
#endif
        >();
        sol::usertype<trussc::VideoWriter> t = lua->new_usertype<trussc::VideoWriter>("VideoWriter", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrame"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo) { return self.addFrame(fbo); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels) { return self.addFrame(pixels); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["addFrameAt"] = sol::overload([](trussc::VideoWriter& self, const trussc::Fbo & fbo, double timeSec) { return self.addFrameAt(fbo, timeSec); }, [](trussc::VideoWriter& self, const trussc::Pixels & pixels, double timeSec) { return self.addFrameAt(pixels, timeSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["close"] = &trussc::VideoWriter::close;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFps"] = &trussc::VideoWriter::getFps;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::VideoWriter::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getHeight"] = &trussc::VideoWriter::getHeight;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::VideoWriter::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::VideoWriter::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getSettings"] = &trussc::VideoWriter::getSettings;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getWidth"] = &trussc::VideoWriter::getWidth;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isOpen"] = &trussc::VideoWriter::isOpen;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["open"] = sol::overload([](trussc::VideoWriter& self, const fs::path & path, int width, int height) { return self.open(path, width, height); }, [](trussc::VideoWriter& self, const fs::path & path, int width, int height, const trussc::VideoRecordSettings & settings) { return self.open(path, width, height, settings); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE))
        t["submitFrame"] = &trussc::VideoWriter::submitFrame;
#endif
    }
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    {
        auto constructors = tcxLuaConstructors<trussc::ScreenRecorder
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
            , trussc::ScreenRecorder()
#endif
        >();
        sol::usertype<trussc::ScreenRecorder> t = lua->new_usertype<trussc::ScreenRecorder>("ScreenRecorder", constructors, sol::call_constructor, constructors);
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getFrameCount"] = &trussc::ScreenRecorder::getFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getPath"] = &trussc::ScreenRecorder::getPath;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["getRejectedFrameCount"] = &trussc::ScreenRecorder::getRejectedFrameCount;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["isRecording"] = &trussc::ScreenRecorder::isRecording;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["start"] = sol::overload([](trussc::ScreenRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::ScreenRecorder& self, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(path, settings); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path) { return self.start(fbo, path); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, const trussc::VideoRecordSettings & settings) { return self.start(fbo, path, settings); }, [](trussc::ScreenRecorder& self, const fs::path & path, float durationSec) { return self.start(path, durationSec); }, [](trussc::ScreenRecorder& self, const trussc::Fbo & fbo, const fs::path & path, float durationSec) { return self.start(fbo, path, durationSec); });
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["stop"] = &trussc::ScreenRecorder::stop;
#endif
#if (defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)) || defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        t["writer"] = &trussc::ScreenRecorder::writer;
#endif
    }
#endif
    {
        auto constructors = tcxLuaConstructors<trussc::FullscreenShader
            , trussc::FullscreenShader()
        >();
        sol::usertype<trussc::FullscreenShader> t = lua->new_usertype<trussc::FullscreenShader>("FullscreenShader", constructors, sol::call_constructor, constructors);
        t["begin"] = &trussc::FullscreenShader::begin;
        t["clear"] = &trussc::FullscreenShader::clear;
        t["draw"] = &trussc::FullscreenShader::draw;
        t["drawVertices"] = sol::overload([](trussc::FullscreenShader& self, int vertexCount) { return self.drawVertices(vertexCount); }, [](trussc::FullscreenShader& self, int vertexCount, int instanceCount) { return self.drawVertices(vertexCount, instanceCount); });
        t["end"] = &trussc::FullscreenShader::end;
        t["isLoaded"] = &trussc::FullscreenShader::isLoaded;
        t["load"] = &trussc::FullscreenShader::load;
        t["setTexture"] = sol::overload([](trussc::FullscreenShader& self, int slot, sg_image image, sg_sampler sampler) { return self.setTexture(slot, image, sampler); }, [](trussc::FullscreenShader& self, int slot, sg_view view, sg_sampler sampler) { return self.setTexture(slot, view, sampler); });
        t["setUniform"] = sol::overload([](trussc::FullscreenShader& self, int slot, float value) { return self.setUniform(slot, value); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec2 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec3 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Vec4 & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const trussc::Color & c) { return self.setUniform(slot, c); }, [](trussc::FullscreenShader& self, int slot, const std::vector<float> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec2> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec3> & v) { return self.setUniform(slot, v); }, [](trussc::FullscreenShader& self, int slot, const std::vector<trussc::Vec4> & v) { return self.setUniform(slot, v); });
    }
    {
        sol::usertype<trussc::MouseDragEventArgs> t = lua->new_usertype<trussc::MouseDragEventArgs>("MouseDragEventArgs");
        t["alt"] = &trussc::MouseDragEventArgs::alt;
        t["button"] = &trussc::MouseDragEventArgs::button;
        t["consumed"] = &trussc::MouseDragEventArgs::consumed;
        t["ctrl"] = &trussc::MouseDragEventArgs::ctrl;
        t["delta"] = &trussc::MouseDragEventArgs::delta;
        t["deltaX"] = &trussc::MouseDragEventArgs::deltaX;
        t["deltaY"] = &trussc::MouseDragEventArgs::deltaY;
        t["globalDelta"] = &trussc::MouseDragEventArgs::globalDelta;
        t["globalPos"] = &trussc::MouseDragEventArgs::globalPos;
        t["injected"] = &trussc::MouseDragEventArgs::injected;
        t["pos"] = &trussc::MouseDragEventArgs::pos;
        t["shift"] = &trussc::MouseDragEventArgs::shift;
        t["sourceTag"] = &trussc::MouseDragEventArgs::sourceTag;
        t["super"] = &trussc::MouseDragEventArgs::super;
        t["x"] = &trussc::MouseDragEventArgs::x;
        t["y"] = &trussc::MouseDragEventArgs::y;
        t["syncLegacy"] = &trussc::MouseDragEventArgs::syncLegacy;
    }
    {
        auto constructors = tcxLuaConstructors<trussc::AudioRecorder
            , trussc::AudioRecorder()
        >();
        sol::usertype<trussc::AudioRecorder> t = lua->new_usertype<trussc::AudioRecorder>("AudioRecorder", constructors, sol::call_constructor, constructors);
        t["getDroppedFrames"] = &trussc::AudioRecorder::getDroppedFrames;
        t["getPath"] = &trussc::AudioRecorder::getPath;
        t["getRecordedSeconds"] = &trussc::AudioRecorder::getRecordedSeconds;
        t["isRecording"] = &trussc::AudioRecorder::isRecording;
        t["start"] = sol::overload([](trussc::AudioRecorder& self, const fs::path & path) { return self.start(path); }, [](trussc::AudioRecorder& self, const fs::path & path, const trussc::AudioRecordSettings & settings) { return self.start(path, settings); });
        t["stop"] = &trussc::AudioRecorder::stop;
    }
    lua->new_usertype<trussc::ChipSoundNote::Wave>("ChipSoundNoteWave",
        sol::meta_function::equal_to, [](trussc::ChipSoundNote::Wave a, trussc::ChipSoundNote::Wave b){ return a == b; },
        "Sin", sol::var(trussc::ChipSoundNote::Wave::Sin),
        "Square", sol::var(trussc::ChipSoundNote::Wave::Square),
        "Triangle", sol::var(trussc::ChipSoundNote::Wave::Triangle),
        "Sawtooth", sol::var(trussc::ChipSoundNote::Wave::Sawtooth),
        "Noise", sol::var(trussc::ChipSoundNote::Wave::Noise),
        "PinkNoise", sol::var(trussc::ChipSoundNote::Wave::PinkNoise),
        "Silent", sol::var(trussc::ChipSoundNote::Wave::Silent));
    {
        sol::usertype<trussc::Font::PlacedGlyph> t = lua->new_usertype<trussc::Font::PlacedGlyph>("FontPlacedGlyph");
        t["baselineY"] = &trussc::Font::PlacedGlyph::baselineY;
        t["codepoint"] = &trussc::Font::PlacedGlyph::codepoint;
        t["drawX"] = &trussc::Font::PlacedGlyph::drawX;
        t["pivotX"] = &trussc::Font::PlacedGlyph::pivotX;
        t["pivotY"] = &trussc::Font::PlacedGlyph::pivotY;
        t["rotationCw"] = &trussc::Font::PlacedGlyph::rotationCw;
        t["scaleX"] = &trussc::Font::PlacedGlyph::scaleX;
    }
    {
        sol::usertype<trussc::LoadResult> t = lua->new_usertype<trussc::LoadResult>("LoadResult");
        t["error"] = &trussc::LoadResult::error;
        t["message"] = &trussc::LoadResult::message;
        t["ok"] = &trussc::LoadResult::ok;
        t["fail"] = sol::overload([](trussc::LoadError e) { return trussc::LoadResult::fail(e); }, [](trussc::LoadError e, std::string msg) { return trussc::LoadResult::fail(e, msg); });
        t["success"] = &trussc::LoadResult::success;
    }
    lua->new_usertype<trussc::WindowType>("WindowType",
        sol::meta_function::equal_to, [](trussc::WindowType a, trussc::WindowType b){ return a == b; },
        "Rect", sol::var(trussc::WindowType::Rect),
        "Hanning", sol::var(trussc::WindowType::Hanning),
        "Hamming", sol::var(trussc::WindowType::Hamming),
        "Blackman", sol::var(trussc::WindowType::Blackman));
    {
        sol::usertype<trussc::InputModifiers> t = lua->new_usertype<trussc::InputModifiers>("InputModifiers");
        t["alt"] = &trussc::InputModifiers::alt;
        t["ctrl"] = &trussc::InputModifiers::ctrl;
        t["shift"] = &trussc::InputModifiers::shift;
        t["super"] = &trussc::InputModifiers::super;
    }
    lua->new_usertype<trussc::SoundStream::Kind>("SoundStreamKind",
        sol::meta_function::equal_to, [](trussc::SoundStream::Kind a, trussc::SoundStream::Kind b){ return a == b; },
        "Eager", sol::var(trussc::SoundStream::Kind::Eager),
        "Stream", sol::var(trussc::SoundStream::Kind::Stream));
    {
        sol::usertype<trussc::DeviceLostEventArgs> t = lua->new_usertype<trussc::DeviceLostEventArgs>("DeviceLostEventArgs");
        t["cancel"] = &trussc::DeviceLostEventArgs::cancel;
        t["reason"] = &trussc::DeviceLostEventArgs::reason;
    }
    {
        sol::usertype<trussc::ConsoleEventArgs> t = lua->new_usertype<trussc::ConsoleEventArgs>("ConsoleEventArgs");
        t["args"] = &trussc::ConsoleEventArgs::args;
        t["raw"] = &trussc::ConsoleEventArgs::raw;
    }
    {
        sol::usertype<trussc::EnumLabelSpan> t = lua->new_usertype<trussc::EnumLabelSpan>("EnumLabelSpan");
        t["count"] = &trussc::EnumLabelSpan::count;
    }
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif
