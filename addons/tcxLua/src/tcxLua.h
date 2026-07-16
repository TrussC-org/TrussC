#pragma once

#include "sol/sol.hpp"
#include "tcxLuaPathAdapter.h"  // sol2 <-> std::filesystem::path (Lua string) conversion; must precede any path binding TU
#include "TrussC.h"

// namespace tcx::lua {

class tcxLua {
public:
    struct LuaModulePreferences {
        bool base = true;
        bool math = true;
        bool string = true;
        bool table = true;
        bool coroutine = true;
        bool io = false;
        bool package = false;
        bool os = false;
        bool debug = false;
        bool bit32 = false;
        bool ffi = false;
        bool utf8 = false;

        LuaModulePreferences() {}
    };

    std::shared_ptr<sol::state> getLuaState(LuaModulePreferences module_preference = LuaModulePreferences());
    void setBindings(const std::shared_ptr<sol::state>& lua);

    /// @brief Get Estimated Lua version by checking Lua feature from Lua code side
    /// @return 
    std::string getLuaVersionEstimated(const std::shared_ptr<sol::state>& lua);

    /// @brief Estimate LuaJIT is used or not by checking Lua feature from Lua code side
    /// @return 
    bool isLuaJITUsedEstimated(const std::shared_ptr<sol::state>& lua);

    /// @brief Get able to use LuaJIT or not from Sol2
    /// @return 
    bool canUseLuaJITFromSol2();

protected:
    void setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua);
    void setGeneratedTypeBindings(const std::shared_ptr<sol::state>& lua);   // luagen-types (Phase 2 usertypes)
    void setTypeBindings(const std::shared_ptr<sol::state>& lua);
    void setConstBindings(const std::shared_ptr<sol::state>& lua);
    void setColorConstBindings(const std::shared_ptr<sol::state>& lua);
    void setMathBindings(const std::shared_ptr<sol::state>& lua);

    template<typename TweenT, typename TweenValue>
    void defineTween(const std::shared_ptr<sol::state>& lua, std::string name){
        using EaseType = trussc::EaseType;
        using EaseMode = trussc::EaseMode;

        sol::usertype<TweenT> tween_t = lua->new_usertype<TweenT>(name,
            sol::constructors<
                TweenT(),
                TweenT(TweenValue, TweenValue, float),
                TweenT(TweenValue, TweenValue, float, EaseType),
                TweenT(TweenValue, TweenValue, float, EaseType, EaseMode)
                // FIXME: move constructor?
            >(),
            sol::call_constructor, sol::constructors<
                TweenT(),
                TweenT(TweenValue, TweenValue, float),
                TweenT(TweenValue, TweenValue, float, EaseType),
                TweenT(TweenValue, TweenValue, float, EaseType, EaseMode)
            >(),
            "from", &TweenT::from,
            "to", &TweenT::to,
            "duration", &TweenT::duration,
            "ease", sol::overload(
                [](TweenT& x, EaseType t) -> TweenT& { return x.ease(t); },
                [](TweenT& x, EaseType t, EaseMode m) -> TweenT& { return x.ease(t, m); },
                [](TweenT& x, EaseType t, EaseType o) -> TweenT& { return x.ease(t, o); }
            ),
            "loop", &TweenT::loop,
            "yoyo", &TweenT::yoyo,
            "delay", &TweenT::delay,
            "start", &TweenT::start,
            "pause", &TweenT::pause,
            "resume", &TweenT::resume,
            "reset", &TweenT::reset,
            "finish", &TweenT::finish,
            "getValue", &TweenT::getValue,
            "getProgress", &TweenT::getProgress,
            "getElapsed", &TweenT::getElapsed,
            "getDuration", &TweenT::getDuration,
            "isPlaying", &TweenT::isPlaying,
            "isComplete", &TweenT::isComplete,
            "getStart", &TweenT::getStart,
            "getEnd", &TweenT::getEnd,
            "getLoopCount", &TweenT::getLoopCount
        );
    }
};

// } // namespace tcx::lua