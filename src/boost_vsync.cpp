// Boost: force no VSync
// Calls eglSwapInterval(0) to disable VSync in ANGLE, removing the 60 FPS cap.

#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

static bool g_applied = false;

namespace boost_vsync {

    void apply() {
        // Disabled completely
    }

    void tryApply() {
        // Disabled completely
    }
}
