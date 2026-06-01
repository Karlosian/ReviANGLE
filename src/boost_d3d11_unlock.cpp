#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_d3d11_unlock {
    void apply() {
        if (!Config::get().d3d11_unlock) return;
        angle::log("d3d11_unlock: mutex removed (placeholder)");
    }
}
