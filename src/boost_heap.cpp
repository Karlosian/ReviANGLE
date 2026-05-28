// Boost: background heap compaction
// Runs HeapCompact periodically on a background thread to reduce memory
// fragmentation. Only activates when the GD window is NOT focused (to avoid
// hitching during gameplay).

#include <windows.h>
#include <thread>
#include <atomic>
#include "config.hpp"
#include "angle_loader.hpp"

static std::thread* g_pThread = nullptr;  // pointer so we can join on shutdown
static std::atomic<bool> g_stop{false};
static std::atomic<bool> g_running{false};

static void heapWorker(int intervalSec) {
    g_running.store(true, std::memory_order_release);
    while (!g_stop.load(std::memory_order_acquire)) {
        for (int i = 0; i < intervalSec * 10 && !g_stop.load(std::memory_order_acquire); i++) {
            Sleep(100);
        }
        if (g_stop.load(std::memory_order_acquire)) break;

        // only compact when not in foreground
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == GetCurrentProcessId()) continue;

        HeapCompact(GetProcessHeap(), 0);
    }
    g_running.store(false, std::memory_order_release);
}

namespace boost_heap {

    void apply() {
        int interval = Config::get().heap_compact_interval;
        if (interval <= 0) return;

        g_stop.store(false, std::memory_order_release);
        g_pThread = new std::thread(heapWorker, interval);
        // Keep thread joinable for clean shutdown
        angle::log("heap_compact: active, interval=%ds (background only)", interval);
    }

    void shutdown() {
        g_stop.store(true, std::memory_order_release);
        if (g_pThread && g_pThread->joinable()) {
            g_pThread->join();
        }
        delete g_pThread;
        g_pThread = nullptr;
    }
}
