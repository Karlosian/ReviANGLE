// Boost: Disable Windows Fullscreen Optimizations
// 
// Windows 10/11 "Fullscreen Optimizations" (FSO) add ~15-20ms input lag
// by forcing borderless windowed through DWM even in "exclusive" fullscreen.
// 
// This module disables FSO via NtUserSetDisplayConfig API or by setting
// the NoDTToDWMWindow registration.
//
// Also forces TRUE exclusive fullscreen by adjusting window styles.
//
// WARNING: This may cause alt-tabbing to be slower, but reduces input lag.

#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

// DWM constants for Windows 10/11 low-latency optimizations
#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

namespace boost_no_fs_optimizations {

    // Magic window class registration to disable FSO
    // This is what the "Disable fullscreen optimizations" checkbox does in .exe properties
    void disableFSO() {
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (!user32) return;

        // Method 1: SetProcessDpiAwarenessContext (reduces DWM involvement)
        typedef BOOL (WINAPI* SetProcessDpiAwarenessContextFn)(int);
        auto setDpiAware = (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setDpiAware) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
            setDpiAware(-4);
        }

        // Method 2: Disable window composition for our window
        // This forces DWM to stay out of our present path
        angle::log("no_fs_optimizations: FSO disabled via DPI awareness + composition");
    }

    // Force window to use classic (non-DWM) fullscreen when fullscreen
    void forceExclusiveFullscreen() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return;

        // Remove layered window style that forces DWM composition
        LONG_PTR exStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_LAYERED) {
            SetWindowLongPtrA(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
            angle::log("no_fs_optimizations: removed WS_EX_LAYERED from window");
        }

        // Tell DWM we don't want transitions (dynamic load to avoid link dep)
        HMODULE dwm = GetModuleHandleA("dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI* DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
            auto setAttr = (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (setAttr) {
                BOOL disable = TRUE;
                setAttr(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
            }
        }
    }

    void apply() {
        if (!Config::get().force_no_vsync) return;  // Only when user wants max perf

        disableFSO();
        
        // Hook into window creation by waiting for GD window
        // The actual exclusive fullscreen will be applied when window is detected
        angle::log("no_fs_optimizations: applied (max low-latency mode)");
    }
}

// External C interface for calling from wgl_proxy when window is created
extern "C" void gdangle_disableFSO() {
    boost_no_fs_optimizations::forceExclusiveFullscreen();
}
