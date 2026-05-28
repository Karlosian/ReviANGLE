// =====================================================================
//  Boost: Adaptive Effect LOD  (effect_lod)
// =====================================================================
//
//  Что делает: динамически снижает качество ВТОРОСТЕПЕННЫХ визуальных
//  эффектов (частицы, мелкие decoration-объекты), КОГДА И ТОЛЬКО КОГДА
//  FPS падает ниже целевого порога. Основная картинка (геймплей, иконка
//  игрока, ground/background, UI) не трогается ни при каких условиях.
//
//  Стратегия:
//    Раз в секунду считаем средний FPS по последним 60 кадрам (через
//    счётчики draw-call-ов из gl_proxy). Сравниваем с целевым FPS:
//
//      - fps >= target * 0.95   →  level 0  (full quality, как ваниль)
//      - fps >= target * 0.80   →  level 1  (cap GL_POINTS до 75 % от max)
//      - fps >= target * 0.65   →  level 2  (cap до 50 %)
//      - fps <  target * 0.65   →  level 3  (cap до 30 %, опускаем редкие
//                                            мелкие GL_TRIANGLES батчи
//                                            размером < 6 vert)
//
//  Почему это безопасно:
//    1. Триггерится ТОЛЬКО на просадках. Если FPS на потолке — модуль
//       вообще не вмешивается в render-pipeline (zero overhead, only
//       один atomic load в hot path).
//    2. Cap на vertex count для GL_POINTS — это режим cocos2d
//       `CCParticleSystemPoint`, отдельный от иконки игрока (которая
//       рисуется как GL_TRIANGLES батчи size >= 6). Без артефактов.
//    3. Hysteresis: переходы между уровнями требуют 2 секунд устойчивого
//       состояния (никаких мерцаний LOD-ов туда-сюда).
//    4. На современных ПК эта логика никогда не активируется, потому что
//       FPS всегда выше порога.
//
//  Параметры конфига [BoostPipeline]:
//    effect_lod              — bool  — главный switch (default ON)
//    effect_lod_min_fps      — int   — минимально допустимый FPS, ниже
//                                       которого включается LOD
//                                       (0 = взять frame_pacing_target)
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

constexpr GLenum GL_POINTS    = 0x0000;
constexpr GLenum GL_TRIANGLES = 0x0004;

extern "C" unsigned long long gdangle_getDrawArraysCount();
extern "C" unsigned long long gdangle_getDrawElementsCount();

