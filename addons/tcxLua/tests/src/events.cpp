#include <tcxLua.h>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
using namespace trussc;
// The production headless runner resolves events through the attached App.
// Exercise that context deterministically without starting a timed host loop.
struct HeadlessEventContext {
    std::shared_ptr<internal::AppOwner> owner = std::make_shared<internal::AppOwner>();
    internal::AppOwnerScope scope{owner};
    HeadlessEventContext() { owner->context = &internal::nonWindowContext(); }
};
bool script(sol::state& lua, const char* source) {
    auto result = lua.safe_script(source, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error error = result;
        std::printf("  %s\n", error.what());
        return false;
    }
    return true;
}
}

int runLuaEventTests(const std::filesystem::path& temp) {
    HeadlessEventContext context;
    int failures = 0;
    auto check = [&](const char* label, bool success) {
        std::printf("Event: %-65s %s\n", label, success ? "PASS" : "FAIL");
        failures += !success;
    };
    tcxLua binding;
    auto state = binding.getLuaState();
    auto& lua = *state;
    Event<void> signal;
    lua["signal"] = &signal;
    lua.set_function("emit", [&] { signal.notify(); });
    check("removed method form is rejected", script(lua, R"(
        assert(not pcall(function() signal:listen({}, function(self) end) end))
    )"));
    check("closure capture survives GC until explicit disconnect", script(lua, R"(
        calls = 0
        do
            local self = {alive = true}
            weak = setmetatable({self}, {__mode = 'v'})
            handle = signal:listen(function() assert(self.alive); calls = calls + 1 end)
        end
        collectgarbage('collect'); emit()
        assert(weak[1] ~= nil and calls == 1)
        handle:disconnect(); handle:disconnect()
        emit()
        assert(calls == 1)
        collectgarbage('collect'); collectgarbage('collect')
        assert(weak[1] == nil)
    )"));
    check("same-notify disconnect and self-disconnect are immediate", script(lua, R"(
        local later
        local first = signal:listen(function() later:disconnect() end)
        later = signal:listen(function() calls = calls + 1000 end)
        emit(); first:disconnect(); assert(calls == 1)
        local selfHandle
        selfHandle = signal:listen(function() selfHandle:disconnect(); calls = calls + 1 end)
        emit(); emit(); assert(calls == 2)
    )"));
    tcxLua::clearEventListeners(lua);
    check("owner cleanup disconnects a captured self", script(lua, R"(
        local owner = {calls = 0}
        function owner:start()
            self.handle = signal:listen(function() self.calls = self.calls + 1; return self, 1, 2 end)
        end
        function owner:cleanup() self.handle:disconnect() end
        owner:start(); emit(); assert(owner.calls == 1)
        owner:cleanup(); emit(); assert(owner.calls == 1)
    )"));
    const int stackBeforeNotifications = lua_gettop(lua.lua_state());
    check("closure return values leave the Lua stack balanced", script(lua, R"(
        local handle = signal:listen(function() return {}, 1, 2 end)
        for i = 1, 100 do emit() end
        handle:disconnect()
    )") && lua_gettop(lua.lua_state()) == stackBeforeNotifications);
    lua.set_function("onMainThread", [] { return isMainThread(); });
    check("worker delivery setup", script(lua, R"(
        calls = 0
        handle = signal:listen(function() assert(onMainThread()); calls = calls + 1 end)
    )"));
    std::thread([&] { signal.notify(); }).join();
    check("worker does not enter Lua", lua["calls"].get<int>() == 0);
    internal::drainMainThreadQueue();
    check("queued delivery reaches main", lua["calls"].get<int>() == 1);
    std::thread([&] { signal.notify(); }).join();
    check("disconnect cancels queued delivery", script(lua, "handle:disconnect()"));
    internal::drainMainThreadQueue();
    check("canceled delivery did not fire", lua["calls"].get<int>() == 1);

    check("ignored handle survives GC until reload", script(lua, R"(
        signal:listen(function() calls = calls + 100 end)
        collectgarbage('collect'); collectgarbage('collect')
        emit(); assert(calls == 101)
    )"));
    std::thread([&] { signal.notify(); }).join();
    auto replacement = temp / "event-reload.lua";
    std::ofstream(replacement) << "reloaded = true\n";
    check("runFile clears all old subscriptions", tcxLua::runFile(lua, replacement) && signal.listenerCount() == 0);
    internal::drainMainThreadQueue();
    signal.notify();
    check("reload cancels old queued callbacks", lua["calls"].get<int>() == 101);

    // Test each newly exposed source with known payloads; real device production
    // is separate from the Lua bridge contract being checked here.
    Image image;
    SoundBuffer buffer;
    MicInput mic;
    lua["image"] = &image;
    lua["buffer"] = &buffer;
    lua["mic"] = &mic;
    auto priorRun = internal::appRunToken();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    const auto imagePath = temp / "event-image.ppm";
    { std::ofstream file(imagePath, std::ios::binary); file << "P6\n1 1\n255\n"; file.write("\xff\x00\x00", 3); }
    lua["eventImagePath"] = imagePath;
    lua["eventSoundPath"] = std::filesystem::path(__FILE__).parent_path().parent_path() / "data/samples.flac";
    check("async load calls defer their Lua completions", script(lua, R"(
        asyncReceived = {}
        image.onLoad:listen(function(result)
            assert(result:ok() and not image:isLoading()); asyncReceived.image = true
        end)
        buffer.onLoad:listen(function(result)
            assert(result:ok() and not buffer:isLoading()); asyncReceived.buffer = true
        end)
        image:loadAsync(eventImagePath)
        buffer:loadAsync(eventSoundPath)
        assert(image:isLoading() and buffer:isLoading())
        assert(not asyncReceived.image and not asyncReceived.buffer)
    )"));
    internal::waitForResourceLoadsForTests();
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    check("real async decoders fire in Lua", script(lua,
        "assert(asyncReceived.image and asyncReceived.buffer and buffer.numSamples == 8)"));
    tcxLua::clearEventListeners(lua);
    check("Image.loadAsync accepts the explicit mipmaps argument", script(lua,
        "image:loadAsync(eventImagePath, false); assert(image:isLoading()); image:clear()"));
    internal::waitForResourceLoadsForTests();
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
    internal::appRunToken() = priorRun;
    check("new event sources and console controls", script(lua, R"(
        received = {}
        local function mark(name, field, expected)
            return function(args) assert(onMainThread() and args[field] == expected); received[name] = true; savedArgs = args end
        end
        image.onLoad:listen(mark('image', 'message', 'image'))
        buffer.onLoad:listen(mark('buffer', 'message', 'buffer'))
        mic.onStart:listen(mark('start', 'success', true))
        mic.onDisconnect:listen(mark('mic', 'reason', 'mic'))
        local audio = AudioEngine.getInstance()
        audio.onDisconnect:listen(mark('audio', 'reason', 'audio'))
        audio.audioDeviceChanged:listen(mark('device', 'sampleRate', 12345))
        runtime():events().console:listen(mark('console', 'raw', 'hello lua'))
        console.stop(); assert(not console.isEnabled())
        console.start(); console.stop(); assert(not console.isEnabled())
        assert(not image:isLoading() and not buffer:isLoading())
        assert(type(Image.loadAsync) == 'function' and type(SoundBuffer.loadAsync) == 'function')
        for _, make in ipairs({TweenFloat, TweenVec2, TweenVec3, TweenColor}) do
            local tween = make()
            tween.complete:listen(function() received[tostring(make)] = true end)
            tween:start(); tween:finish()
            assert(received[tostring(make)])
        end
    )"));
    LoadResult loaded;
    loaded.message = "image"; image.onLoad.notify(loaded);
    loaded.message = "buffer"; buffer.onLoad.notify(loaded);
    MicStartResult started; started.success = true;
    std::thread([&] { mic.onStart.notify(started); }).join();
    check("typed worker event waits for main", script(lua, "assert(not received.start)"));
    internal::drainMainThreadQueue();
    DeviceDisconnectEventArgs disconnected;
    disconnected.reason = "mic"; mic.onDisconnect.notify(disconnected);
    disconnected.reason = "audio"; AudioEngine::getInstance().onDisconnect.notify(disconnected);
    AudioDeviceChangedArgs changed; changed.sampleRate = 12345;
    AudioEngine::getInstance().audioDeviceChanged.notify(changed);
    {
        ConsoleEventArgs input; input.raw = "hello lua";
        runtime().events().console.notify(input);
    }
    check("every new event fires; retained payload is owned", script(lua, R"(
        for _, name in ipairs({'image', 'buffer', 'start', 'mic', 'audio', 'device', 'console'}) do
            assert(received[name], name)
        end
        collectgarbage('collect'); assert(savedArgs.raw == 'hello lua')
    )"));
    check("event view retains its source, subscription does not", script(lua, R"(
        local owner = Image()
        local weak = setmetatable({owner}, {__mode = 'v'})
        local view = owner.onLoad
        owner = nil; collectgarbage('collect'); collectgarbage('collect')
        assert(weak[1] ~= nil)
        view:listen(function() end)
        view = nil; collectgarbage('collect'); collectgarbage('collect')
        assert(weak[1] == nil)
    )"));
    check("main-thread payload edits retain Event propagation semantics", script(lua, R"(
        events().keyPressed:listen(function(args) args.consumed = true; savedKey = args end)
    )"));
    KeyEventArgs key;
    key.key = 65;
    events().keyPressed.notify(key);
    check("consumed is written back before Event continues", key.consumed);
    check("saved payload survives native scope", script(lua, "assert(savedKey.key == 65 and savedKey.consumed)"));
    RectNodeButton button;
    lua["inheritedButton"] = &button;
    check("inherited event getter accepts the derived userdata", script(lua,
        "buttonCalls = 0; inheritedButton.mousePressed:listen(function(args) buttonCalls = buttonCalls + 1 end)"));
    MouseEventArgs pressed;
    button.mousePressed.notify(pressed);
    check("inherited event reaches Lua", lua["buttonCalls"].get<int>() == 1);
    int callbackErrors = 0;
    auto errorListener = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Error && args.message.find("event test error") != std::string::npos)
            ++callbackErrors;
    });
    check("callback error is contained", script(lua, R"(
        local handle = signal:listen(function() error('event test error') end)
        emit(); handle:disconnect()
    )") && callbackErrors == 1);
    check("App end clears subscriptions", script(lua, R"(
        signal:listen(function() error('after App end') end)
    )"));
    events().exit.notify();
    check("App end disconnected all", signal.listenerCount() == 0 && image.onLoad.listenerCount() == 0);
    check("state close setup", script(lua, "signal:listen(function() error('after state close') end)"));
    lua["signal"] = sol::lua_nil;
    state.reset();
    signal.notify();
    check("state close disconnects live subscriptions", signal.listenerCount() == 0);
    return failures;
}
