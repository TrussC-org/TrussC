"""Compile the actual Windows wait helpers against scripted COM results (no SDK/GPU)."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'core/platform/win/tcVideoPlayer_win.cpp').read_text(encoding='utf-8')
functions = []
for name in ('HRESULT TCVideoPlayerImpl::mutexStep(', 'bool TCVideoPlayerImpl::waitForRendering(',
             'bool TCVideoPlayerImpl::acquireRendering('):
    begin = source.index(name)
    end = source.index('\n}\n', begin) + 3
    functions.append(source[begin:end])

prefix = r'''
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using HRESULT = int32_t;
using UINT64 = uint64_t;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_FAIL = -1, WAIT_TIMEOUT = 258, WAIT_ABANDONED = 128;
constexpr int D3D11_ASYNC_GETDATA_DONOTFLUSH = 1;
void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void SwitchToThread() { std::this_thread::yield(); }
std::vector<std::string> logs;
struct Log : std::ostringstream { ~Log() { logs.push_back(str()); } };
Log logNotice(const char*) { return {}; }
Log logWarning(const char*) { return {}; }
struct IDXGIKeyedMutex {
    HRESULT result = S_OK;
    UINT64 key = 99;
    int timeout = -1, acquires = 0, releases = 0;
    HRESULT AcquireSync(UINT64 value, int wait) { key = value; timeout = wait; ++acquires; return result; }
    HRESULT ReleaseSync(UINT64 value) { key = value; ++releases; return result; }
};
struct Query {};
struct Context {
    HRESULT result = S_OK;
    int pending = 0, ended = 0, flushed = 0;
    void End(Query*) { ++ended; }
    void Flush() { ++flushed; }
    HRESULT GetData(Query*, void*, int, int flags) {
        assert(flags == D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (pending > 0) { --pending; return S_FALSE; }
        return result;
    }
};
template<class T> struct Ptr {
    T object;
    T* Get() { return &object; }
    T* operator->() { return Get(); }
};
struct Errors {
    std::string message;
    HRESULT code = 0;
    void report(const char* text, HRESULT value) { message = text; code = value; }
};
struct State {
    bool mutexLog_ = false, mutexBroken_ = false, timeoutWarned_ = false;
    bool gpuWait_ = true;  // Exercise the opt-in completion wait.
    bool renderOwns_ = false, renderPending_ = true;
    uint64_t logFrame_ = 1, transferredFrames_ = 0, playerId_ = 7;
    UINT64 pendingKey_ = 1;
    Ptr<IDXGIKeyedMutex> renderMutex_;
    Ptr<Context> renderContext_;
    Ptr<Query> renderDone_;
    Errors playbackErrors_;
};
class TCVideoPlayerImpl {
public:
    std::shared_ptr<State> state_ = std::make_shared<State>();
    HRESULT mutexStep(IDXGIKeyedMutex*, bool, UINT64, const char*);
    bool waitForRendering();
    bool acquireRendering();
};
'''
suffix = r'''
int main() {
    TCVideoPlayerImpl player;
    auto& state = *player.state_;
    auto& mutex = state.renderMutex_.object;
    mutex.result = WAIT_TIMEOUT;  // A nonnegative HRESULT still does not grant ownership.
    assert(!player.acquireRendering());
    assert(!state.renderOwns_ && state.renderPending_ && !state.mutexBroken_);
    assert(mutex.timeout == 100 && mutex.key == 1);
    assert(logs.size() == 1 && state.timeoutWarned_);
    assert(!player.acquireRendering() && logs.size() == 1);  // Warning once per player.
    mutex.result = S_OK;
    assert(player.acquireRendering());
    assert(state.renderOwns_ && !state.renderPending_);
    for (HRESULT result : {WAIT_ABANDONED, E_FAIL}) {
        state.renderOwns_ = false;
        state.renderPending_ = true;
        state.mutexBroken_ = false;
        mutex.result = result;
        assert(!player.acquireRendering());
        assert(state.mutexBroken_ && !state.renderOwns_ && state.renderPending_);
        assert(state.playbackErrors_.code == result && !state.playbackErrors_.message.empty());
    }
    state.mutexBroken_ = false;
    state.renderOwns_ = true;
    state.renderContext_->pending = 2;
    assert(player.waitForRendering());
    assert(state.renderContext_->ended == 1 && state.renderContext_->flushed == 1);
    state.renderContext_->result = S_FALSE;
    assert(!player.waitForRendering() && state.renderOwns_ && !state.mutexBroken_);
    state.renderContext_->result = E_FAIL;
    assert(!player.waitForRendering() && state.mutexBroken_);
    mutex.result = S_OK;
    state.mutexLog_ = true;
    logs.clear();
    assert(player.mutexStep(&mutex, false, 0, "sokol-release") == S_OK);
    assert(mutex.releases == 1 && mutex.key == 0);
    assert(logs.size() == 1);
    for (const char* field : {"player=7", "frame=1", "step=sokol-release", "key=0", "HRESULT=0x0", "wait_us="})
        assert(logs.back().find(field) != std::string::npos);
    state.logFrame_ = 120;
    player.mutexStep(&mutex, true, 1, "sokol-acquire");
    assert(logs.size() == 2 && logs.back().find("frame=120") != std::string::npos);
    state.logFrame_ = 121;
    player.mutexStep(&mutex, true, 1, "sokol-acquire");
    assert(logs.size() == 2);
}
'''
with tempfile.TemporaryDirectory(dir=ROOT / '.build', prefix='win-mutex-') as temporary:
    path = Path(temporary)
    cpp = path / 'main.cpp'
    cpp.write_text(prefix + '\n'.join(functions) + suffix, encoding='utf-8')
    compiler = os.environ.get('CXX', 'c++')
    command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(path / 'mutex')]
    print('COMMAND:', *command, flush=True)
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'mutex')], check=True)
print('PASS: success, timeout, abandoned, failure, GPU completion, ownership, bounded log')
