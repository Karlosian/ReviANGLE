#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_effect_lod {
    void apply() {
        if (!Config::get().effect_lod) return;
        angle::log("effect_lod: LOD enabled");
    }
}
