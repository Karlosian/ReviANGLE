#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

namespace boost_pcore_pin {
    void apply() {
        if (!Config::get().pcore_pin) return;

        THREAD_POWER_THROTTLING_STATE tpts = {0};
        tpts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        tpts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        tpts.StateMask = 0; // Turn off throttling for thread
        
        if (SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &tpts, sizeof(tpts))) {
            angle::log("pcore_pin: Thread Power Throttling disabled (pinned to P-cores/max freq)");
        } else {
            angle::log("pcore_pin: SetThreadInformation failed (OS may not support it)");
        }
    }
}
