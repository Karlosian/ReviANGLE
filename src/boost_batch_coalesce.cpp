// Boost: Batch Coalesce
// Merges adjacent draw calls that share the same GL state (texture, program,
// blend mode). Instead of issuing 10 separate glDrawArrays with 6 vertices
// each, we issue 1 call with 60 vertices.

#include <windows.h>
#include <atomic>
#include "config.hpp"
#include "gl_proxy.hpp"
#include "angle_loader.hpp"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int          GLint;
typedef int          GLsizei;

using DrawArraysFn  = void(WINAPI*)(GLenum, GLint, GLsizei);
using GetIntegervFn = void(WINAPI*)(GLenum, GLint*);

static DrawArraysFn   s_origDrawArrays = nullptr;
static GetIntegervFn  s_getIntegerv     = nullptr;
static bool g_active = false;

// Thread-local state caches — updated by notifyTex/notifyProg instead of
// calling glGetIntegerv per draw (saves 2 driver round-trips each draw call).
// FIX: Also query actual binding on each draw so deferred binds (from
// drawcall_sort's hooked glBindTexture) don't go unnoticed.
static thread_local GLuint t_curTex  = 0;
static thread_local GLuint t_curProg = 0;

// Coalesce state per thread
static thread_local GLuint  t_lastTex   = 0;
static thread_local GLuint  t_lastProg  = 0;
static thread_local GLenum  t_lastMode  = 0;
static thread_local GLint   t_coalStart = 0;
static thread_local GLsizei t_coalCount = 0;
static std::atomic<int> g_coalesced{0};

// Sync thread-local state from actual GL bindings. This handles the case
// where drawcall_sort defers glBindTexture — batch_coalesce still sees the
// correct texture because we query it directly rather than relying on
// notifyBindTex() alone.
static inline void syncTextureState() {
    GLint tex = 0;
    if (s_getIntegerv) s_getIntegerv(0x8069 /*GL_TEXTURE_BINDING_2D*/, &tex);
    t_curTex = (GLuint)tex;
}

static void doFlush() {
    if (t_coalCount > 0) {
        s_origDrawArrays(t_lastMode, t_coalStart, t_coalCount);
        t_coalCount = 0;
    }
}

static void WINAPI hooked_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!g_active) {
        s_origDrawArrays(mode, first, count);
        return;
    }

    // FIX: always sync texture state from actual GL binding — this ensures
    // batch_coalesce sees the real bound texture even when drawcall_sort has
    // deferred the bind call. Without this, deferred binds cause the coalesce
    // decision to compare against stale t_curTex, producing incorrect merges.
    syncTextureState();

    bool canMerge = (mode == t_lastMode &&
                     t_curTex  == t_lastTex &&
                     t_curProg == t_lastProg &&
                     first == t_coalStart + t_coalCount);

    if (canMerge) {
        t_coalCount += count;
        g_coalesced++;
    } else {
        doFlush();
        t_lastMode  = mode;
        t_lastTex   = t_curTex;
        t_lastProg  = t_curProg;
        t_coalStart = first;
        t_coalCount = count;
    }
}

extern "C" void boost_batch_coalesce_flush() { doFlush(); }

namespace boost_batch_coalesce {
    void apply() {
        if (!Config::get().batch_coalesce) return;

        s_origDrawArrays = (DrawArraysFn)glproxy::resolve("glDrawArrays");
        s_getIntegerv     = (GetIntegervFn)glproxy::resolve("glGetIntegerv");

        if (s_origDrawArrays) {
            g_active = true;
            angle::log("batch_coalesce: active");
        }
    }

    void flush() { doFlush(); }
    void notifyBindTex(GLuint tex)  { t_curTex  = tex; if (g_active) doFlush(); }
    void notifyUseProgram(GLuint p) { t_curProg = p;   if (g_active) doFlush(); }
    void* getDrawArraysHook() { return g_active ? (void*)hooked_glDrawArrays : nullptr; }
    int getCoalescedCount() { return g_coalesced.load(); }
}