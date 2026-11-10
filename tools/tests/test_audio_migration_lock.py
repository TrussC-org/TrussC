#!/usr/bin/env python3
"""Compile production re-init, eager migration and diagnostics with device stubs.

The second voice attempts a diagnostic read after the first voice migrated. A
slot destructor does the same during cap reduction. Contention is acknowledged
by a promise, so no verdict depends on a sleep or scheduling delay. No test seam
is added to the engine. Run through tcbuild.sh; --mutations checks the oracles.
"""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]


def before(text, anchor, where):
    # The fixture slices production code at these anchors; name the one that moved.
    if anchor not in text:
        sys.exit(f"test_audio_migration_lock: anchor {anchor.strip()!r} not found in {where}; "
                 "update the anchor in this test after editing that function")
    return text[:text.index(anchor)]


def main():
    compiler = os.environ.get("CXX") or shutil.which("c++")
    if not compiler:
        print("SKIP: test_audio_migration_lock requires a C++ compiler (c++ on PATH, or set CXX)")
        return
    source = (ROOT / "core/include/tc/sound/tcAudio_impl.cpp").read_text(encoding="utf-8")
    init = body(source, "bool AudioEngine::init(const AudioSettings& settings)")
    # Exercise all control flow through migration. Native context/device opening
    # is outside this seam; the stand-in below checks the lock is released there.
    prefix = before(init, "    // Lazily create a persistent ma_context.", "AudioEngine::init()")
    reader = body(source, "std::vector<PlayingSoundInfo> AudioEngine::getPlayingSounds() const")
    migration = body(source, "void AudioEngine::migrateVoicesToNewRate(")
    # Keep the real loop, early returns and eager migration. Decoder work is
    # outside this lock test; the full streaming path is covered by streamSeek.
    migration = before(migration, "        } else {", "AudioEngine::migrateVoicesToNewRate()") + r'''
        } else { throw "unexpected stream in eager fixture"; }
    }
}
'''
    code = r'''
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace fs = std::filesystem;
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
struct UnlockedRead {};
thread_local std::promise<bool>* readAttempt = nullptr;
struct ObservedMutex {
    std::mutex native;
    const std::thread::id engineThread = std::this_thread::get_id();
    std::atomic<bool> engineThreadHolds{false};
    void lock() {
        if (readAttempt) {
            const bool acquired = native.try_lock();
            if (acquired) native.unlock();
            readAttempt->set_value(!acquired);
            readAttempt = nullptr;
            if (acquired) {
                // A mutant is unprotected: do not actually race vector::resize.
                throw UnlockedRead{};
            }
        }
        native.lock();
        if (std::this_thread::get_id() == engineThread) engineThreadHolds = true;
    }
    void unlock() {
        if (std::this_thread::get_id() == engineThread) engineThreadHolds = false;
        native.unlock();
    }
};
struct SoundSource {
    enum Kind { Eager, Stream };
    int sampleRate = 48000;
    mutable std::function<void()> onKind;
    Kind kind() const {
        if (onKind) { auto callback = std::exchange(onKind, {}); callback(); }
        return Eager;
    }
    float getDuration() const { return 1.0f; }
};
struct SoundBuffer : SoundSource { fs::path getPath() const { return {}; } };
struct SoundStream : SoundSource { fs::path getPath() const { return {}; } };
struct PlayingSound {
    std::shared_ptr<SoundSource> buffer = std::make_shared<SoundBuffer>();
    bool playing = true, paused = false, loop = false;
    double positionF = 12000;
    float volume = 1, pan = 0, speed = 1, rateRatio = 1;
    std::atomic<int> positionRateHz_{48000};
    std::atomic<float> level{0};
    std::function<void()> onDestroy;
    ~PlayingSound() { if (onDestroy) onDestroy(); }
};
struct PlayingSoundInfo {
    int slot;
    bool streaming, paused, loop;
    fs::path path;
    float position, duration, volume, pan, speed, level;
};
struct Diagnostics {
    std::atomic<bool> running{true}, lossPending{false};
    bool isStalled() const { return false; }
};
struct AudioDeviceInfo { std::string name; };
struct AudioSettings {
    int sampleRate = 96000, channels = 2, bufferSize = 256, maxPolyphony = 1;
    std::string deviceName;
    std::optional<AudioDeviceInfo> device;
};
namespace internal { struct AudioOutputRing { AudioOutputRing(int, int) {} }; }
struct Log { template<class T> Log& operator<<(const T&) { return *this; } };
Log logNotice(const char*) { return {}; }
struct ma_device {};
struct AudioEngine;
AudioEngine* currentEngine = nullptr;
void ma_device_uninit(ma_device*);
void deviceLossSlot() {}
struct AudioEngine {
    static constexpr int DEFAULT_SAMPLE_RATE = 48000, DEFAULT_CHANNELS = 2,
                         DEFAULT_BUFFER_SIZE = 256, ANALYSIS_BUFFER_SIZE = 8;
    bool initialized_ = true;
    std::atomic<bool> inputNotifyEnabled_{true};
    void* device_ = new ma_device;
    int sampleRate_ = 48000, channels_ = 2, bufferSize_ = 256, maxPolyphony_ = 3;
    mutable ObservedMutex mutex_;
    std::mutex analysisMutex_;
    std::unique_ptr<internal::AudioOutputRing> analysisRing_;
    float analysisCopy_[ANALYSIS_BUFFER_SIZE]{};
    std::unique_ptr<Diagnostics> diag_ = std::make_unique<Diagnostics>();
    std::vector<std::shared_ptr<PlayingSound>> playingSounds_;
    std::vector<std::thread> readers;
    std::atomic<int> completedReads{0};
    int deviceStops = 0;
    void resetMeters() {} // Unrelated meter reset; real implementation locks separately.
    bool init(const AudioSettings&);
    std::vector<PlayingSoundInfo> getPlayingSounds() const;
    void diagnosticAttempt(const char* label) {
        auto attempt = std::make_shared<std::promise<bool>>();
        auto result = attempt->get_future();
        readers.emplace_back([this, attempt] {
            readAttempt = attempt.get();
            try {
                const auto snapshot = getPlayingSounds();
                if (snapshot.size() == 2) ++completedReads;
            } catch (const UnlockedRead&) {}
        });
        check(label, result.get());
    }
    void deviceCall(const char* label) {
        // A native stop/start can wait for a callback that takes mutex_.
        // Model that callback's ability to acquire the lock, without hanging
        // the intentionally broken "lock around device calls" mutant.
        bool unlocked = false;
        std::thread callback([&] {
            unlocked = !mutex_.engineThreadHolds.load();
            if (unlocked) {
                // Diagnostic readers released by migration can legitimately
                // own the lock here. Wait for them, without a timing verdict.
                std::lock_guard<std::mutex> lock(mutex_.native);
            }
        });
        callback.join();
        check(label, unlocked);
    }
    void migrateVoicesToNewRate(int, int);
};
void ma_device_uninit(ma_device*) {
    ++currentEngine->deviceStops;
    currentEngine->deviceCall("device shutdown can acquire engine lock");
}
READER
MIGRATION
INIT_PREFIX
    deviceCall("device opening can acquire engine lock");
    return true;
}
int main() {
    // Live re-init, init after shutdown, and unchanged-rate cap reduction.
    for (int scenario = 0; scenario < 3; ++scenario) {
        AudioEngine engine;
        currentEngine = &engine;
        if (scenario == 1) {
            delete static_cast<ma_device*>(engine.device_);
            engine.device_ = nullptr;
            engine.initialized_ = false;
        }
        for (int i = 0; i < 3; ++i) {
            auto voice = std::make_shared<PlayingSound>();
            if (i == 2) voice->onDestroy = [&] {
                engine.diagnosticAttempt("diagnostic reader excluded during slot shrink");
            };
            engine.playingSounds_.push_back(std::move(voice));
        }
        if (scenario != 2) {
            engine.playingSounds_[1]->buffer->onKind = [&] {
                check("first voice migrated before diagnostic attempt",
                      engine.playingSounds_[0]->rateRatio == 0.5f);
                engine.diagnosticAttempt("diagnostic reader excluded during migration");
            };
        }
        AudioSettings settings;
        settings.maxPolyphony = 2;
        if (scenario == 2) settings.sampleRate = 48000;
        check("init succeeds", engine.init(settings));
        for (auto& reader : engine.readers) reader.join();
        check("diagnostic reads finish with retained slots",
              engine.completedReads == (scenario == 2 ? 1 : 2));
        check("cap and migration applied", engine.playingSounds_.size() == 2
              && engine.maxPolyphony_ == 2
              && engine.playingSounds_[0]->rateRatio == (scenario == 2 ? 1.0f : 0.5f));
        engine.playingSounds_[1]->buffer->onKind = nullptr;
        check("live device stopped before migration", engine.deviceStops == (scenario == 1 ? 0 : 1));
    }
    return failures ? 1 : 0;
}
'''
    # Observe only engine mutex acquisition; analysis uses a different mutex.
    prefix = prefix.replace("std::lock_guard<std::mutex> lock(mutex_);",
                            "std::lock_guard<ObservedMutex> lock(mutex_);")
    reader = reader.replace("std::lock_guard<std::mutex> lock(mutex_);",
                            "std::lock_guard<ObservedMutex> lock(mutex_);")
    variants = [("fixed", prefix, reader)]
    if "--mutations" in sys.argv:
        lock = "        std::lock_guard<ObservedMutex> lock(mutex_);"
        assert prefix.count(lock) == 1
        variants.append(("missing_lock", prefix.replace(lock, ""), reader))
        # Protect migration alone: cap reduction must fail independently.
        migrate = "        migrateVoicesToNewRate(oldRate, sampleRate_);"
        variants.append(("migration_only", prefix.replace(lock, "").replace(
            migrate, lock + "\n" + migrate), reader))
        # Move the same lock to the function entry, including blocking stop.
        entry = prefix.index("{") + 1
        wider = prefix.replace(lock, "")
        wider = wider[:entry] + "\n" + lock + wider[entry:]
        variants.append(("device_call_under_lock", wider, reader))
    with tempfile.TemporaryDirectory(prefix="tc-audio-migration-") as directory:
        cpp, binary = Path(directory) / "probe.cpp", Path(directory) / "probe"
        for name, selected_prefix, selected_reader in variants:
            cpp.write_text(code.replace("INIT_PREFIX", selected_prefix).replace(
                "READER", selected_reader).replace("MIGRATION", migration), encoding="utf-8")
            subprocess.run([compiler, "-std=c++20", "-pthread",
                            "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    encoding="utf-8", timeout=15)
            print(f"{name}: exit {result.returncode}\n{result.stdout}", flush=True)
            expected = 0 if name == "fixed" else 1
            assert result.returncode == expected, name
            if expected:
                assert "FAIL:" in result.stdout, name
                labels = {
                    "missing_lock": ["diagnostic reader excluded during migration",
                                     "diagnostic reader excluded during slot shrink"],
                    "migration_only": ["diagnostic reader excluded during slot shrink"],
                    "device_call_under_lock": ["device shutdown can acquire engine lock",
                                               "device opening can acquire engine lock"],
                }
                for label in labels[name]:
                    assert f"FAIL: {label}" in result.stdout, (name, label)


if __name__ == "__main__":
    main()
