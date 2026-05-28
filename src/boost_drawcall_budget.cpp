// =====================================================================
//  Boost: Draw Call Budget  (drawcall_budget)
// =====================================================================
//
//  Что делает:
//    Гарантирует минимально допустимый FPS на ЛЮБОМ ПК (старом и новом)
//    путём адаптивного пропуска декоративных draw-call-ов при просадке.
//
//  Принцип работы:
//    Каждый кадр замеряем время от начала кадра (после SwapBuffers) до
//    момента следующего draw call. Если накопленное время отрисовки
//    превышает frame_budget_ms — включаем «экономный режим»:
//      * GL_POINTS батчи (particles CCParticleSystemPoint) — пропускаются
//      * GL_TRIANGLES / GL_TRIANGLE_STRIP батчи с count < SMALL_BATCH_THRESHOLD
//        (декоративная геометрия, мелкие эффекты) — пропускаются
//      * GL_TRIANGLES батчи count >= SMALL_BATCH_THRESHOLD — ВСЕГДА проходят
//        (иконка игрока, ground, tiles, UI — их нельзя резать)
//
//    «Экономный режим» сбрасывается на следующем кадре. Гистерезис:
//    уровень throttle снижается плавно (не прыгает туда-сюда каждый кадр).
//
//  Почему это не ломает картинку:
//    1. Иконка игрока в GD — GL_TRIANGLES батч >= 12 вершин (quad × 2 tri).
//    2. Ground / background — очень крупные батчи (сотни вершин).
//    3. UI / счётчики — GD рисует их в конце кадра крупными батчами.
//    4. Particles GL_POINTS при просадке уже невидимы (они мелкие и быстрые).
//    5. Маленькие GL_TRIANGLES (< 6 вершин) — одиночные декоративные квады,
//       которых на экране тысячи; их исчезновение незаметно глазу.
//
//  Параметры конфига [BoostPipeline]:
//    drawcall_budget        — bool — главный switch (default true)
//    drawcall_budget_min_fps — int  — целевой минимальный FPS (default 28)
//
//  Авторы: ReviANGLE / Reviusion
// =====================================================================

#include <windows.h>
#include <atomic>
#include "config.hpp"
#include "gl_proxy.hpp"
#include "angle_loader.hpp"

typedef unsigned int GLenum;
typedef int          GLint;
typedef int          GLsizei;

constexpr GLenum GL_POINTS         = 0x0000;
constexpr GLenum GL_LINES          = 0x0001;
constexpr GLenum GL_TRIANGLES      = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN   = 0x0006;

// draw calls with count < this are considered "decorative small batches"
static constexpr GLsizei SMALL_BATCH_THRESHOLD = 12;

namespace boost_drawcall_budget {

static bool                 g_enabled      = false;
static double               g_budgetMs     = 35.7;  // 1000/28
static LARGE_INTEGER        s_freq         = {};

// Per-frame state — reset at the start of each frame (BeginFrame).
static LARGE_INTEGER        s_frameStart   = {};
// Throttle level: 0=off, 1=drop points, 2=drop points+small tris
static std::atomic<int>     g_throttle{0};
// Hysteresis: frames at current load
static int                  s_heavyFrames  = 0;
static int                  s_lightFrames  = 0;

// Original driver entry points
using DrawArraysFn   = void (WINAPI*)(GLenum, GLint, GLsizei);
using DrawElementsFn = void (WINAPI*)(GLenum, GLsizei, GLenum, const void*);
static DrawArraysFn   s_origDrawArrays   = nullptr;
static DrawElementsFn s_origDrawElements = nullptr;

// ---- hot-path helpers ---------------------------------------------------

static inline double elapsedFrameMs() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - s_frameStart.QuadPart) * 1000.0
           / (double)s_freq.QuadPart;
}

// Returns true if this draw should be skipped given current throttle level.
static inline bool shouldSkipArrays(GLenum mode, GLsizei count) {
    int t = g_throttle.load(std::memory_order_relaxed);
    if (t == 0) return false;
    // Level 1+: drop all GL_POINTS (particle systems)
    if (mode == GL_POINTS) return true;
    // Level 2: also drop small decorative triangle batches
    if (t >= 2 && (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN)
               && count < SMALL_BATCH_THRESHOLD) return true;
    return false;
}

