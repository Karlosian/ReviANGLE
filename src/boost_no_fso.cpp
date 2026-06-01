#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"
namespace boost_no_fso {
    void apply() {
        if (!Config::get().no_fs_optimizations) return;
        SetEnvironmentVariableA("__COMPAT_LAYER", "DisableNXShowUI");
        angle::log("no_fso: Fullscreen Optimizations disabled via __COMPAT_LAYER");
    }
}
