#include "tcxLua.h"

namespace {
using namespace trussc;
char sessionKey;

struct Subscription {
    EventListener listener;
    sol::main_protected_function function;
    void disconnect() {
        listener.disconnect();
        function = sol::main_protected_function{};
    }
    template<class... Args>
    void invoke(Args&&... args) {
        if (!function.valid()) return;
        // A callback can disconnect itself while it is running.
        auto callback = function;
        auto result = callback(std::forward<Args>(args)...);
        if (!result.valid()) {
            sol::error error = result;
            logError("tcxLua") << error.what();
        }
    }
};

struct Session : std::enable_shared_from_this<Session> {
    std::vector<std::shared_ptr<Subscription>> subscriptions;
    EventListener exit, unload;
    ~Session() { clear(); }
    void clear() {
        for (auto& subscription : subscriptions) subscription->disconnect();
        subscriptions.clear();
    }
};

// A Lua handle never extends the registry or Lua state's lifetime.
struct Handle {
    std::weak_ptr<Session> session;
    std::weak_ptr<Subscription> subscription;
    void disconnect() {
        if (auto owner = session.lock()) {
            if (auto live = subscription.lock()) {
                live->disconnect();
                std::erase(owner->subscriptions, live);
            }
        }
    }
};

Session* session(lua_State* state) {
    lua_pushlightuserdata(state, &sessionKey);
    lua_rawget(state, LUA_REGISTRYINDEX);
    auto* value = static_cast<Session*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return value;
}

// No priority or Deliver parameter is exposed in Lua. Event itself handles the
// worker-to-main copy and cancellation before any Lua object is accessed.
template<class T>
void bindEvent(sol::state& lua, const char* name) {
    auto subscribe = [](Event<T>& event, sol::protected_function function, sol::this_state state) {
        auto entry = std::make_shared<Subscription>();
        entry->function = std::move(function);
        std::weak_ptr<Subscription> weak = entry;
        if constexpr (std::is_void_v<T>) {
            entry->listener = event.listen([weak] {
                if (auto live = weak.lock()) live->invoke();
            }, Deliver::Main);
        } else {
            static_assert(!internal::isAudioEvent<T>);
            entry->listener = event.listen([weak](T& args) {
                // Lua may retain the payload after this callback returns.
                if (auto live = weak.lock(); live && live->function.valid()) {
                    using Payload = std::remove_const_t<T>;
                    auto payload = sol::make_object(live->function.lua_state(), Payload(args));
                    live->invoke(payload);
                    if constexpr (!std::is_const_v<T>) args = payload.template as<Payload&>();
                }
            }, Deliver::Main);
        }
        auto* owner = session(state);
        owner->subscriptions.push_back(entry);
        return Handle{owner->weak_from_this(), entry};
    };
    // The overload matcher enforces arity even for this single signature.
    lua.new_usertype<Event<T>>(name, sol::no_constructor, "listen", sol::overload(subscribe));
}
}

void tcxLua::clearEventListeners(sol::state& lua) {
    if (auto* value = session(lua.lua_state())) value->clear();
}

void tcxLua::setEventBindings(const std::shared_ptr<sol::state>& lua) {
    auto value = std::make_shared<Session>();
    // The registry owns the session. Event callbacks use weak_ptr, so neither
    // callbacks nor handles keep a closed state alive. __gc releases sol refs
    // while lua_close still has a valid registry.
    lua->registry()["tcxLua.eventSession"] = value;
    lua_pushlightuserdata(lua->lua_state(), &sessionKey);
    lua_pushlightuserdata(lua->lua_state(), value.get());
    lua_rawset(lua->lua_state(), LUA_REGISTRYINDEX);
    CoreEvents* hostEvents = internal::nonWindowContext().coreEvents;
    if (auto owner = internal::currentAppOwner(); owner && owner->context)
        hostEvents = owner->context->coreEvents;
    else if (auto* window = internal::resolveWindow()) hostEvents = &window->events();
    std::weak_ptr<Session> weak = value;
    value->exit = hostEvents->exit.listen([weak] {
        if (auto live = weak.lock()) live->clear();
    }, Deliver::Main, EventPriority::BeforeApp);
    value->unload = hostEvents->hotReloadUnload.listen([weak] {
        if (auto live = weak.lock()) live->clear();
    }, Deliver::Main, EventPriority::BeforeApp);
    lua->new_usertype<Handle>("EventHandle", sol::no_constructor, "disconnect", &Handle::disconnect);
    bindEvent<void>(*lua, "EventVoid");
#define TCXLUA_EVENT(Type) bindEvent<Type>(*lua, "Event" #Type)
    TCXLUA_EVENT(LoadResult);
    TCXLUA_EVENT(DeviceDisconnectEventArgs);
    TCXLUA_EVENT(AudioDeviceChangedArgs);
    TCXLUA_EVENT(MicStartResult);
    TCXLUA_EVENT(ConsoleEventArgs);
    TCXLUA_EVENT(ExitRequestEventArgs);
    TCXLUA_EVENT(DeviceLostEventArgs);
    TCXLUA_EVENT(FilesOpenedEventArgs);
    TCXLUA_EVENT(KeyEventArgs);
    TCXLUA_EVENT(MouseEventArgs);
    TCXLUA_EVENT(MouseMoveEventArgs);
    TCXLUA_EVENT(MouseDragEventArgs);
    TCXLUA_EVENT(ScrollEventArgs);
    TCXLUA_EVENT(CloseRequestEventArgs);
    TCXLUA_EVENT(ResizeEventArgs);
    TCXLUA_EVENT(OccludedEventArgs);
    TCXLUA_EVENT(FocusEventArgs);
    TCXLUA_EVENT(DragDropEventArgs);
    TCXLUA_EVENT(ClipboardPastedEventArgs);
    TCXLUA_EVENT(TouchEventArgs);
    TCXLUA_EVENT(UdpReceiveEventArgs);
    TCXLUA_EVENT(UdpErrorEventArgs);
    TCXLUA_EVENT(TcpClientConnectEventArgs);
    TCXLUA_EVENT(TcpServerReceiveEventArgs);
    TCXLUA_EVENT(TcpClientDisconnectEventArgs);
    TCXLUA_EVENT(TcpServerErrorEventArgs);
    TCXLUA_EVENT(TcpSendCompleteEventArgs);
    TCXLUA_EVENT(TcpConnectEventArgs);
    TCXLUA_EVENT(TcpReceiveEventArgs);
    TCXLUA_EVENT(TcpDisconnectEventArgs);
    TCXLUA_EVENT(TcpErrorEventArgs);
    TCXLUA_EVENT(VideoErrorEventArgs);
#undef TCXLUA_EVENT
    auto console = lua->create_named_table("console");
    console.set_function("start", &trussc::console::start);
    console.set_function("stop", &trussc::console::stop);
    console.set_function("isEnabled", &trussc::console::isEnabled);
}