static inline bool shouldSkipElements(GLenum mode, GLsizei count) {
    int t = g_throttle.load(std::memory_order_relaxed);
    if (t == 0) return false;
    if (t >= 2 && (mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN)
               && count < SMALL_BATCH_THRESHOLD) return true;
    return false;
}

// ---- hooked entry points ------------------------------------------------

extern "C" void gdangle_dcBudgetDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!shouldSkipArrays(mode, count)) {
        if (s_origDrawArrays) s_origDrawArrays(mode, first, count);
    }
}

extern "C" void gdangle_dcBudgetDrawElements(GLenum mode, GLsizei count,
                                              GLenum type, const void* indices) {
    if (!shouldSkipElements(mode, count)) {
        if (s_origDrawElements) s_origDrawElements(mode, count, type, indices);
    }
}

extern "C" void* gdangle_dcBudgetGetDrawArraysHook() {
    return g_enabled ? (void*)gdangle_dcBudgetDrawArrays : nullptr;
}
extern "C" void* gdangle_dcBudgetGetDrawElementsHook() {
    return g_enabled ? (void*)gdangle_dcBudgetDrawElements : nullptr;
}

// ---- per-frame tick (called from wglSwapBuffers) ------------------------
//
// Called AFTER the real eglSwapBuffers — measures how long the previous
// frame took and adjusts the throttle level for the NEXT frame.

extern "C" void gdangle_dcBudgetTick(bool skipped) {
    if (!g_enabled || Config::get().megahack_detected) return;
    if (skipped) return;  // idle frame — don't influence throttle

    double ms = elapsedFrameMs();

    // Decide new desired throttle based on frame time vs budget
    int desired;
    if      (ms < g_budgetMs * 0.90) desired = 0;  // well within budget
    else if (ms < g_budgetMs * 1.10) desired = 1;  // marginal — drop particles
    else                             desired = 2;   // over budget — drop small tris too

    int current = g_throttle.load(std::memory_order_relaxed);

    if (desired > current) {
        // Ramp UP throttle immediately — we're already late
        s_heavyFrames++;
        s_lightFrames = 0;
        if (s_heavyFrames >= 2) {  // require 2 heavy frames before stepping up
            g_throttle.store(desired, std::memory_order_release);
            if (desired != current)
                angle::log("dcBudget: frame=%.1f ms -> throttle %d->%d",
                           ms, current, desired);
            s_heavyFrames = 0;
        }
    } else if (desired < current) {
        // Ramp DOWN throttle slowly - hysteresis prevents oscillation
        s_lightFrames++;
        s_heavyFrames = 0;
        if (s_lightFrames >= 8) {  // require 8 light frames before stepping down
            int next = current - 1;
            g_throttle.store(next, std::memory_order_release);
            angle::log("dcBudget: frame=%.1f ms -> throttle %d->%d (easing)",
                       ms, current, next);
            s_lightFrames = 0;
        }
    } else {
        s_heavyFrames = 0;
        s_lightFrames = 0;
    }

    // Reset frame start timer for the next frame
    QueryPerformanceCounter(&s_frameStart);
}

// Called at the START of each wglSwapBuffers (before present) to mark
// the beginning of the new frame's budget window.
extern "C" void gdangle_dcBudgetBeginFrame() {
    if (!g_enabled || Config::get().megahack_detected) return;
    QueryPerformanceCounter(&s_frameStart);
}

// ---- module lifecycle ---------------------------------------------------

void apply() {
    auto& cfg = Config::get();
    if (!cfg.drawcall_budget) return;

    s_origDrawArrays   = (DrawArraysFn)  glproxy::resolve("glDrawArrays");
    s_origDrawElements = (DrawElementsFn)glproxy::resolve("glDrawElements");
    if (!s_origDrawArrays || !s_origDrawElements) {
        angle::log("dcBudget: GL entry points unavailable, disabled");
        return;
    }

    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_frameStart);

    int minFps = cfg.drawcall_budget_min_fps > 0 ? cfg.drawcall_budget_min_fps : 28;
    g_budgetMs = 1000.0 / (double)minFps;
    g_enabled  = true;

    angle::log("dcBudget: ENABLED (min_fps=%d, budget=%.1f ms)", minFps, g_budgetMs);
}

} // namespace boost_drawcall_budget
