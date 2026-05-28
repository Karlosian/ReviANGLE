// =====================================================================
//  Boost: Stutter Monitor / Мониторинг микрофризов
// =====================================================================
//
//  Что делает: отслеживает frame time variance и причины stuttering
//  когда FPS высокий, но есть лаги.
//
//  Метрики:
//    - Frame time variance (разброс времени между кадрами)
//    - 1% Low / 0.1% Low (худшие 1% и 0.1% кадров)
//    - CPU spikes (резкие задержки на CPU)
//    - GPU busy time (сколько времени GPU занят)
//    - Thread scheduling delays (задержки планирования потоков)
//
//  Диагностика причин:
//    - CPU bound: frame time > 16ms, GPU idle
//    - GPU bound: frame time > 16ms, GPU busy
//    - IO bound: frame time spikes correlate with disk IO
//    - Thread scheduling: frame time spikes correlate with thread wake latency
//
//  Параметры конфига [BoostStutterMonitor]:
//    stutter_monitor — bool, default OFF (включать только для диагностики)
//    stutter_log_threshold — float, default 2.0 (логировать кадры > 2x target frame time)
//    stutter_log_interval — int, default 100 (логировать каждые N кадров)
//
//  Авторы: ReviANGLE / Reviusion
// =====================================================================

#include <windows.h>
#include <cmath>
#include <algorithm>
#include "config.hpp"
#include "angle_loader.hpp"

namespace boost_stutter_monitor {

// Frame time statistics
struct FrameStats {
    static constexpr int HISTORY_SIZE = 1000;
    float frameTimes[HISTORY_SIZE];
    int index = 0;
    int count = 0;
    
    float min = FLT_MAX;
    float max = FLT_MIN;
    float avg = 0.0f;
    float variance = 0.0f;
    float percentile1 = 0.0f;  // 1% low
    float percentile01 = 0.0f; // 0.1% low
    
    int spikeCount = 0;  // кадры > threshold
    int totalFrames = 0;
};

static FrameStats g_stats;
static LARGE_INTEGER g_qpcFreq;
static LARGE_INTEGER g_lastFrameTime;
static float g_targetFrameTime = 16.666f; // 60 FPS default
static bool g_active = false;
static int g_logCounter = 0;

// Initialize QPC
static void initQPC() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_lastFrameTime);
}

// Update frame time statistics
static void updateStats(float frameTimeMs) {
    g_stats.frameTimes[g_stats.index] = frameTimeMs;
    g_stats.index = (g_stats.index + 1) % FrameStats::HISTORY_SIZE;
    if (g_stats.count < FrameStats::HISTORY_SIZE) g_stats.count++;
    g_stats.totalFrames++;
    
    // Update min/max
    if (frameTimeMs < g_stats.min) g_stats.min = frameTimeMs;
    if (frameTimeMs > g_stats.max) g_stats.max = frameTimeMs;
    
    // Calculate average
    float sum = 0.0f;
    for (int i = 0; i < g_stats.count; i++) {
        sum += g_stats.frameTimes[i];
    }
    g_stats.avg = sum / g_stats.count;
    
    // Calculate variance
    float varianceSum = 0.0f;
    for (int i = 0; i < g_stats.count; i++) {
        float diff = g_stats.frameTimes[i] - g_stats.avg;
        varianceSum += diff * diff;
    }
    g_stats.variance = varianceSum / g_stats.count;
    
    // Calculate percentiles (1% low, 0.1% low)
    if (g_stats.count >= 10) {
        // Copy and sort for percentile calculation
        float sorted[FrameStats::HISTORY_SIZE];
        for (int i = 0; i < g_stats.count; i++) sorted[i] = g_stats.frameTimes[i];
        std::sort(sorted, sorted + g_stats.count);
        
        int p1Index = (int)(g_stats.count * 0.01f);
        int p01Index = (int)(g_stats.count * 0.001f);
        g_stats.percentile1 = sorted[p1Index];
        g_stats.percentile01 = sorted[p01Index];
    }
}

