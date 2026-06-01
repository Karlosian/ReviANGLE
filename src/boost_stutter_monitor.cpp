#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_stutter_monitor {
    void apply() {
        if (!Config::get().stutter_monitor) return;
        angle::log("stutter_monitor: initialized");
    }
}
