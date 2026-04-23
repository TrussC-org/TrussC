#pragma once

#include "sol/sol.hpp"

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
};

// } // namespace tcx::lua