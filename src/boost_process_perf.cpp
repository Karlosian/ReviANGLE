// Boost: Process / thread performance lock
// ------------------------------------------------------------------------
// Windows 10 1709+ has a "Power Throttling" feature that the scheduler can
// apply to any process it decides is "background-ish". Once throttled, the
// process is forced onto E-cores (on hybrid CPUs), capped at a lower clock,
// and may have its memory paged. The detection heuristic is opaque and
// occasionally false-positives on fullscreen games. The result is visible
// as random multi-millisecond stutters that don't correlate with any work.
//
// We explicitly opt-out via SetProcessInformation(ProcessPowerThrottling,
// EXECUTION_SPEED=0). EXECUTION_SPEED=0 means "never throttle", regardless
// of what the scheduler thinks. This is documented and supported since
// 1709 — older Windows builds get a benign ERROR_INVALID_PARAMETER which
// we log and move on.
//
// We ALSO set the same flag on the render thread (the one that calls
// eglSwapBuffers / wglSwapBuffers). The render thread is the one that
// most often gets misclassified because it's frequently waiting on the
// GPU / VBlank.
//
// Bonus: on Win11 23H2+ there's PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
// — when set, our 1ms timer resolution (from boost_timer_fix) isn't
// invalidated by the OS's anti-power-creep cleanup. We try it; old kernels
// reject with ERROR_INVALID_PARAMETER and we move on.

#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED        0x1
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION         0x1
#endif

typedef struct _PROCESS_POWER_THROTTLING_STATE_X {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE_X;

// Same struct layout for ThreadPowerThrottling.
typedef PROCESS_POWER_THROTTLING_STATE_X THREAD_POWER_THROTTLING_STATE_X;

#ifndef ProcessPowerThrottling
#define ProcessPowerThrottling 4
#endif
#ifndef ThreadPowerThrottling
#define ThreadPowerThrottling  4
#endif

using SetProcInfoFn = BOOL(WINAPI*)(HANDLE, int, PVOID, DWORD);
using SetThreadInfoFn = BOOL(WINAPI*)(HANDLE, int, PVOID, DWORD);

namespace boost_process_perf {

    void apply() {
        const auto& cfg = Config::get();
        if (!cfg.process_perf_lock) return;

        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        if (!k32) {
            angle::log("process_perf: kernel32 not loaded?!");
            return;
        }
        auto setProcInfo  = (SetProcInfoFn)  GetProcAddress(k32, "SetProcessInformation");
        auto setThreadInfo = (SetThreadInfoFn)GetProcAddress(k32, "SetThreadInformation");
        if (!setProcInfo) {
            angle::log("process_perf: SetProcessInformation unavailable (pre-Win10 1709?)");
            return;
        }

        // Process level: never throttle execution speed.
        PROCESS_POWER_THROTTLING_STATE_X ps = {};
        ps.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        ps.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        ps.StateMask   = 0; // 0 = disable throttling (we explicitly want full speed)

        if (setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof(ps))) {
            angle::log("process_perf: ExecutionSpeed throttling DISABLED (process-wide)");
        } else {
            angle::log("process_perf: SetProcessInformation(ExecutionSpeed) failed err=%lu",
                       GetLastError());
        }

        // Bonus: ignore-timer-resolution (Win11 23H2+).
        ps.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        ps.StateMask   = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        if (setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof(ps))) {
            angle::log("process_perf: TimerResolution kept (Win11 23H2 path)");
        }
        // Don't log failure — pre-23H2 always fails here.

        // Render thread: opt-out too. We can't easily reach into another thread,
        // but the THREAD that calls boost_process_perf::apply() == the same
        // thread that just finished postGLInit, which is the cocos2d main
        // thread = render thread.
        if (setThreadInfo) {
            THREAD_POWER_THROTTLING_STATE_X ts = {};
            ts.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            ts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            ts.StateMask   = 0;

            if (setThreadInfo(GetCurrentThread(), ThreadPowerThrottling, &ts, sizeof(ts))) {
                angle::log("process_perf: render-thread throttling DISABLED");
            }
        }
    }
}
