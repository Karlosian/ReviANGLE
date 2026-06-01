#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

namespace boost_process_perf {
    void apply() {
        if (!Config::get().process_perf) return;

        PROCESS_POWER_THROTTLING_STATE ppts = {0};
        ppts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        ppts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        ppts.StateMask = 0;
        
        if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ppts, sizeof(ppts))) {
            angle::log("process_perf: Power Throttling disabled (ExecutionSpeed forced)");
        } else {
            angle::log("process_perf: SetProcessInformation failed (OS may not support it)");
        }
    }
}
