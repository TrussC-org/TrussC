#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <string>
#include <vector>

#include WEB_DROP_APP_HEADER

#ifndef __EMSCRIPTEN__
// Only the browser boundary and state are substituted in the native build.
static struct {
    struct { bool enabled = true; } drop;
    sapp_desc desc = {};
} _sapp_tc;
static std::vector<std::string> _sapp_tc_dropped_paths(1);
static double fileSize;
static int readers;
static sapp_html5_fetch_request pending;
using _sapp_tc_html5_fetch_callback = void (*)(const sapp_html5_fetch_response*);
static void _sapp_tc_emsc_invoke_fetch_cb(int, int, int, _sapp_tc_html5_fetch_callback,
    uint32_t, void*, uint32_t, void*);
static void _sapp_tc_js_fetch_dropped_file(int index,
    void (*callback)(const sapp_html5_fetch_response*), void* buffer,
    uint32_t capacity, void* user, uint32_t = 0, int = 0) {
    ++readers;
    pending = {index, callback, {buffer, capacity}, user};
}
#include "web_drop_preflight.inc"
static BrowserSize _sapp_tc_js_dropped_file_size(int index) {
    if (index != 0) return 0;
    if constexpr (std::is_same_v<BrowserSize, double>) return fileSize;
    return static_cast<BrowserSize>(static_cast<uint64_t>(fileSize));
}
static void setFile(double size) { fileSize = size; }
static int readerCount() { return readers; }
static void finishRead(int mode) {
    const unsigned char bytes[] = {1, 2, 3, 4};
    if (mode == 0) memcpy(const_cast<void*>(pending.buffer.ptr), bytes, sizeof(bytes));
    _sapp_tc_emsc_invoke_fetch_cb(pending.dropped_file_index, mode == 0,
        mode == 0 ? 0 : 2, pending.callback, mode == 0 ? 4 : 0,
        const_cast<void*>(pending.buffer.ptr), pending.buffer.size, pending.user_data);
}
#else
EM_JS(void, installBrowser, (), {
    Module.sapp_emsc_target = {addEventListener() {}, removeEventListener() {}};
    globalThis.window = {addEventListener() {}, removeEventListener() {}};
    globalThis.document = {querySelector() { return Module.sapp_emsc_target; }, addEventListener() {}, removeEventListener() {}};
    specialHTMLTargets[1] = globalThis.document;
    specialHTMLTargets[2] = globalThis.window;
    Module.sokol_dropped_files = [{size: 4}];
    Module.testReaders = [];
    Module.testThrow = 0;
    globalThis.FileReader = class {
        constructor() {
            if (Module.testThrow === 1) throw new Error('reader unavailable');
            Module.testReaders.push(this);
        }
        readAsArrayBuffer(file) {
            this.file = file;
            if (Module.testThrow === 2) throw new Error('read unavailable');
        }
        abort() {
            this.aborts = (this.aborts || 0) + 1;
            if (this.onabort) this.onabort();
            if (this.abortThrows) throw new Error('abort unavailable');
        }
    };
});
EM_JS(void, transferLargeLocalData, (), {
    const text = 'D3b-日本語\n'.repeat(20000);
    Module.sokol_paste({clipboardData: {getData() {return text;}}});
    const files = Array.from({length: 20}, (_, index) => ({name: index + text, size: 4}));
    Module.sokol_drop({stopPropagation() {}, preventDefault() {}, dataTransfer: {files}});
    Module.sokol_dropped_files = [{size: 4}];
});
EM_JS(void, setFile, (double size), { Module.sokol_dropped_files = [{size: size}]; });
EM_JS(int, readerCount, (), { return Module.testReaders.length; });
EM_JS(void, finishRead, (int mode), {
    const reader = Module.testReaders[Module.testReaders.length - 1];
    if (mode === 0) reader.onload({target: {result: new Uint8Array([1, 2, 3, 4]).buffer}});
    if (mode === 1) reader.onerror();
    if (mode === 2) reader.onabort();
    if (mode === 3) reader.onload({target: {result: new ArrayBuffer(9)}});
    if (mode === 4) reader.onload({target: {result: {byteLength: 268435457}}});
    if (mode === 5) reader.onload({target: {result: new ArrayBuffer(0)}});
});
EM_JS(void, finishTwice, (), {
    const reader = Module.testReaders[Module.testReaders.length - 1];
    const onload = reader.onload;
    const event = {target: {result: new Uint8Array([1, 2, 3, 4]).buffer}};
    onload(event);
    onload(event);
});
EM_JS(void, setThrow, (int mode), { Module.testThrow = mode; });
EM_JS(void, finishAll, (), {
    for (const reader of Module.testReaders) {
        if (reader.onerror) reader.onerror();
    }
});
EM_JS(int, activeReaders, (), { return Module.sokol_active_drop_readers || 0; });
EM_JS(void, retainOldReaders, (), {
    Module.testOldReaders = Module.testReaders.slice(-4).map(reader => ({
        reader: reader, onload: reader.onload, onerror: reader.onerror, onabort: reader.onabort
    }));
    // Even an abort exception must leave every slot closed before cleanup.
    Module.testOldReaders[1].reader.abortThrows = true;
});
EM_JS(int, oldReadersReleased, (), {
    return Module.testOldReaders.every(({reader}) => reader.aborts === 1 &&
        reader.onload === null && reader.onerror === null && reader.onabort === null);
});
EM_JS(void, completeOldReaders, (), {
    for (const old of Module.testOldReaders) {
        for (let i = 0; i < 2; ++i) {
            old.onload({target: {result: new Uint8Array([9, 9, 9, 9]).buffer}});
            old.onerror();
            old.onabort();
        }
    }
});
#endif

