// =============================================================================
// core/tests/soundVoiceLifetime — behavioral regression test for #281: a
// Sound plays only while it, or a copy of it, is alive.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
// The engine runs on miniaudio's null backend
// (AudioSettings::backend = AudioBackend::Null), a device-less clock that still
// drives the real mixer callback, so no sound card is needed. Voices are
// counted with AudioEngine::getPlayingSounds().
//
// Guards the invariants:
// - maxPolyphony + 8 scoped looping Sounds each play, release their voice
//   at the end of their scope, and a new Sound plays afterwards.
// - A scoped copy does not stop the original; the voice stops when the last
//   handle that shares it goes away.
// - A scoped one-shot stops when its scope ends.
// - Copy assignment and move assignment release the old voice; a move
//   carries the voice over to the target, which keeps playing.
// - A paused voice is released too.
// - stop() releases the voice for every copy that shares it.
// - A streamed voice closes its file when it is stopped or when its last
//   handle goes away (checked on Linux through /proc/self/fd), and a
//   stream with maxPolyphony 1 plays again from a new scoped copy each time.
// - Later play() calls fill every voice slot.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifdef __linux__
#include <climits>
#include <dirent.h>
#include <unistd.h>
#endif

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

constexpr int kRate = 48000;
constexpr int kPolyphony = 8;

static size_t voices() {
    const AudioEngine& engine = AudioEngine::getInstance();
    return engine.getPlayingSounds().size();
}

// A looping test tone, silent so the mix stays quiet.
static Sound makeLoop(float freq = 440.0f) {
    Sound s;
    s.loadTestTone(freq, 1.0f);
    s.setLoop(true);
    s.setVolume(0.0f);
    return s;
}

// 16-bit stereo PCM WAV of silence.
static bool writeWav(const fs::path& path, float seconds) {
    const uint32_t frames = (uint32_t)(seconds * kRate);
    const uint32_t dataBytes = frames * 4;
    ofstream f(path, ios::binary | ios::trunc);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(2); u32((uint32_t)kRate); u32((uint32_t)kRate * 4);
    u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) u32(0);
    return (bool)f;
}

