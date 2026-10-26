#!/usr/bin/env python3
"""ASan reentrancy probe of the verbatim production completion/close functions.
Run through tcbuild.sh after building addons/tcxCurl/tests (uses its link recipe).
No browser runtime is claimed: this exercises the real Logger synchronously.
"""
import pathlib
import shlex
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
implementation = (root / 'core/include/tc/graphics/tcBoundedFontFetch.cpp').read_text(encoding="utf-8")

def function(signature):
    begin = implementation.index(signature)
    brace = implementation.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (implementation[end] == '{') - (implementation[end] == '}')
        end += 1
    return implementation[begin:end]

source = '''#include "TrussC.h"
#define __EMSCRIPTEN__
#include "tc/graphics/tcBoundedFontFetch.h"
#undef __EMSCRIPTEN__
#include <cassert>
namespace trussc::internal {
size_t rejectedFontFetches = 0;
void cancelFontStream(void*) {}
'''
source += function('void finishFontStream(') + '\n'
source += function('void closeBoundedFontFetch(') + '\n}\n'
source += '''int main() {
    using namespace trussc::internal;
    struct State { BoundedFontFetch* fetch; bool completed = false, cancelled = false; };
    auto state = std::make_shared<State>();
    auto* request = new BoundedFontFetch;
    state->fetch = request;
    request->userData = new std::shared_ptr<State>(state);
    request->complete = [](BoundedFontFetch* result, bool success) {
        auto* holder = static_cast<std::shared_ptr<State>*>(result->userData);
        auto active = *holder;
        assert(!success);
        active->fetch = nullptr; // Font::finishUrlAsync detaches before closing
        active->completed = true;
        closeBoundedFontFetch(result);
        delete holder;
    };
    unsigned warnings = 0;
    auto listener = trussc::getLogger().onLog.listen([&](trussc::LogEventArgs& event) {
        if (event.level != trussc::LogLevel::Warning) return;
        ++warnings;
        // Same ownership edge as Font::cancelAsync -> finishUrlAsync.
        state->cancelled = true;
        if (state->fetch) {
            auto* active = state->fetch;
            state->fetch = nullptr;
            delete static_cast<std::shared_ptr<State>*>(active->userData);
            closeBoundedFontFetch(active);
        }
        assert(state->completed); // warning is observable only after settlement
    });
    finishFontStream(request, nullptr, 0, -1);
    assert(state->completed && state->cancelled && !state->fetch && warnings == 1);
}
'''
build = root / 'addons/tcxCurl/tests/build-linux'
args = [arg for arg in shlex.split((build / 'CMakeFiles/tests.dir/link.txt').read_text(encoding="utf-8")) if arg != '-DNDEBUG']
with tempfile.TemporaryDirectory(prefix='font-lifetime-') as tmp:
    probe = pathlib.Path(tmp) / 'probe.cpp'
    probe.write_text(source, encoding="utf-8")
    args[args.index('CMakeFiles/tests.dir/src/main.cpp.o')] = str(probe)
    args[args.index('-o') + 1] = str(pathlib.Path(tmp) / 'probe')
    args[1:1] = ['-std=c++20', '-fsanitize=address', '-fno-omit-frame-pointer',
                 '-DSOKOL_GLCORE', '-I' + str(root / 'core/include')]
    subprocess.run(args, cwd=build, check=True)
    subprocess.run([str(pathlib.Path(tmp) / 'probe')], check=True)
print('PASS: first font-loss Warning cancels only after completion; ASan clean')
