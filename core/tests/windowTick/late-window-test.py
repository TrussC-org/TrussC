#!/usr/bin/env python3
"""Own-process ASan/TSan late-handle/restart regressions and teardown extractions.
Run through tcbuild.sh after the shared Linux build. Scratch is removed on exit.
"""
from pathlib import Path
import json
import os
import shlex
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(os.environ.get('TC_LATE_WINDOW_OUTPUT', ROOT / '.verification/cp3fu11/late-window'))
OUT.mkdir(parents=True, exist_ok=True)

def body(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
rows = []
with tempfile.TemporaryDirectory(prefix='tc-an2b-cp3fu11-late-', dir=os.environ.get('TMPDIR')) as directory:
    scratch = Path(directory)
    # Execute the incoming macOS shared_ptr deleter on real retained Windows.
    # Destruction still uses the production common CPU registry and mutex.
    mac = (ROOT/'core/platform/mac/tcWindowMac.mm').read_text(encoding='utf-8')
    deleter = body(mac, '[](Window* w) {')
    (scratch/'late-mac-deleter.h').write_text(
        'namespace trussc::internal { const auto lateMacWindowDeleter = ' + deleter + '; }\n', encoding='utf-8')
    flags = ['-DTC_LATE_MAC_DELETER', '-I'+str(scratch)]
    for line in (ROOT/'core/build-linux/CMakeFiles/TrussC.dir/flags.make').read_text(encoding='utf-8').splitlines():
        if line.startswith(('CXX_DEFINES =', 'CXX_INCLUDES =')):
            flags += shlex.split(line.split('=', 1)[1])
    flags = ['-std=c++20', '-O0', '-g1', '-fsanitize=address', '-fno-omit-frame-pointer', '-fno-pie', *flags]
    link = shlex.split((ROOT/'core/tests/allCoreTests/build-linux/CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
    libs = link[next(i for i, value in enumerate(link) if value.endswith('libTrussC.a')):]
    libs = [value for value in libs if value != '-Wl,--wrap=malloc']
    objects = []
    for name, source in [('probe', ROOT/'core/tests/windowTick/late-window.cpp'), ('adapter', ROOT/'core/platform/linux/tcWindowLinux.cpp')]:
        obj = scratch/(name+'.o')
        subprocess.run(['c++', *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    source = (ROOT/'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
    release_guard = 'void RenderTargetResources::release() {\n    if (frameworkShutdown()) return;'
    assert source.count(release_guard) == 1
    variants = [('baseline', source), ('no_shutdown_guard', source.replace(
        'return frameworkStopped.load(std::memory_order_acquire);', 'return false;')),
        ('release_guard_removed', source.replace(release_guard, 'void RenderTargetResources::release() {')),
        ('window_skip_erase', source.replace('if (!initializedWindowRegistry) return;',
            'if (frameworkShutdown() || !initializedWindowRegistry) return;')),
        ('lighting_skip_scrub', source.replace('template<class Scrub> void scrubLightingContexts(Scrub scrub) noexcept {',
            'template<class Scrub> void scrubLightingContexts(Scrub scrub) noexcept {\n    if (frameworkShutdown()) return;')),
        ('debug_skip_erase', source)]
    assert all(content != source for name, content in variants if name not in ('baseline', 'debug_skip_erase'))
    debug_source = (ROOT/'core/include/tc/utils/tcDebugName.cpp').read_text(encoding='utf-8')
    debug_guard = 'if (debugRegistryDestroyed.load(std::memory_order_acquire)) return;'
    assert debug_source.count(debug_guard) == 1
    debug_objects = {}
    for name in ['baseline', 'debug_skip_erase']:
        cpp = scratch/(name+'-debug.cpp')
        cpp.write_text(debug_source if name == 'baseline' else debug_source.replace(debug_guard,
            'if (frameworkShutdown() || debugRegistryDestroyed.load(std::memory_order_acquire)) return;'), encoding='utf-8')
        obj = scratch/(name+'-debug.o')
        subprocess.run(['c++', *flags, '-I'+str(ROOT/'core/include/tc/utils'), '-c', str(cpp), '-o', str(obj)], check=True)
        debug_objects[name] = str(obj)
    for name, content in variants:
        # Keep relative includes valid when compiling the actual production TU.
        content = content.replace('#include "impl/', '#include "'+str(ROOT/'core/include/impl')+'/')
        content = content.replace('#include "internal/', '#include "'+str(ROOT/'core/include/tc/app/internal')+'/')
        content = content.replace('#include "../../../platform/', '#include "'+str(ROOT/'core/platform')+'/')
        cpp = scratch/(name+'.cpp'); cpp.write_text(content, encoding='utf-8')
        (OUT/(name+'.cpp')).write_text(content, encoding='utf-8')
        obj = scratch/(name+'.o')
        subprocess.run(['c++', *flags, '-c', str(cpp), '-o', str(obj)], check=True)
        binary = scratch/name
        wrapped = ['sg_isvalid', 'sgl_default_context', 'sg_destroy_buffer', 'sg_destroy_image',
            'sg_destroy_view', 'sg_destroy_sampler', 'sg_destroy_pipeline', 'sg_destroy_shader',
            'sgl_destroy_pipeline', 'sgl_destroy_context', 'sapp_destroy_window', 'sapp_window_request_close',
            '_ZN6trussc8internal17getDefaultContextEv', '_ZN6trussc8internal20currentWindowContextEv',
            '_ZN6trussc9getLoggerEv']
        subprocess.run(['c++', '-fsanitize=address', '-no-pie', *objects, str(obj),
            debug_objects['debug_skip_erase' if name == 'debug_skip_erase' else 'baseline'], *libs,
            *['-Wl,--wrap='+symbol for symbol in wrapped], '-o', str(binary)], check=True)
        modes = (['static', 'after', 'thread', 'headless', 'restart', *(['concurrent'] * 10), 'concurrent-scrubs'] if name == 'baseline' else
                 ['restart'] if name in ('window_skip_erase', 'debug_skip_erase', 'lighting_skip_scrub') else
                 ['after'] if name == 'release_guard_removed' else ['static', 'after'])
        for iteration, mode in enumerate(modes):
            # Only ASan/runtime failure counts as mutation detection.
            # Avoid competing with the display sweep's default :99 range.
            server = 1000 + int.from_bytes(os.urandom(2), 'big') % 20000
            xvfb_log = OUT/(name+'-'+mode+'-xvfb.log')
            command = ['xvfb-run', '-a', '-n', str(server), '-e', str(xvfb_log),
                       '-s', '-screen 0 1280x720x24', str(binary), mode]
            run = subprocess.run(command, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', TRUSSC_MCP='0', TC_LATE_WINDOW_MUTATION=name,
                LIBGL_ALWAYS_SOFTWARE='1', __GLX_VENDOR_LIBRARY_NAME='mesa', GALLIUM_DRIVER='llvmpipe'),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', timeout=60)
            (OUT/(name+'-'+mode+'-'+str(iteration)+'.log')).write_text(run.stdout, encoding='utf-8')
            detected = run.returncode == 1 and 'ERROR: AddressSanitizer:' in run.stdout and any(
                frame in run.stdout for frame in ('Window::', 'RenderTargetResources::', 'currentWindowContext', 'debugObjects'))
            semantic = (run.returncode == 1 and 'late-window: restart FAIL:' in run.stdout and
                        'late-window: restart completed,' in run.stdout)
            rows.append(dict(variant=name, mode=mode, returncode=run.returncode, asan=detected,
                             completed_assertion_failure=semantic))
            if name == 'baseline':
                expected = 'late-window: restart completed, 0 failures' if mode in ('restart', 'concurrent', 'concurrent-scrubs') else 'late-window: stopped=1'
                assert run.returncode == 0 and expected in run.stdout and 'late-window: static destruction completed' in run.stdout, run.stdout
            else:
                assert semantic if name == 'lighting_skip_scrub' else detected, run.stdout
            print(name, mode, 'PASS', flush=True)
        binary.unlink()
        obj.unlink()
    # Instrument the same production TUs and fixture with TSan. Workers erase
    # Windows while peer light/material/environment destructors scrub contexts.
    # Linux ASLR can prevent TSan startup; only a completed run earns a verdict.
    tsan_flags = [flag.replace('-fsanitize=address', '-fsanitize=thread') for flag in flags]
    tsan_objects = []
    for name, path in [('probe', ROOT/'core/tests/windowTick/late-window.cpp'),
                       ('adapter', ROOT/'core/platform/linux/tcWindowLinux.cpp'),
                       ('debug', ROOT/'core/include/tc/utils/tcDebugName.cpp')]:
        obj = scratch/('tsan-'+name+'.o')
        subprocess.run(['c++', *tsan_flags, '-c', str(path), '-o', str(obj)], check=True)
        tsan_objects.append(str(obj))
    erase = body(source, 'void unregisterWindow(')
    scrub = body(source, 'template<class Scrub> void scrubLightingContexts(')
    lock = '    std::lock_guard lock(windowRegistryMutex());\n'
    assert erase.count(lock) == scrub.count(lock) == 1
    for name, content in [('baseline', source),
            ('window_erase_unlocked', source.replace(erase, erase.replace(lock, ''))),
            ('lighting_scrub_unlocked', source.replace(scrub, scrub.replace(lock, '')))]:
        content = content.replace('#include "impl/', '#include "'+str(ROOT/'core/include/impl')+'/')
        content = content.replace('#include "internal/', '#include "'+str(ROOT/'core/include/tc/app/internal')+'/')
        content = content.replace('#include "../../../platform/', '#include "'+str(ROOT/'core/platform')+'/')
        cpp = scratch/('tsan-'+name+'.cpp'); cpp.write_text(content, encoding='utf-8')
        obj = scratch/('tsan-'+name+'.o'); binary = scratch/('tsan-'+name)
        subprocess.run(['c++', *tsan_flags, '-c', str(cpp), '-o', str(obj)], check=True)
        subprocess.run(['c++', '-fsanitize=thread', '-no-pie', *tsan_objects, str(obj), *libs,
            *['-Wl,--wrap='+symbol for symbol in wrapped], '-o', str(binary)], check=True)
        completed = 0
        for attempt in range(100):
            mode = 'concurrent-scrubs' if name == 'lighting_scrub_unlocked' or (name == 'baseline' and completed == 2) else 'concurrent'
            run = subprocess.run([str(binary), mode], env=dict(os.environ,
                TSAN_OPTIONS='halt_on_error=0:exitcode=66', TRUSSC_MCP='0'),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', timeout=60)
            (OUT/('tsan-'+name+'-'+str(attempt)+'.log')).write_text(run.stdout, encoding='utf-8')
            if 'FATAL: ThreadSanitizer: unexpected memory mapping' in run.stdout:
                rows.append(dict(tsan=name, attempt=attempt, startup_mapping_failure=True, returncode=run.returncode))
                continue
            if run.returncode == -11 and not run.stdout:
                # This host also intermittently faults in TSan before main,
                # reproduced by a standalone trivial TSan thread probe. The
                # stderr entry marker makes any in-test crash fail normally.
                rows.append(dict(tsan=name, attempt=attempt, startup_segv=True, returncode=run.returncode))
                continue
            complete = 'late-window: restart completed,' in run.stdout and 'late-window: static destruction completed' in run.stdout
            race = 'WARNING: ThreadSanitizer: data race' in run.stdout
            rows.append(dict(tsan=name, attempt=attempt, returncode=run.returncode, completed=complete, data_race=race))
            assert complete, run.stdout
            if name == 'baseline':
                assert run.returncode == 0 and not race and 'late-window: restart completed, 0 failures' in run.stdout, run.stdout
            else:
                assert run.returncode == 66 and race and ('unregisterWindow' in run.stdout or 'scrubLightingContexts' in run.stdout), run.stdout
            completed += 1
            if completed == (3 if name == 'baseline' else 1): break
        assert completed == (3 if name == 'baseline' else 1), 'TSan never completed: '+name
        print('tsan', name, 'PASS', flush=True)
        binary.unlink(); obj.unlink()
    # Each adapter's actual body must stop before ANY teardown dependency.
    # Every seam aborts if called. The native sentinel intentionally stays live.
    for platform, file in [('mac', 'mac/tcWindowMac.mm'), ('win', 'win/tcWindowWin.cpp'), ('linux', 'linux/tcWindowLinux.cpp')]:
        adapter = (ROOT/'core/platform'/file).read_text(encoding='utf-8')
        functions = '\n'.join(body(adapter, marker) for marker in
            ['void Window::teardownSecondary() noexcept', 'void Window::closeSecondary()'])
        fixture = r'''
#include <cstdlib>
#include <optional>
struct AdapterState { int win; };
namespace internal {
bool frameworkShutdown() noexcept { return true; }
struct TeardownSteps { template<class F> void run(const char*, F) { std::abort(); } };
struct EntryStackGuard { template<class T> EntryStackGuard(T) { std::abort(); } };
enum class AppEntry { Exit };
struct WindowRequestAccess { template<class T> static void unbind(T&) { std::abort(); } };
}
void sapp_destroy_window(int) { std::abort(); }
void sapp_window_request_close(int) { std::abort(); }
struct Resources { void release() { std::abort(); } };
struct Target { Resources& resources() { std::abort(); } };
struct Context {
 bool targetsClosed = false;
 void teardownTargets() { std::abort(); }
 void* acquireSwapchain = nullptr; void* acquireSwapchainUser = nullptr; void* activateNative = nullptr;
 Target swapchainTarget;
};
struct Window {
 void* native_; bool closeRequested_ = false; Context ctx_;
 void teardownSecondary() noexcept; void closeSecondary(); void endApp() { std::abort(); }
};
''' + functions + '\nint main() { AdapterState state{7}; Window w{&state}; w.teardownSecondary(); w.closeSecondary(); return w.native_ != &state; }\n'
        cpp = scratch/(platform+'.cpp'); cpp.write_text(fixture, encoding='utf-8')
        (OUT/(platform+'.cpp')).write_text(fixture, encoding='utf-8')
        binary = scratch/platform
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        rows.append(dict(adapter=platform, returncode=0))
        mutated = fixture.replace('if (internal::frameworkShutdown()) return;', '')
        assert mutated != fixture
        cpp.write_text(mutated, encoding='utf-8')
        (OUT/(platform+'-mutant.cpp')).write_text(mutated, encoding='utf-8')
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert run.returncode != 0, platform + ' adapter mutant survived'
        rows.append(dict(adapter=platform, mutation='remove_guards', returncode=run.returncode))
        print(platform, 'adapter guards and mutation PASS', flush=True)
    # Independent late resource release: ANY sgl query/release would abort.
    # This populated cache tests pipeline release; the own binary also tests
    # an empty cache with a nonzero context and wrapped default-context query.
    resource_body = body(source, 'void RenderTargetResources::release()')
    fixture = r'''
#include <cstdlib>
#include <unordered_map>
#include <memory>
namespace internal {
bool frameworkShutdown() noexcept { return true; }
struct Handle { unsigned id = 0; };
Handle sgl_default_context() { std::abort(); }
void sgl_destroy_context(Handle) { std::abort(); }
void sgl_destroy_pipeline(Handle) { std::abort(); }
auto& pipelineOwnerCtx() { static std::unordered_map<unsigned,unsigned> map; std::abort(); return map; }
struct RenderTargetResources {
 std::unordered_map<int,Handle> cache, selections;
 std::shared_ptr<int> rawCache;
 Handle context{99}; bool contextFailed = false;
 void release();
};
''' + resource_body + r'''
}
int main() { internal::RenderTargetResources resources; resources.cache[1] = {42}; resources.release(); return resources.context.id != 99; }
'''
    (OUT/'release.cpp').write_text(fixture, encoding='utf-8')
    cpp = scratch/'release.cpp'; binary = scratch/'release'
    for variant in ['baseline', 'remove_guard']:
        cpp.write_text(fixture if variant == 'baseline' else fixture.replace('if (frameworkShutdown()) return;', ''), encoding='utf-8')
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (run.returncode == 0) == (variant == 'baseline')
        rows.append(dict(resource_release=variant, returncode=run.returncode))
    print('resource release guard and mutation PASS', flush=True)
    # A WindowContext also owns pending screenshot jobs. Late cancellation must
    # drop CPU members without enqueuing completions or logging a failure.
    screenshots = body((ROOT/'core/include/tc/app/tcScreenshot.cpp').read_text(encoding='utf-8'), 'void closeScreenshots(')
    fixture = r'''
#include <cstdlib>
#include <deque>
#include <memory>
namespace internal {
bool frameworkShutdown() noexcept { return true; }
struct TeardownSteps { template<class F> void run(const char*, F) { std::abort(); } };
enum class SaveError { WriteFailed };
struct SaveResult { static int fail(SaveError, const char*) { std::abort(); } };
struct ScreenshotQueue { std::deque<std::shared_ptr<int>> pending; };
void complete(const std::shared_ptr<int>&, int) { std::abort(); }
''' + screenshots + r'''
}
int main() { internal::ScreenshotQueue queue; queue.pending.push_back(std::make_shared<int>(7)); internal::closeScreenshots(queue); return queue.pending.size() != 1; }
'''
    (OUT/'screenshots.cpp').write_text(fixture, encoding='utf-8')
    cpp = scratch/'screenshots.cpp'; binary = scratch/'screenshots'
    for variant in ['baseline', 'remove_guard']:
        cpp.write_text(fixture if variant == 'baseline' else fixture.replace('if (frameworkShutdown()) return;', ''), encoding='utf-8')
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (run.returncode == 0) == (variant == 'baseline')
        rows.append(dict(screenshot_cancellation=variant, returncode=run.returncode))
    print('screenshot cancellation guard and mutation PASS', flush=True)
    # CPU entries must be erased while stopped; only static registry destruction
    # may prevent access. Compile the actual DebugName destructor for each case.
    debug_body = body((ROOT/'core/include/tc/utils/tcDebugName.cpp').read_text(encoding='utf-8'), 'DebugName::~DebugName()')
    fixture = r'''
#include <atomic>
#include <map>
#include <mutex>
#include <cstring>
namespace trussc::internal {
bool stopped = false;
bool frameworkShutdown() noexcept { return stopped; }
std::atomic<bool> debugRegistryDestroyed{false};
struct Registry { std::mutex mutex; std::map<unsigned, int> entries; } storage;
int visits = 0;
Registry& registry() { ++visits; return storage; }
struct DebugName { unsigned index_ = 4; ~DebugName(); };
''' + debug_body + r'''
}
int main(int argc, char** argv) {
    using namespace trussc::internal;
    if (argc != 2) return 2;
    stopped = std::strcmp(argv[1], "shutdown") == 0;
    debugRegistryDestroyed.store(std::strcmp(argv[1], "destroyed") == 0);
    storage.entries[4] = 1;
    { DebugName name; }
    const bool inactive = debugRegistryDestroyed.load();
    return visits != (inactive ? 0 : 1) || storage.entries.size() != (inactive ? 1u : 0u);
}
'''
    (OUT/'debug-name.cpp').write_text(fixture, encoding='utf-8')
    cpp = scratch/'debug-name.cpp'; binary = scratch/'debug-name'
    guard = 'debugRegistryDestroyed.load(std::memory_order_acquire)'
    assert guard in fixture
    for variant, modes, replacement in [
        ('baseline', ['active', 'shutdown', 'destroyed'], guard),
        ('skip_shutdown_erase', ['shutdown'], 'frameworkShutdown() || '+guard),
        ('remove_registry_mark', ['destroyed'], 'false'),
    ]:
        cpp.write_text(fixture.replace(guard, replacement), encoding='utf-8')
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        for mode in modes:
            run = subprocess.run([str(binary), mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            assert (run.returncode == 0) == (variant == 'baseline')
            rows.append(dict(debug_name=variant, mode=mode, returncode=run.returncode))
    print('debug-name lifetime guards and mutations PASS', flush=True)
    # Resource-load state destruction must also avoid its process-lifetime
    # registry once shutdown is published, even if credits remain outstanding.
    resource_dtor = body((ROOT/'core/include/tc/utils/tcResourceLoad.cpp').read_text(encoding='utf-8'), 'ResourceLoadState::~ResourceLoadState()')
    fixture = r'''
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace trussc::internal {
bool stopped = false;
bool frameworkShutdown() noexcept { return stopped; }
struct LoadHost { std::mutex mutex; };
LoadHost& host() { std::abort(); }
struct ResourceLoadState { int credits = 1, requestCredit = 1; ~ResourceLoadState(); };
void releaseCredits(LoadHost&, ResourceLoadState&) { std::abort(); }
''' + resource_dtor + r'''
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    using namespace trussc::internal;
    stopped = std::strcmp(argv[1], "shutdown") == 0;
    ResourceLoadState state;
    if (!stopped) state.credits = state.requestCredit = 0;
}
'''
    (OUT/'resource-state.cpp').write_text(fixture, encoding='utf-8')
    cpp = scratch/'resource-state.cpp'; binary = scratch/'resource-state'
    for variant, modes in [('baseline', ['idle', 'shutdown']), ('remove_guard', ['shutdown'])]:
        cpp.write_text(fixture if variant == 'baseline' else fixture.replace('if (frameworkShutdown()) return;', ''), encoding='utf-8')
        subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
        for mode in modes:
            run = subprocess.run([str(binary), mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            assert (run.returncode == 0) == (variant == 'baseline')
            rows.append(dict(resource_state=variant, mode=mode, returncode=run.returncode))
    print('resource state destruction guard and mutation PASS', flush=True)
    # Verify the actual early guard on the otherwise backend-heavy Web font
    # completion and resource cancellation bodies; the seam aborts if reached.
    source_pairs = [
        ('resource_cancel', ROOT/'core/include/tc/utils/tcResourceLoad.cpp', 'void ResourceLoadState::cancel()', 'if (frameworkShutdown()) return;'),
        ('web_font_finish', ROOT/'core/include/tc/graphics/tcResourceLoadGraphics.cpp', 'void Font::finishUrlAsync(', 'if (internal::frameworkShutdown()) return;'),
    ]
    for name, path, marker, guard in source_pairs:
        actual = body(path.read_text(encoding='utf-8'), marker)
        first = actual[actual.index('{')+1:].lstrip()
        assert first.startswith(guard), name + ' must guard before any body work'
        test_guard = guard.replace('internal::frameworkShutdown()', 'frameworkShutdown()')
        fixture = r'''
#include <cstdlib>
bool frameworkShutdown() noexcept { return true; }
void entry() {
''' + test_guard + r'''
 std::abort();
}
int main() { entry(); }
'''
        (OUT/(name+'.cpp')).write_text(fixture, encoding='utf-8')
        cpp = scratch/(name+'.cpp'); binary = scratch/name
        for variant in ['baseline', 'remove_guard']:
            cpp.write_text(fixture if variant == 'baseline' else fixture.replace(test_guard, ''), encoding='utf-8')
            subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            assert (run.returncode == 0) == (variant == 'baseline')
            rows.append(dict(resource_entry=name, variant=variant, returncode=run.returncode))
    print('resource cancellation and Web font early guards and mutations PASS', flush=True)
(OUT/'results.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
