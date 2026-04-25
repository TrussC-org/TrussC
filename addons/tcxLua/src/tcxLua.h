#pragma once

#include "sol/sol.hpp"
#include "TrussC.h"

// namespace tcx::lua {

class tcxLua {
public:
    std::shared_ptr<sol::state> getLuaState();
    void setBindings(const std::shared_ptr<sol::state>& lua);

protected:
    void setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua);
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