#ifdef __linux__
// Number of this process's file descriptors open on `path`.
static int openHandles(const fs::path& path) {
    std::error_code ec;
    const fs::path target = fs::canonical(path, ec);
    if (ec) return -1;
    DIR* d = opendir("/proc/self/fd");
    if (!d) return -1;
    int n = 0;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const string link = string("/proc/self/fd/") + e->d_name;
        char buf[PATH_MAX];
        const ssize_t len = readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (len <= 0) continue;
        buf[len] = '\0';
        if (target == fs::path(buf)) ++n;
    }
    closedir(d);
    return n;
}
#endif

} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();   // this thread is the main thread

    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.backend = AudioBackend::Null;
    settings.sampleRate = kRate;
    settings.channels = 2;
    settings.bufferSize = 256;
    settings.maxPolyphony = kPolyphony;
    if (!engine.init(settings)) {
        printf("SKIP: the audio engine does not start on the null backend here\n");
        return 0;
    }

    // --- Scoped looping Sounds release their voices ---------------------------
    {
        int played = 0;
        for (int i = 0; i < kPolyphony + 8; ++i) {
            Sound s = makeLoop(440.0f + i);
            if (s.play()) ++played;
        }
        check("scoped loops: each of maxPolyphony + 8 plays", played == kPolyphony + 8,
              to_string(played));
        check("scoped loops: no voice is left", voices() == 0, to_string(voices()));
        Sound bgm = makeLoop();
        check("scoped loops: a new Sound plays afterwards", bgm.play() && bgm.isPlaying());
    }
    check("a Sound going out of scope releases its voice", voices() == 0, to_string(voices()));

    // --- Copies share the voice -----------------------------------------------
    {
        Sound a = makeLoop();
        a.play();
        { Sound b = a; }
        check("copy: a scoped copy does not stop the original", a.isPlaying());
        check("copy: one voice", voices() == 1, to_string(voices()));

        Sound* c = new Sound(a);
        a = Sound();   // the original handle drops the voice; the copy keeps it
        check("copy: the voice plays while a copy is alive", c->isPlaying() && voices() == 1);
        delete c;
        check("copy: the last handle going away stops the voice", voices() == 0,
              to_string(voices()));
    }

    // --- A scoped one-shot stops when its scope ends --------------------------
    {
        Sound hit;
        hit.loadTestTone(880.0f, 10.0f);   // long enough not to end on its own
        hit.setVolume(0.0f);
        {
            Sound s = hit;
            check("one-shot: a scoped copy plays", s.play() && s.isPlaying());
            check("one-shot: one voice", voices() == 1, to_string(voices()));
        }
        check("one-shot: it stops when its scope ends", voices() == 0, to_string(voices()));
        check("one-shot: the loaded Sound is not playing", !hit.isPlaying());
    }

    // --- Assignment releases the old voice ------------------------------------
    {
        Sound s = makeLoop();
        const Sound idle = makeLoop(660.0f);
        int played = 0;
        for (int i = 0; i < kPolyphony + 8; ++i) {
            if (s.play()) ++played;
            if (i % 2 == 0) s = idle;      // copy assignment
            else            s = makeLoop();   // move assignment
        }
        check("assignment: each play over the reassigned Sound plays",
              played == kPolyphony + 8, to_string(played));
        check("assignment: no voice is left", voices() == 0, to_string(voices()));

        Sound t = makeLoop(550.0f);
        s = makeLoop();
        s.play();
        t.play();
        check("move assignment: two voices before", voices() == 2, to_string(voices()));
        s = std::move(t);
        check("move assignment: the old voice is released", voices() == 1, to_string(voices()));
        check("move assignment: the moved voice keeps playing in the target", s.isPlaying());
        check("move assignment: the source no longer plays", !t.isPlaying());

        Sound m = std::move(s);
        check("move construction: the voice keeps playing", m.isPlaying() && voices() == 1);
    }
    check("move: the last handle going away stops the voice", voices() == 0, to_string(voices()));

    // --- A paused voice is released -------------------------------------------
    {
        Sound p = makeLoop();
        p.play();
        p.pause();
        check("pause: a paused voice is listed", voices() == 1, to_string(voices()));
    }
    check("pause: a paused voice is released with its Sound", voices() == 0, to_string(voices()));

    // --- stop() releases the voice for every copy -----------------------------
    {
        Sound a = makeLoop();
        a.play();
        Sound b = a;
        a.stop();
        check("stop(): a copy sees the shared voice stopped", !b.isPlaying() && voices() == 0);
        check("stop(): the copy plays again", b.play() && b.isPlaying());
    }
    check("stop(): no voice is left", voices() == 0, to_string(voices()));

    // --- Streams release their decoder and file -------------------------------
    {
        const fs::path wav = fs::temp_directory_path() / "tc_soundVoiceLifetime.wav";
        check("stream: the test file is written", writeWav(wav, 2.0f));
        Sound stream;
        check("stream: loadStream() succeeds", (bool)stream.loadStream(wav, 1));
        stream.setLoop(true);
        stream.setVolume(0.0f);

        int played = 0;
        for (int i = 0; i < 4; ++i) {
            Sound s = stream;
            if (s.play()) ++played;
        }
        check("stream: a new scoped copy plays each time (maxPolyphony 1)", played == 4,
              to_string(played));
        check("stream: no voice is left", voices() == 0, to_string(voices()));
#ifdef __linux__
        {
            Sound s = stream;
            s.play();
            check("stream: a playing voice holds its file", openHandles(wav) == 1,
                  to_string(openHandles(wav)));
        }
        check("stream: the file closes when the last handle goes away", openHandles(wav) == 0,
              to_string(openHandles(wav)));
        stream.play();
        stream.stop();
        check("stream: the file closes on stop()", openHandles(wav) == 0,
              to_string(openHandles(wav)));
#endif
        check("stream: plays again after stop()", stream.play() && stream.isPlaying());
        stream.stop();
        std::error_code ec;
        fs::remove(wav, ec);
    }

    // --- Later play() calls still work ----------------------------------------
    {
        int played = 0;
        vector<Sound> kept(kPolyphony);
        for (auto& s : kept) {
            s = makeLoop();
            if (s.play()) ++played;
        }
        check("later: maxPolyphony kept Sounds all play", played == kPolyphony,
              to_string(played));
        check("later: every slot is in use", voices() == (size_t)kPolyphony, to_string(voices()));
    }
    check("later: no voice is left", voices() == 0, to_string(voices()));

    engine.shutdown();
    printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail,
           g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
