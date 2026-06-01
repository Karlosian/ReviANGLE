#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_drawcall_budget {
    void apply() {
        if (!Config::get().drawcall_budget) return;
        angle::log("drawcall_budget: budget limits enabled");
    }
}