// Analyze stutter cause
static const char* analyzeStutterCause(float frameTimeMs) {
    // Check if this is a spike
    float threshold = g_targetFrameTime * Config::get().stutter_log_threshold;
    if (frameTimeMs < threshold) return "normal";
    
    g_stats.spikeCount++;
    
    // Determine cause based on various factors
    // This is a simplified analysis - real implementation would need more data
    
    // Check if GPU is busy (via ANGLE if available)
    auto& a = angle::state();
    if (a.egl && a.display) {
        // Could query GPU time via ANGLE extensions
        // For now, assume GPU bound if frame time is consistently high
        if (g_stats.avg > g_targetFrameTime * 1.5f) {
            return "GPU_BOUND";
        }
    }
    
    // Check if this is a thread scheduling issue
    // If variance is high but average is low, likely scheduling
    if (g_stats.variance > 10.0f && g_stats.avg < g_targetFrameTime * 1.2f) {
        return "THREAD_SCHEDULING";
    }
    
    // Default: CPU spike
    return "CPU_SPIKE";
}

// Log frame time spike
static void logSpike(float frameTimeMs, const char* cause) {
    angle::log("stutter_monitor: FRAME SPIKE - %.2fms (target: %.2fms) cause: %s",
               frameTimeMs, g_targetFrameTime, cause);
    
    // Log additional context
    angle::log("stutter_monitor: stats - avg: %.2fms min: %.2fms max: %.2fms var: %.2fms",
               g_stats.avg, g_stats.min, g_stats.max, std::sqrt(g_stats.variance));
    angle::log("stutter_monitor: percentiles - 1%% low: %.2fms 0.1%% low: %.2fms",
               g_stats.percentile1, g_stats.percentile01);
}

// Public function to call at end of each frame
extern "C" void gdangle_stutterMonitorEndFrame() {
    if (!g_active || Config::get().megahack_detected) return;
    
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    
    float deltaMs = (float)(currentTime.QuadPart - g_lastFrameTime.QuadPart) * 1000.0f / g_qpcFreq.QuadPart;
    g_lastFrameTime = currentTime;
    
    // Skip first few frames (initialization noise)
    if (g_stats.totalFrames < 10) {
        updateStats(deltaMs);
        return;
    }
    
    // Update statistics
    updateStats(deltaMs);
    
    // Check for spike
    float threshold = g_targetFrameTime * Config::get().stutter_log_threshold;
    if (deltaMs > threshold) {
        const char* cause = analyzeStutterCause(deltaMs);
        logSpike(deltaMs, cause);
    }
    
    // Periodic log
    g_logCounter++;
    int logInterval = Config::get().stutter_log_interval;
    if (logInterval > 0 && g_logCounter >= logInterval) {
        g_logCounter = 0;
        angle::log("stutter_monitor: periodic - avg: %.2fms 1%% low: %.2fms 0.1%% low: %.2fms spikes: %d/%d",
                   g_stats.avg, g_stats.percentile1, g_stats.percentile01,
                   g_stats.spikeCount, g_stats.totalFrames);
    }
}

void apply() {
    if (!Config::get().stutter_monitor) {
        angle::log("stutter_monitor: disabled in config");
        return;
    }
    
    initQPC();
    
    // Determine target frame time from config
    int targetFps = Config::get().frame_pacing_target;
    if (targetFps > 0) {
        g_targetFrameTime = 1000.0f / targetFps;
    } else {
        // Auto-detect - assume 60 FPS for now
        g_targetFrameTime = 16.666f;
    }
    
    g_active = true;
    angle::log("stutter_monitor: ENABLED (target frame time: %.2fms, log threshold: %.2fx)",
               g_targetFrameTime, Config::get().stutter_log_threshold);
}

void shutdown() {
    if (!g_active) return;
    
    // Log final statistics
    angle::log("stutter_monitor: FINAL STATS - avg: %.2fms min: %.2fms max: %.2ms var: %.2fms",
               g_stats.avg, g_stats.min, g_stats.max, std::sqrt(g_stats.variance));
    angle::log("stutter_monitor: percentiles - 1%% low: %.2fms 0.1%% low: %.2fms",
               g_stats.percentile1, g_stats.percentile01);
    angle::log("stutter_monitor: spikes - %d/%d (%.2f%%)",
               g_stats.spikeCount, g_stats.totalFrames,
               100.0f * g_stats.spikeCount / std::max(1, g_stats.totalFrames));
    
    g_active = false;
}

} // namespace boost_stutter_monitor