static int failures;
static int checks;
static int callbacks;
static int warnings;
static sapp_html5_fetch_response last;
static void check(const char* name, bool ok) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}
static void receive(const sapp_html5_fetch_response* response) {
    ++callbacks;
    last = *response;
}
static void logMessage(const char*, uint32_t level, uint32_t, const char*, uint32_t, const char*, void*) {
    if (level == 2) ++warnings;
}
#if defined(__EMSCRIPTEN__) && defined(TEST_HAS_SIZE64)
static int cleanups;
static int lateCallbacks;
static void receiveOldRun(const sapp_html5_fetch_response* response) {
    if (cleanups) ++lateCallbacks;
    receive(response);
}
static void cleanup() {
    ++cleanups;
    check("readers are aborted and handlers cleared before application cleanup",
        activeReaders() == 0 && oldReadersReleased());
}
#endif
int main() {
#ifdef __EMSCRIPTEN__
    sapp_desc desc = {};
    desc.enable_dragndrop = true;
    desc.enable_clipboard = true;
    desc.logger.func = logMessage;
    _sapp_tc_init_state(&desc);
    installBrowser();
    _sapp_tc_js_add_dragndrop_listeners();
    _sapp_tc_js_add_clipboard_listener();
    transferLargeLocalData();
    std::string localText;
    for (int i = 0; i < 20000; ++i) localText += "D3b-日本語\n";
    check("Web paste over 64 KiB arrives whole through heap bridge", _sapp_tc_clipboard_text == localText);
    bool completeDrop = sapp_get_num_dropped_files() == 20;
    for (int i = 0; completeDrop && i < 20; ++i)
        completeDrop = sapp_get_dropped_file_path(i) == std::to_string(i) + localText;
    check("Web heap bridge retains 20 long filenames", completeDrop);

    _sapp_tc_dropped_paths.resize(1);
#endif
    _sapp_tc.desc.logger.func = logMessage;
    unsigned char buffer[8] = {};
    int user = 0;
    sapp_html5_fetch_request request = {0, receive, {buffer, sizeof(buffer)}, &user};
    auto rejected = [&](const char* name, double size, size_t capacity, sapp_html5_fetch_error error) {
        setFile(size);
        request.buffer.size = capacity;
        const int beforeReaders = readerCount();
        const int beforeCallbacks = callbacks;
        const int beforeWarnings = warnings;
        sapp_html5_fetch_dropped_file(&request);
        check(name, readerCount() == beforeReaders && callbacks == beforeCallbacks + 1 &&
            !last.succeeded && last.error_code == error && last.data.size == 0 &&
            last.file_index == request.dropped_file_index && last.buffer.ptr == buffer &&
            last.buffer.size == capacity && last.user_data == &user);
        check("rejected request logs one warning", warnings == beforeWarnings + 1);
    };
    rejected("size above 4 GiB is rejected before reading", 4294967300.0, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_BUFFER_TOO_SMALL);
    rejected("size above 4 GiB with a large buffer is rejected", 4294967300.0, UINT32_MAX, SAPP_HTML5_FETCH_ERROR_BUFFER_TOO_SMALL);
    rejected("content above limit is rejected before reading", 268435457.0, 536870912, SAPP_HTML5_FETCH_ERROR_OTHER);
    rejected("content larger than buffer is rejected", 9, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_BUFFER_TOO_SMALL);
    request.dropped_file_index = -1;
    rejected("negative file index is rejected", 4, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_OTHER);
    request.dropped_file_index = 1;
    rejected("file index past count is rejected", 4, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_OTHER);
    request.dropped_file_index = 0;
    setFile(4294967300.0);
#ifdef TEST_HAS_SIZE64
    check("64-bit getter retains full size", sapp_html5_get_dropped_file_size64(0) == UINT64_C(4294967300));
    check("legacy getter saturates at UINT32_MAX", sapp_html5_get_dropped_file_size(0) == UINT32_MAX);
    setFile(9007199254740991.0);
    check("64-bit getter retains largest safe integer", sapp_html5_get_dropped_file_size64(0) == UINT64_C(9007199254740991));
    check("unavailable file size is zero", sapp_html5_get_dropped_file_size64(-1) == 0 &&
        sapp_html5_get_dropped_file_size64(1) == 0);
    _sapp_tc.drop.enabled = false;
    check("disabled drops have zero file size", sapp_html5_get_dropped_file_size64(0) == 0);
    _sapp_tc.drop.enabled = true;
#else
    check("legacy getter does not wrap large sizes", sapp_html5_get_dropped_file_size(0) == UINT32_MAX);
#endif
    setFile(4);
    request.buffer.size = sizeof(buffer);
    const int beforeReaders = readerCount();
    const int beforeCallbacks = callbacks;
    sapp_html5_fetch_dropped_file(&request);
    check("normal file starts one reader", readerCount() == beforeReaders + 1 && callbacks == beforeCallbacks);
    finishRead(0);
    const unsigned char expected[] = {1, 2, 3, 4};
    check("normal file completes with bytes and request metadata", callbacks == beforeCallbacks + 1 &&
        last.succeeded && last.data.size == 4 && last.data.ptr == buffer &&
        last.buffer.size == sizeof(buffer) && last.user_data == &user && memcmp(buffer, expected, 4) == 0);
#if defined(__EMSCRIPTEN__) && defined(TEST_HAS_SIZE64)
    // Metadata-only files exercise the limits without reserving their content.
    for (double size : {-1.0, 1.5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        rejected("invalid file size is rejected", size, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_OTHER);
    }
    setFile(268435456);
    request.buffer.size = 268435456;
    int before = readerCount();
    sapp_html5_fetch_dropped_file(&request);
    check("content at limit is accepted", readerCount() == before + 1);
    finishRead(1);
    request.buffer.size = sizeof(buffer);
    setFile(4);
    before = readerCount();
    for (int i = 0; i < 4; ++i) sapp_html5_fetch_dropped_file(&request);
    check("four concurrent readers are accepted", readerCount() == before + 4);
    rejected("reader limit is rejected before reading", 4, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_OTHER);
    finishAll();
    before = readerCount();
    sapp_html5_fetch_dropped_file(&request);
    check("error releases reader slot", readerCount() == before + 1);
    finishRead(2);
    before = readerCount();
    sapp_html5_fetch_dropped_file(&request);
    check("abort releases reader slot", readerCount() == before + 1);
    finishRead(0);
    for (int mode : {1, 2}) {
        setThrow(mode);
        const int beforeCallback = callbacks;
        const int beforeWarning = warnings;
        sapp_html5_fetch_dropped_file(&request);
        check("reader exception completes once with warning", callbacks == beforeCallback + 1 &&
            !last.succeeded && last.error_code == SAPP_HTML5_FETCH_ERROR_OTHER && warnings == beforeWarning + 1);
    }
    setThrow(0);
    before = readerCount();
    for (int i = 0; i < 4; ++i) sapp_html5_fetch_dropped_file(&request);
    check("exceptions release all reader slots", readerCount() == before + 4);
    finishAll();
    for (int mode : {3, 4}) {
        memset(buffer, 0x5a, sizeof(buffer));
        request.buffer.size = mode == 3 ? sizeof(buffer) : 536870912;
        sapp_html5_fetch_dropped_file(&request);
        const int beforeCallback = callbacks;
        const int beforeWarning = warnings;
        finishRead(mode);
        check("result above content or buffer limit is rejected before copying", callbacks == beforeCallback + 1 &&
            !last.succeeded && last.data.size == 0 && warnings == beforeWarning + 1 &&
            last.error_code == (mode == 3 ? SAPP_HTML5_FETCH_ERROR_BUFFER_TOO_SMALL : SAPP_HTML5_FETCH_ERROR_OTHER) &&
            buffer[0] == 0x5a && buffer[7] == 0x5a);
    }
    request.buffer.size = sizeof(buffer);
    sapp_html5_fetch_dropped_file(&request);
    before = callbacks;
    finishTwice();
    check("duplicate completion invokes one callback", callbacks == before + 1 && last.succeeded);
    setFile(0);
    sapp_html5_fetch_dropped_file(&request);
    before = callbacks;
    finishRead(5);
    check("empty file completes successfully", callbacks == before + 1 && last.succeeded && last.data.size == 0);

    // Exercise the actual quit frame and C state teardown, then reuse this Web module.
    setFile(4);
    request.callback = receiveOldRun;
    before = readerCount();
    const int beforeShutdownCallbacks = callbacks;
    const int beforeShutdownWarnings = warnings;
    for (int i = 0; i < 4; ++i) sapp_html5_fetch_dropped_file(&request);
    check("shutdown starts with four pending reads", readerCount() == before + 4 &&
        activeReaders() == 4 && callbacks == beforeShutdownCallbacks);
    retainOldReaders();
    _sapp_tc.desc.cleanup_cb = cleanup;
    sapp_quit();
    check("quit frame cleans up without completing retired requests",
        !_sapp_tc_emsc_frame_animation_loop(1.0, nullptr) && cleanups == 1 &&
        activeReaders() == 0 && callbacks == beforeShutdownCallbacks && warnings == beforeShutdownWarnings);
    memset(buffer, 0x5a, sizeof(buffer));

    _sapp_tc_init_state(&desc);
    _sapp_tc_js_add_dragndrop_listeners();
    _sapp_tc_dropped_paths.resize(1);
    setFile(4);
    request.callback = receive;
    before = readerCount();
    sapp_html5_fetch_dropped_file(&request);
    check("fetch after reinitialization starts a reader", readerCount() == before + 1 &&
        activeReaders() == 1 && callbacks == beforeShutdownCallbacks);
    completeOldReaders();
    check("old completions cannot change the new run count or buffer", activeReaders() == 1 &&
        callbacks == beforeShutdownCallbacks && warnings == beforeShutdownWarnings &&
        lateCallbacks == 0 && buffer[0] == 0x5a && buffer[7] == 0x5a);
    finishRead(0);
    check("fetch after reinitialization succeeds and releases exactly one slot", activeReaders() == 0 &&
        callbacks == beforeShutdownCallbacks + 1 && last.succeeded && last.data.size == 4 &&
        last.buffer.ptr == buffer && last.buffer.size == sizeof(buffer) && last.user_data == &user &&
        memcmp(buffer, expected, 4) == 0);
    completeOldReaders();
    check("old completions after new success remain inert", activeReaders() == 0 &&
        callbacks == beforeShutdownCallbacks + 1 && lateCallbacks == 0 && warnings == beforeShutdownWarnings);
    before = readerCount();
    for (int i = 0; i < 4; ++i) sapp_html5_fetch_dropped_file(&request);
    check("new run retains exactly four reader slots", readerCount() == before + 4 && activeReaders() == 4);
    rejected("new run still rejects a fifth reader", 4, sizeof(buffer), SAPP_HTML5_FETCH_ERROR_OTHER);
    sapp_quit();
    check("second shutdown also releases all slots", !_sapp_tc_emsc_frame_animation_loop(2.0, nullptr) &&
        activeReaders() == 0);
#endif
#if defined(__EMSCRIPTEN__) && !defined(TEST_HAS_SIZE64)
    _sapp_tc_discard_state();
#endif
    printf("webDropSize: %d checks, %d failures, %d readers, %d callbacks, %d warnings\n",
        checks, failures, readerCount(), callbacks, warnings);
    return failures ? 1 : 0;
}
