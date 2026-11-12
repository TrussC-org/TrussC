#pragma once
#include <type_traits>

// Event views keep their source userdata alive through a Lua-visible edge.
// Subscriptions deliberately do not retain this view: the source may be self.
namespace tcx_lua_detail {
template<class T>
sol::object eventView(trussc::Event<T>& event, sol::this_state state) {
    sol::object view = sol::make_object(state, std::ref(event));
    view.push();
    lua_createtable(state, 1, 0);
    lua_pushvalue(state, 1); // source userdata: property getter's self argument
    lua_rawseti(state, -2, 1);
    lua_setuservalue(state, -2);
    lua_pop(state, 1);
    return view;
}

template<class Owner = void, class Base, class T>
auto eventProperty(trussc::Event<T> Base::* member) {
    using Source = std::conditional_t<std::is_void_v<Owner>, Base, Owner>;
    return sol::property([member](Source& source, sol::this_state state) {
        return eventView(source.*member, state);
    });
}
}