namespace boost_effect_lod {

// ---- state ---------------------------------------------------------------
static bool          g_enabled       = false;
static int           g_minFps        = 60;
static std::atomic<int> g_lodLevel{0}; // 0 = off, 1..3 = aggressive
static int           g_particleHardCap = 150; // initial; refreshed from cfg
static int           g_baseMax        = 150;

// frame timing — same QPC the rest of the engine uses
static LARGE_INTEGER s_freq           = {};
static LARGE_INTEGER s_windowStart    = {};
static int           s_framesInWindow = 0;
static int           s_pendingLevel   = 0;
static int           s_stableTicks    = 0;

// Hooks to the real driver entry points.
using DrawArraysFn   = void(WINAPI*)(GLenum, GLint, GLsizei);
using DrawElementsFn = void(WINAPI*)(GLenum, GLsizei, GLenum, const void*);
static DrawArraysFn   s_origDrawArrays   = nullptr;
static DrawElementsFn s_origDrawElements = nullptr;

// Map a LOD level to a multiplier on point-particle counts.
static float lodMultiplier(int level) {
    switch (level) {
        case 0: return 1.00f;
        case 1: return 0.75f;
        case 2: return 0.50f;
        default:return 0.30f;
    }
}

// ---- public hook entry points -------------------------------------------
//
// gl_proxy.cpp dispatches to these *before* the real driver call, so we can
// shrink the vertex count for transient point-particle batches. We touch
// nothing for indexed quad geometry (the icon, the ground, UI text, etc.).

extern "C" void gdangle_effectLodDrawArrays(GLenum mode, GLint first, GLsizei count) {
    int level = g_lodLevel.load(std::memory_order_relaxed);

    // Hot path: when LOD is OFF we forward straight through (single int load).
    if (!g_enabled || level == 0) {
        if (s_origDrawArrays) s_origDrawArrays(mode, first, count);
        return;
    }

    // Apply only to GL_POINTS. cocos2d's CCParticleSystemPoint streams
    // particles through this path. CCParticleSystemQuad uses indexed
    // GL_TRIANGLES via DrawElements and is intentionally not throttled
    // here — that path is handled by the existing particle_throttle module.
    if (mode == GL_POINTS) {
        float mul = lodMultiplier(level);
        GLsizei capped = (GLsizei)((float)count * mul);
        if (capped < 4) capped = 4;       // never empty out the particle entirely
        if (capped < count) count = capped;
    }

    if (s_origDrawArrays) s_origDrawArrays(mode, first, count);
}

extern "C" void gdangle_effectLodDrawElements(GLenum mode, GLsizei count,
                                              GLenum type, const void* idx) {
    int level = g_lodLevel.load(std::memory_order_relaxed);

    if (!g_enabled || level == 0) {
        if (s_origDrawElements) s_origDrawElements(mode, count, type, idx);
        return;
    }

    // At the most aggressive level we drop *very small* triangle batches
    // (< 6 vertices = single quad of decorative geometry). The icon, ground
    // and UI batches are never under 6 because cocos2d batches them
    // together (V3F_C4B_T2F_Quad arrays). So this drops only the rare,
    // unbatched effect quad — safe.
    if (level >= 3 && mode == GL_TRIANGLES && count > 0 && count < 6) {
        return;
    }

    if (s_origDrawElements) s_origDrawElements(mode, count, type, idx);
}

// Public hook getters used by gl_proxy.cpp's draw dispatchers.
extern "C" void* gdangle_effectLodGetDrawArraysHook() {
    return g_enabled ? (void*)gdangle_effectLodDrawArrays : nullptr;
}
extern "C" void* gdangle_effectLodGetDrawElementsHook() {
    return g_enabled ? (void*)gdangle_effectLodDrawElements : nullptr;
}

// ---- per-frame tick (called from wgl_wglSwapBuffers once per frame) ------
extern "C" void gdangle_effectLodTick() {
    if (!g_enabled) return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    s_framesInWindow++;

    double elapsed = (double)(now.QuadPart - s_windowStart.QuadPart) /
                     (double)s_freq.QuadPart;
    if (elapsed < 0.5) return;            // sample twice a second

    double fps = (double)s_framesInWindow / elapsed;
    s_framesInWindow = 0;
    s_windowStart    = now;

    // Decide target level based on FPS / target-FPS ratio.
    double ratio = fps / (double)g_minFps;
    int target;
    if      (ratio >= 0.95) target = 0;
    else if (ratio >= 0.80) target = 1;
    else if (ratio >= 0.65) target = 2;
    else                    target = 3;

    // Hysteresis: requires 2 consecutive samples (≈1 s) before applying.
    if (target == s_pendingLevel) {
        if (++s_stableTicks >= 2) {
            int old = g_lodLevel.exchange(target, std::memory_order_release);
            if (old != target) {
                angle::log("effect_lod: fps=%.1f → LOD %d→%d (cap=%d%%)",
                           fps, old, target, (int)(lodMultiplier(target) * 100));
            }
            s_stableTicks = 0;
        }
    } else {
        s_pendingLevel = target;
        s_stableTicks  = 0;
    }
}

// ---- module lifecycle ----------------------------------------------------
void apply() {
    auto& cfg = Config::get();
    if (!cfg.effect_lod) {
        angle::log("effect_lod: disabled by config");
        return;
    }

    // Resolve real driver entry points (we forward through them).
    s_origDrawArrays   = (DrawArraysFn)  glproxy::resolve("glDrawArrays");
    s_origDrawElements = (DrawElementsFn)glproxy::resolve("glDrawElements");
    if (!s_origDrawArrays || !s_origDrawElements) {
        angle::log("effect_lod: GL entry points unavailable, disabled");
        return;
    }

    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_windowStart);

    // Resolve target FPS: explicit cfg.effect_lod_min_fps > frame_pacing_target > 60
    int target = cfg.effect_lod_min_fps;
    if (target <= 0) target = cfg.frame_pacing_target;
    if (target <= 0) target = 60;
    g_minFps = target;
    g_baseMax = cfg.particle_max > 0 ? cfg.particle_max : 150;
    g_particleHardCap = g_baseMax;
    g_enabled = true;

    angle::log("effect_lod: ENABLED (target=%d FPS, hysteresis=1s)", g_minFps);
}

} // namespace boost_effect_lod
