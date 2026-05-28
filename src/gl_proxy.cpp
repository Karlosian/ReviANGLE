#include "gl_proxy.hpp"
#include "angle_loader.hpp"
#include "config.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <psapi.h>

namespace glproxy {

void init() {
    angle::init();
}

void* resolve(const char* name) {
    auto& a = angle::state();
    // gles2 handle is stable after init — skip re-init check on hot path.
    // angle::init() is guaranteed to run before any GL call via glproxy::init()
    // called from DllMain / wglCreateContext.
    if (a.gles2) {
        if (void* p = (void*)GetProcAddress(a.gles2, name)) return p;
    }
    if (a.eglGetProcAddress) {
        return a.eglGetProcAddress(name);
    }
    // Fallback: try late init (shouldn't normally reach here)
    if (!a.initialized && angle::init() && a.gles2) {
        if (void* p = (void*)GetProcAddress(a.gles2, name)) return p;
    }
    return nullptr;
}

} // namespace glproxy

// GL types we use in the shims (matches gl.h so we don't need the full header)
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int          GLint;
typedef int          GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float        GLfloat;
typedef double       GLdouble;
typedef void         GLvoid;

// --- OpenGL 1.1 functions that GD / cocos2d might call directly via the import table ---
// Most cocos2d calls go through wglGetProcAddress after context creation, but a few
// legacy symbols may be looked up at load time. Forward them all to ANGLE.

// glClear / glClearColor / glClearDepthf / glClearStencil dedup.
// cocos2d issues identical clears each frame; dedup against last call when no
// draws have happened in between (otherwise the clear is meaningful).
static thread_local int t_frameEpoch = 0;            // bumped at frame boundary
static thread_local bool t_dirtySinceClear = true;  // any draw call sets this
static thread_local bool t_seenClearThisFrame = false;
static thread_local GLbitfield t_lastClearMask = 0;
static thread_local int t_lastClearedEpoch = -1;    // last frame in which glClear passed through
static thread_local GLfloat t_clearColorR = 1e30f, t_clearColorG = 1e30f, t_clearColorB = 1e30f, t_clearColorA = 1e30f;
static std::atomic<unsigned long long> g_megahackClearSeq{0};
static std::atomic<unsigned long long> g_megahackDrawSeq{0};
extern std::atomic<unsigned long long> g_drawArrays;
extern std::atomic<unsigned long long> g_drawElements;
static thread_local GLint t_vpX = 0x7FFFFFFF, t_vpY = 0x7FFFFFFF;
static thread_local GLsizei t_vpW = -1, t_vpH = -1;
static thread_local GLfloat t_clearDepth = 1e30f;
static thread_local GLint t_clearStencil = 0x7FFFFFFF;
static thread_local GLfloat g_depthRangeN = -1.0f, g_depthRangeF = -1.0f;
static thread_local GLfloat t_sampleCoverageVal = -1.0f;
static thread_local GLboolean t_sampleCoverageInv = 2;
static thread_local unsigned int t_capMask = 0;
static thread_local unsigned int t_capKnown = 0;  // tracks which cap states we've seen
static thread_local unsigned int t_colorMask = 0xFFFFFFFFu;
static thread_local GLenum t_cullFaceMode = 0xFFFFFFFFu;
static thread_local int t_depthMask = -1;
static thread_local GLenum t_frontFaceMode = 0xFFFFFFFFu;
static thread_local GLenum t_hintModes[256] = {};
static thread_local GLenum t_hintTgts[256] = {};
static thread_local GLfloat t_lineWidth = -1.0f;
static thread_local GLenum t_pixelStoreNames[64] = {};
static thread_local GLint t_pixelStoreParams[64] = {};
static thread_local GLfloat t_polygonOffsetFactor = 1e30f, t_polygonOffsetUnits = 1e30f;
static thread_local GLint t_scissorX = -1, t_scissorY = -1;
static thread_local GLsizei t_scissorW = -1, t_scissorH = -1;
static thread_local GLenum t_stencilFuncVal = 0xFFFFFFFFu;
static thread_local GLint t_stencilFuncRef = -1;
static thread_local GLuint t_stencilFuncMask = 0xFFFFFFFFu;
static thread_local GLuint t_stencilMask = 0xFFFFFFFFu;
static thread_local GLenum t_stencilOpFail = 0xFFFFFFFFu, t_stencilOpZFail = 0xFFFFFFFFu, t_stencilOpZPass = 0xFFFFFFFFu;
static thread_local GLenum t_blendFuncSrc = 0xFFFFFFFFu, t_blendFuncDst = 0xFFFFFFFFu;

static thread_local GLenum t_blendEq = 0xFFFFFFFFu;
static thread_local GLenum t_blendFuncSepSrcRGB = 0xFFFFFFFFu, t_blendFuncSepDstRGB = 0xFFFFFFFFu, t_blendFuncSepSrcAlpha = 0xFFFFFFFFu, t_blendFuncSepDstAlpha = 0xFFFFFFFFu;
static thread_local GLenum t_depthFunc = 0xFFFFFFFFu;
static thread_local GLenum t_activeTexture = 0xFFFFFFFFu;
static thread_local GLuint t_bound2D[32] = {};
static thread_local GLuint t_boundCube[32] = {};

extern "C" void gdangle_invalidateProxyStateCaches() {
    t_frameEpoch++;
    t_dirtySinceClear = true;
    t_seenClearThisFrame = false;
    t_lastClearMask = 0;
    t_clearColorR = 1e30f; t_clearColorG = 1e30f; t_clearColorB = 1e30f; t_clearColorA = 1e30f;
    // Reset enable-mask "known" bits too — otherwise after a foreign mod toggles
    // a cap directly via ANGLE, our dedup will incorrectly skip the next real
    // glEnable/glDisable from the game.
    t_capKnown = 0;
    t_capMask = 0;
    t_vpX = 0x7FFFFFFF; t_vpY = 0x7FFFFFFF; t_vpW = -1; t_vpH = -1;
    t_clearDepth = 1e30f;
    t_clearStencil = 0x7FFFFFFF;
    g_depthRangeN = -1.0f; g_depthRangeF = -1.0f;
    t_sampleCoverageVal = -1.0f; t_sampleCoverageInv = 2;
    t_capMask = 0;
    t_capKnown = 0;
    t_colorMask = 0xFFFFFFFFu;
    t_cullFaceMode = 0xFFFFFFFFu;
    t_depthMask = -1;
    t_frontFaceMode = 0xFFFFFFFFu;
    memset((void*)t_hintModes, 0, sizeof(t_hintModes));
    memset((void*)t_hintTgts, 0, sizeof(t_hintTgts));
    t_lineWidth = -1.0f;
    memset((void*)t_pixelStoreNames, 0, sizeof(t_pixelStoreNames));
    memset((void*)t_pixelStoreParams, 0, sizeof(t_pixelStoreParams));
    t_polygonOffsetFactor = 1e30f; t_polygonOffsetUnits = 1e30f;
    t_scissorX = -1; t_scissorY = -1; t_scissorW = -1; t_scissorH = -1;
    t_stencilFuncVal = 0xFFFFFFFFu; t_stencilFuncRef = -1; t_stencilFuncMask = 0xFFFFFFFFu;
    t_stencilMask = 0xFFFFFFFFu;
    t_stencilOpFail = 0xFFFFFFFFu; t_stencilOpZFail = 0xFFFFFFFFu; t_stencilOpZPass = 0xFFFFFFFFu;
    t_blendFuncSrc = 0xFFFFFFFFu; t_blendFuncDst = 0xFFFFFFFFu;
    t_blendEq = 0xFFFFFFFFu;
    t_blendFuncSepSrcRGB = 0xFFFFFFFFu; t_blendFuncSepDstRGB = 0xFFFFFFFFu; t_blendFuncSepSrcAlpha = 0xFFFFFFFFu; t_blendFuncSepDstAlpha = 0xFFFFFFFFu;
    t_depthFunc = 0xFFFFFFFFu;
    t_activeTexture = 0xFFFFFFFFu;
    memset((void*)t_bound2D, 0, sizeof(t_bound2D));
    memset((void*)t_boundCube, 0, sizeof(t_boundCube));
}


extern "C" void gdangle_markDirty() { t_dirtySinceClear = true; }

// Per-thread draw-disable flag: set when ANGLE crashes mid-frame so all
// subsequent draw calls in that frame are skipped. Reset at frame start
// (gdangle_resetDrawDisabled, called from wgl_wglSwapBuffers).
static thread_local bool t_drawDisabled = false;
extern "C" void gdangle_resetDrawDisabled() { t_drawDisabled = false; }

// ── MegaHack helpers (defined here so the file links after the v8 cleanup) ──
// These were previously implemented as part of the broken "underlay capture +
// restore" mechanism that froze the screen on a stale framebuffer texture.
// The capture/restore code is gone; the helpers below provide the minimum
// needed by the remaining mod-compat code paths and the gl_proxy_ext copyPixels
// stride-aware path.

// Cache MegaHack DLL address range so calledFromMegaHack() is cheap on the
// hot path (one atomic load per call once resolved).
static std::atomic<uintptr_t> s_megaHackBase{0};
static std::atomic<uintptr_t> s_megaHackEnd{0};
static std::atomic<bool>      s_megaHackResolved{false};

static void resolveMegaHackRangeIfNeeded() {
    if (s_megaHackResolved.load(std::memory_order_acquire)) return;
    // Single-attempt resolution: if MegaHack isn't loaded yet we keep base=0
    // and try again on the next call (cheap — three GetModuleHandleA calls).
    static const char* kNames[] = {
        "absolllute.hackmega.geode",
        "absolllute.hackmega.dll",
        "absolllute.hackmega",
    };
    HMODULE h = nullptr;
    for (const char* n : kNames) {
        h = GetModuleHandleA(n);
        if (h) break;
    }
    if (!h) return;
    MODULEINFO mi{};
    if (GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) {
        uintptr_t base = (uintptr_t)mi.lpBaseOfDll;
        uintptr_t end  = base + mi.SizeOfImage;
        s_megaHackBase.store(base, std::memory_order_relaxed);
        s_megaHackEnd.store(end,   std::memory_order_relaxed);
        s_megaHackResolved.store(true, std::memory_order_release);
        angle::forceLog("MegaHack range resolved: %p .. %p", (void*)base, (void*)end);
    }
}

// Returns true if the immediate caller's return address lies inside MegaHack's
// DLL image. Used by gl_glClear (strip color clears that originate inside
// MegaHack) and by copyPixels (use stride-aware path for ImGui-style uploads).
extern "C" bool calledFromMegaHack() {
    if (!Config::get().megahack_detected) return false;
    resolveMegaHackRangeIfNeeded();
    uintptr_t base = s_megaHackBase.load(std::memory_order_relaxed);
    uintptr_t end  = s_megaHackEnd.load(std::memory_order_relaxed);
    if (base == 0 || end == 0) return false;
    // Walk the stack a few frames to find a non-proxy return address. The
    // immediate _ReturnAddress() is inside this DLL (the inline call site),
    // so we capture a small stack and scan.
    void* frames[8] = {};
    USHORT n = CaptureStackBackTrace(0, 8, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t ra = (uintptr_t)frames[i];
        if (ra >= base && ra < end) return true;
    }
    return false;
}

// Queries the currently bound draw framebuffer. Used to gate MegaHack-specific
// clear handling so it only applies to the default (window) framebuffer.
extern "C" bool gdangle_isDefaultDrawFramebufferBound() {
    typedef void (WINAPI *PFN_GI)(GLenum, GLint*);
    static PFN_GI pGetIntegerv = nullptr;
    if (!pGetIntegerv) pGetIntegerv = (PFN_GI)glproxy::resolve("glGetIntegerv");
    if (!pGetIntegerv) return true; // best-effort: assume default if we can't query
    GLint drawFbo = 0;
    pGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &drawFbo);
    return drawFbo == 0;
}

// Intentionally a no-op stub. The previous implementation queried full GL
// state on every draw call and force-overrode legitimate blend factors such
// as glBlendFunc(GL_ONE, GL_ZERO) — log proof:
//   MEGAHACK_REPAIR #0: srcRGB=0x0001 dstRGB=0x0000 fixed=blend
// That "repair" was itself the source of wrong blending on MegaHack overlays.
// Call sites in glDraw{Arrays,Elements,*} are kept as hook points for future
// targeted fixes, but the function does nothing today.
extern "C" void gdangle_repairDefaultFramebufferForMegaHack() {
    // intentionally empty
}

// Intentionally a no-op stub. The original implementation blitted a once-
// captured framebuffer texture back into FBO 0 after every glClear on the
// default FBO, creating a capture/restore feedback loop that froze the
// screen on a stale image (37 captures + 80 restores in a single session
// in angle_log.txt). The capture side has already been removed from
// wgl_wglSwapBuffers; this stub keeps the call site at gl_glClear safe
// until that block is removed.
extern "C" void gdangle_restoreMegaHackUnderlayIfNeeded() {
    // intentionally empty
}

// No-op stub. Was used to clear per-frame underlay state. Kept as a hook point
// for any future MegaHack-specific frame-boundary logic.
extern "C" void gdangle_beginMegaHackFrame() {
    // intentionally empty
}

// No-op stub. Was used to mark a thread_local flag the moment MegaHack's
// glPushAttrib hook fired so the underlay capture path could bail. The
// underlay code is gone; the call site in gl_proxy_ext.cpp's glPushAttrib
// wrapper still links to this symbol.
extern "C" void gdangle_noteMegaHackOverlayDraw() {
    // intentionally empty
}

typedef void (WINAPI *PFN_CLR)(GLbitfield);
extern "C" __declspec(dllexport) void WINAPI gl_glClear(GLbitfield mask) {
    static PFN_CLR p = nullptr;
    if (!p) p = (PFN_CLR)glproxy::resolve("glClear");
    static int n = 0;
    unsigned long long mhSeq = Config::get().megahack_detected ? g_megahackClearSeq.fetch_add(1, std::memory_order_relaxed) : 0;
    if (n < 30 || (Config::get().megahack_detected && mhSeq >= 240 && mhSeq < 900)) {
        angle::forceLog("glClear #%d mhSeq=%llu: mask=0x%04X (color=%d depth=%d stencil=%d) clearColor=%.3f,%.3f,%.3f,%.3f dirty=%d epoch=%d",
            n, mhSeq, mask, (mask & 0x4000) ? 1 : 0, (mask & 0x100) ? 1 : 0, (mask & 0x400) ? 1 : 0,
            t_clearColorR, t_clearColorG, t_clearColorB, t_clearColorA, t_dirtySinceClear ? 1 : 0, t_frameEpoch);
    }
    n++;
    bool newFrame = (t_lastClearedEpoch != t_frameEpoch);
    GLbitfield finalMask = mask;
    // FIX (v7): strip color clears that originate inside MegaHack's DLL.
    // Game/cocos2d clears pass through normally (transitions, CCRenderTexture
    // etc. need them). Only MegaHack's internal FBO clears that bleed onto
    // FBO 0 should be stripped.
    if (Config::get().megahack_detected
        && (mask & 0x4000 /*GL_COLOR_BUFFER_BIT*/)
        && gdangle_isDefaultDrawFramebufferBound()
        && calledFromMegaHack()) {
        static int skipN = 0;
        finalMask = mask & ~0x4000u;
        if (skipN < 64) {
            angle::forceLog("MEGAHACK_SKIP_OWN_CLEAR #%d: mask=0x%04X -> 0x%04X (caller is MegaHack DLL)",
                skipN, mask, finalMask);
        }
        skipN++;
        if (finalMask == 0) return;
    }
    if (Config::get().gl_state_dedup && !newFrame && !t_dirtySinceClear && finalMask == t_lastClearMask) return;
    t_lastClearMask = finalMask;
    if (finalMask & 0x4000 /*GL_COLOR_BUFFER_BIT*/) t_seenClearThisFrame = true;
    t_dirtySinceClear = false;
    t_lastClearedEpoch = t_frameEpoch;
    if (p) p(finalMask);
}
typedef void (WINAPI *PFN_CC)(GLfloat, GLfloat, GLfloat, GLfloat);
extern "C" __declspec(dllexport) void WINAPI gl_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    static PFN_CC p = nullptr;
    if (!p) p = (PFN_CC)glproxy::resolve("glClearColor");
    static int n = 0;
    if (Config::get().megahack_detected && (n < 40 || g_megahackClearSeq.load(std::memory_order_relaxed) >= 240)) {
        angle::forceLog("glClearColor #%d: %.3f,%.3f,%.3f,%.3f", n, r, g, b, a);
    }
    n++;
    if (Config::get().gl_state_dedup && r == t_clearColorR && g == t_clearColorG && b == t_clearColorB && a == t_clearColorA) return;
    t_clearColorR = r; t_clearColorG = g; t_clearColorB = b; t_clearColorA = a;
    if (p) p(r, g, b, a);
}

// glViewport: log first 10 changes for diagnostics, then dedup. cocos2d sets
// the viewport on every layer/scene push, often to identical values.
typedef void (WINAPI *PFN_VP)(GLint, GLint, GLsizei, GLsizei);
extern "C" __declspec(dllexport) void WINAPI gl_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    static PFN_VP p = nullptr;
    if (!p) p = (PFN_VP)glproxy::resolve("glViewport");
    static int n = 0;
    if (n < 30) angle::forceLog("glViewport #%d: x=%d y=%d w=%d h=%d", n, x, y, w, h);
    n++;
    if (Config::get().gl_state_dedup && x == t_vpX && y == t_vpY && w == t_vpW && h == t_vpH) return;
    t_vpX = x; t_vpY = y; t_vpW = w; t_vpH = h;
    if (p) p(x, y, w, h);
}
// glClearDepthf dedup.
typedef void (WINAPI *PFN_CDF)(GLfloat);
extern "C" __declspec(dllexport) void WINAPI gl_glClearDepthf(GLfloat d) {
    static PFN_CDF p = nullptr;
    if (!p) p = (PFN_CDF)glproxy::resolve("glClearDepthf");
    if (Config::get().gl_state_dedup && d == t_clearDepth) return;
    t_clearDepth = d;
    if (p) p(d);
}
// glClearStencil dedup.
typedef void (WINAPI *PFN_CS_)(GLint);
extern "C" __declspec(dllexport) void WINAPI gl_glClearStencil(GLint s) {
    static PFN_CS_ p = nullptr;
    if (!p) p = (PFN_CS_)glproxy::resolve("glClearStencil");
    if (Config::get().gl_state_dedup && s == t_clearStencil) return;
    t_clearStencil = s;
    if (p) p(s);
}

// Legacy GL 1.1 glClearDepth takes double — ANGLE only has glClearDepthf (float).
// Convert and forward with dedup.
extern "C" __declspec(dllexport) void WINAPI gl_glClearDepth(GLdouble d) {
    GLfloat fd = (GLfloat)d;
    if (Config::get().gl_state_dedup && fd == t_clearDepth) return;
    t_clearDepth = fd;
    static PFN_CDF p2 = nullptr;
    if (!p2) p2 = (PFN_CDF)glproxy::resolve("glClearDepthf");
    if (p2) p2(fd);
}

// Legacy GL 1.1 glDepthRange takes double pair — ANGLE has glDepthRangef.
// Both share the same dedup state.
typedef void (WINAPI *PFNGLDEPTHRANGEF)(GLfloat, GLfloat);
extern "C" __declspec(dllexport) void WINAPI gl_glDepthRange(GLdouble n, GLdouble f) {
    GLfloat fn = (GLfloat)n, ff = (GLfloat)f;
    if (Config::get().gl_state_dedup && fn == g_depthRangeN && ff == g_depthRangeF) return;
    g_depthRangeN = fn; g_depthRangeF = ff;
    static PFNGLDEPTHRANGEF p = nullptr;
    if (!p) p = (PFNGLDEPTHRANGEF)glproxy::resolve("glDepthRangef");
    if (p) p(fn, ff);
}
// glDepthRangef dedup (the GLES path; GD usually calls this).
extern "C" __declspec(dllexport) void WINAPI gl_glDepthRangef(GLfloat n, GLfloat f) {
    if (Config::get().gl_state_dedup && n == g_depthRangeN && f == g_depthRangeF) return;
    g_depthRangeN = n; g_depthRangeF = f;
    static PFNGLDEPTHRANGEF p = nullptr;
    if (!p) p = (PFNGLDEPTHRANGEF)glproxy::resolve("glDepthRangef");
    if (p) p(n, f);
}
// glSampleCoverage dedup — rarely changes but GD's MSAA path can hammer it.
typedef void (WINAPI *PFN_SAMPCOV)(GLfloat, GLboolean);
extern "C" __declspec(dllexport) void WINAPI gl_glSampleCoverage(GLfloat value, GLboolean invert) {
    if (Config::get().gl_state_dedup && value == t_sampleCoverageVal && invert == t_sampleCoverageInv) return;
    t_sampleCoverageVal = value; t_sampleCoverageInv = invert;
    static PFN_SAMPCOV p = nullptr;
    if (!p) p = (PFN_SAMPCOV)glproxy::resolve("glSampleCoverage");
    if (p) p(value, invert);
}
// ===== State dedup sweep =====
// cocos2d-x re-sets identical render state every sprite. On hard levels with
// 1000+ visible objects, ~30-50% of GL calls are redundant. Dedup removes
// driver-side validation cost — measurable 10-20% CPU win on 2-core systems.

// glEnable / glDisable cap dedup. Maps known caps to a 32-bit bitmask;
// unknown caps fall through unchanged.
static int s_capBit(GLenum cap) {
    switch (cap) {
        case 0x0BE2: return 0;   // GL_BLEND
        case 0x0B71: return 1;   // GL_DEPTH_TEST
        case 0x0B90: return 2;   // GL_STENCIL_TEST
        case 0x0B44: return 3;   // GL_CULL_FACE
        case 0x0BD0: return 4;   // GL_DITHER
        case 0x0C11: return 5;   // GL_SCISSOR_TEST
        case 0x8037: return 6;   // GL_POLYGON_OFFSET_FILL
        case 0x809E: return 7;   // GL_SAMPLE_ALPHA_TO_COVERAGE
        case 0x80A0: return 8;   // GL_SAMPLE_COVERAGE
        case 0x8642: return 9;   // GL_PROGRAM_POINT_SIZE / VERTEX_PROGRAM_POINT_SIZE
        case 0x8861: return 10;  // GL_POINT_SPRITE
        case 0x8DB9: return 11;  // GL_FRAMEBUFFER_SRGB
        case 0x8C89: return 12;  // GL_RASTERIZER_DISCARD
        default:     return -1;  // unknown -> pass through
    }
}
typedef void (WINAPI *PFN_ED)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glEnable(GLenum cap) {
    static PFN_ED p = nullptr;
    if (!p) p = (PFN_ED)glproxy::resolve("glEnable");
    static int n = 0;
    if (n < 40) {
        const char* capName = "?";
        switch (cap) { case 0x0BE2: capName="BLEND"; break; case 0x0B71: capName="DEPTH_TEST"; break;
            case 0x0B90: capName="STENCIL_TEST"; break; case 0x0B44: capName="CULL_FACE"; break;
            case 0x0C11: capName="SCISSOR_TEST"; break; }
        angle::forceLog("glEnable #%d: cap=%s (0x%04X)", n, capName, cap);
    }
    n++;
    int b = s_capBit(cap);
    if (b >= 0) {
        unsigned int mask = 1u << b;
        if (t_capKnown & mask) {
            if (Config::get().gl_state_dedup && (t_capMask & mask)) return;
        } else {
            t_capKnown |= mask;
        }
        t_capMask |= mask;
    }
    if (p) p(cap);
}
extern "C" __declspec(dllexport) void WINAPI gl_glDisable(GLenum cap) {
    static PFN_ED p = nullptr;
    if (!p) p = (PFN_ED)glproxy::resolve("glDisable");
    static int n = 0;
    if (n < 40) {
        const char* capName = "?";
        switch (cap) { case 0x0BE2: capName="BLEND"; break; case 0x0B71: capName="DEPTH_TEST"; break;
            case 0x0B90: capName="STENCIL_TEST"; break; case 0x0B44: capName="CULL_FACE"; break;
            case 0x0C11: capName="SCISSOR_TEST"; break; }
        angle::forceLog("glDisable #%d: cap=%s (0x%04X)", n, capName, cap);
    }
    n++;
    int b = s_capBit(cap);
    if (b >= 0) {
        unsigned int mask = 1u << b;
        if (t_capKnown & mask) {
            if (Config::get().gl_state_dedup && !(t_capMask & mask)) return;
        } else {
            t_capKnown |= mask;
        }
        t_capMask &= ~mask;
    }
    if (p) p(cap);
}

// glColorMask — 4 booleans packed into one nibble.
typedef void (WINAPI *PFN_CM)(GLboolean, GLboolean, GLboolean, GLboolean);
extern "C" __declspec(dllexport) void WINAPI gl_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    static PFN_CM p = nullptr;
    if (!p) p = (PFN_CM)glproxy::resolve("glColorMask");
    unsigned int packed = (r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u) | (a ? 8u : 0u);
    // Log color-mask changes when megahack is active (mask=false disables color writes)
    static int cmn = 0;
    if (Config::get().megahack_detected && cmn < 30) {
        angle::forceLog("glColorMask #%d: r=%d g=%d b=%d a=%d%s",
            cmn, r, g, b, a, (packed != 0xF) ? " <-- PARTIAL/NO COLOR WRITE!" : "");
        cmn++;
    }
    if (Config::get().gl_state_dedup && packed == t_colorMask) return;
    t_colorMask = packed;
    if (p) p(r, g, b, a);
}

// glCullFace — single GLenum.
typedef void (WINAPI *PFN_CF)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glCullFace(GLenum mode) {
    static PFN_CF p = nullptr;
    if (!p) p = (PFN_CF)glproxy::resolve("glCullFace");
    if (Config::get().gl_state_dedup && mode == t_cullFaceMode) return;
    t_cullFaceMode = mode;
    if (p) p(mode);
}

// glDepthMask — single boolean.
typedef void (WINAPI *PFN_DM)(GLboolean);
extern "C" __declspec(dllexport) void WINAPI gl_glDepthMask(GLboolean flag) {
    static PFN_DM p = nullptr;
    if (!p) p = (PFN_DM)glproxy::resolve("glDepthMask");
    int v = flag ? 1 : 0;
    if (Config::get().gl_state_dedup && v == t_depthMask) return;
    t_depthMask = v;
    if (p) p(flag);
}
// Diagnostic: count draw calls. Logged from wglSwapBuffers throttle.
std::atomic<unsigned long long> g_drawArrays{0};
std::atomic<unsigned long long> g_drawElements{0};
extern "C" unsigned long long gdangle_getDrawArraysCount() { return g_drawArrays.load(std::memory_order_relaxed); }
extern "C" unsigned long long gdangle_getDrawElementsCount() { return g_drawElements.load(std::memory_order_relaxed); }

typedef void (WINAPI *PFN_DA)(GLenum, GLint, GLsizei);
// Defined in gl_proxy_ext.cpp — returns false when the currently bound
// program has GL_LINK_STATUS=0 (silicate's shaders using desktop-only
// `layout(location=N) uniform` syntax that ESSL3 rejects).
extern "C" bool gdangle_currentProgramOK();

// Opt-in deep diagnostic. Queries the live GL state from ANGLE and dumps
// everything that could plausibly suppress drawing on screen (FBO, viewport,
// scissor, color mask, blend, program, texture bindings). Called from the
// draw hooks every ~200 calls when [BoostExtreme] megahack_dump_draw_state=true.
static void dumpDrawState() {
    if (!Config::get().megahack_dump_draw_state) return;
    typedef void (WINAPI *PFN_GI)(GLenum, GLint*);
    typedef GLboolean (WINAPI *PFN_GB)(GLenum);
    static PFN_GI pGetI = (PFN_GI)glproxy::resolve("glGetIntegerv");
    static PFN_GB pIsEn = (PFN_GB)glproxy::resolve("glIsEnabled");
    if (!pGetI) return;

    GLint program=0, drawFbo=0, readFbo=0, viewport[4]={0,0,0,0}, scissor[4]={0,0,0,0};
    GLint colorMask[4]={1,1,1,1}, activeTex=0, tex0=0,tex1=0,tex2=0,tex3=0, depthMask=0;
    GLint blendSrcRGB=0, blendDstRGB=0, blendSrcA=0, blendDstA=0;
    pGetI(0x8B8D /*GL_CURRENT_PROGRAM*/, &program);
    pGetI(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &drawFbo);
    pGetI(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &readFbo);
    pGetI(0x0BA2 /*GL_VIEWPORT*/, viewport);
    pGetI(0x0C10 /*GL_SCISSOR_BOX*/, scissor);
    pGetI(0x0C23 /*GL_COLOR_WRITEMASK*/, colorMask);
    pGetI(0x84E0 /*GL_ACTIVE_TEXTURE*/, &activeTex);
    pGetI(0x8B56 /*GL_DEPTH_WRITEMASK*/, &depthMask);
    pGetI(0x80C9 /*GL_BLEND_SRC_RGB*/, &blendSrcRGB);
    pGetI(0x80C8 /*GL_BLEND_DST_RGB*/, &blendDstRGB);
    pGetI(0x80CB /*GL_BLEND_SRC_ALPHA*/, &blendSrcA);
    pGetI(0x80CA /*GL_BLEND_DST_ALPHA*/, &blendDstA);

    // Sample texture bindings on units 0..3 (need to glActiveTexture + glGetIntegerv).
    typedef void (WINAPI *PFN_AT)(GLenum);
    static PFN_AT pAct = (PFN_AT)glproxy::resolve("glActiveTexture");
    GLint origActive = activeTex;
    if (pAct) {
        pAct(0x84C0 + 0); pGetI(0x8069 /*GL_TEXTURE_BINDING_2D*/, &tex0);
        pAct(0x84C0 + 1); pGetI(0x8069, &tex1);
        pAct(0x84C0 + 2); pGetI(0x8069, &tex2);
        pAct(0x84C0 + 3); pGetI(0x8069, &tex3);
        pAct((GLenum)origActive);
    }

    GLboolean blendEn = pIsEn ? pIsEn(0x0BE2 /*GL_BLEND*/) : 0;
    GLboolean depthEn = pIsEn ? pIsEn(0x0B71 /*GL_DEPTH_TEST*/) : 0;
    GLboolean scissorEn = pIsEn ? pIsEn(0x0C11 /*GL_SCISSOR_TEST*/) : 0;
    GLboolean cullEn = pIsEn ? pIsEn(0x0B44 /*GL_CULL_FACE*/) : 0;

    angle::forceLog(
        "DRAW_STATE: prog=%d fbo=[d=%d r=%d] vp=[%d,%d,%dx%d] scissor=[en=%d %d,%d,%dx%d] "
        "cmask=[%d%d%d%d] blend=[en=%d srcRGB=0x%X dstRGB=0x%X srcA=0x%X dstA=0x%X] "
        "depth=[en=%d mask=%d] cull=%d activeTex=0x%X tex2D[0-3]=[%d,%d,%d,%d]",
        program, drawFbo, readFbo,
        viewport[0], viewport[1], viewport[2], viewport[3],
        scissorEn ? 1 : 0, scissor[0], scissor[1], scissor[2], scissor[3],
        colorMask[0], colorMask[1], colorMask[2], colorMask[3],
        blendEn ? 1 : 0, (unsigned)blendSrcRGB, (unsigned)blendDstRGB, (unsigned)blendSrcA, (unsigned)blendDstA,
        depthEn ? 1 : 0, depthMask, cullEn ? 1 : 0,
        (unsigned)activeTex,
        tex0, tex1, tex2, tex3);
}

extern "C" __declspec(dllexport) void WINAPI gl_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    static PFN_DA p = nullptr;
    if (!p) p = (PFN_DA)glproxy::resolve("glDrawArrays");
    static int n = 0;
    unsigned long long mhSeq = Config::get().megahack_detected ? g_megahackDrawSeq.fetch_add(1, std::memory_order_relaxed) : 0;
    if (n < 50 || (Config::get().megahack_detected && mhSeq >= 4000 && mhSeq < 9000 && (mhSeq % 100) == 0)) {
        if (t_drawDisabled) angle::forceLog("glDrawArrays #%d mhSeq=%llu: SKIPPED (drawDisabled) mode=%d count=%d", n, mhSeq, mode, count);
        else angle::forceLog("glDrawArrays #%d mhSeq=%llu: mode=%d first=%d count=%d", n, mhSeq, mode, first, count);
    }
    n++;
    if (t_drawDisabled) return;
    if (!Config::get().megahack_detected && !gdangle_currentProgramOK()) return;
    gdangle_repairDefaultFramebufferForMegaHack();
    g_drawArrays.fetch_add(1, std::memory_order_relaxed);
    gdangle_markDirty();
    if (p) {
        __try { p(mode, first, count); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                  GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            static int sc = 0;
            if (sc < 5) { angle::log("glDrawArrays SEH crash: 0x%08X #%d", GetExceptionCode(), sc); sc++; }
            t_drawDisabled = true; // skip all draws for rest of frame
        }
    }
    if (Config::get().megahack_dump_draw_state && mhSeq % 200 == 0) dumpDrawState();
}
typedef void (WINAPI *PFN_DE)(GLenum, GLsizei, GLenum, const GLvoid*);
extern "C" __declspec(dllexport) void WINAPI gl_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* ind) {
    static PFN_DE p = nullptr;
    if (!p) p = (PFN_DE)glproxy::resolve("glDrawElements");
    static int n = 0;
    unsigned long long mhSeq = Config::get().megahack_detected ? g_megahackDrawSeq.fetch_add(1, std::memory_order_relaxed) : 0;
    if (n < 50 || (Config::get().megahack_detected && mhSeq >= 4000 && mhSeq < 9000 && (mhSeq % 100) == 0)) {
        if (t_drawDisabled) angle::forceLog("glDrawElements #%d mhSeq=%llu: SKIPPED (drawDisabled) mode=%d count=%d", n, mhSeq, mode, count);
        else angle::forceLog("glDrawElements #%d mhSeq=%llu: mode=%d count=%d type=0x%X", n, mhSeq, mode, count, type);
    }
    n++;
    if (t_drawDisabled) return;
    if (!Config::get().megahack_detected && !gdangle_currentProgramOK()) return;
    gdangle_repairDefaultFramebufferForMegaHack();
    g_drawElements.fetch_add(1, std::memory_order_relaxed);
    gdangle_markDirty();
    if (p) {
        __try { p(mode, count, type, ind); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                  GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            static int sc = 0;
            if (sc < 5) { angle::log("glDrawElements SEH crash: 0x%08X #%d", GetExceptionCode(), sc); sc++; }
            t_drawDisabled = true;
        }
    }
    if (Config::get().megahack_dump_draw_state && mhSeq % 200 == 0) dumpDrawState();
}
// glDrawElementsBaseVertex — desktop GL 3.2+ compatibility wrapper.
// Prefer ANGLE's real base-vertex entry point when available; falling back to
// plain glDrawElements breaks indexed geometry for overlays that use non-zero
// basevertex (visible as warped / exploded UI geometry).
extern "C" __declspec(dllexport) void WINAPI gl_glDrawElementsBaseVertex(
        GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLint basevertex) {
    typedef void (WINAPI *PFN_DEBV)(GLenum, GLsizei, GLenum, const GLvoid*, GLint);
    static PFN_DEBV pBase = nullptr;
    static PFN_DE pPlain = nullptr;
    if (!pBase) pBase = (PFN_DEBV)glproxy::resolve("glDrawElementsBaseVertex");
    if (!pPlain) pPlain = (PFN_DE)glproxy::resolve("glDrawElements");
    if (t_drawDisabled) return;
    if (!Config::get().megahack_detected && !gdangle_currentProgramOK()) return;
    gdangle_repairDefaultFramebufferForMegaHack();
    g_drawElements.fetch_add(1, std::memory_order_relaxed);
    gdangle_markDirty();
    if (pBase) {
        __try { pBase(mode, count, type, indices, basevertex); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                  GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            static int sc = 0;
            if (sc < 5) { angle::log("glDrawElementsBaseVertex SEH crash: 0x%08X #%d", GetExceptionCode(), sc); sc++; }
            t_drawDisabled = true;
        }
    } else if (pPlain) {
        if (basevertex != 0) {
            static int skipped = 0;
            if (skipped < 10) {
                angle::forceLog("glDrawElementsBaseVertex fallback skipped: non-zero basevertex=%d mode=%u count=%d", basevertex, mode, count);
                skipped++;
            }
            return;
        }
        __try { pPlain(mode, count, type, indices); }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                  GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            static int sc = 0;
            if (sc < 5) { angle::log("glDrawElementsBaseVertex fallback SEH crash: 0x%08X #%d", GetExceptionCode(), sc); sc++; }
            t_drawDisabled = true;
        }
    }
}
// glPrimitiveRestartIndex / glProvokingVertex — GL 3.x desktop, no-op in GLES context.
extern "C" __declspec(dllexport) void WINAPI gl_glPrimitiveRestartIndex(GLuint /*index*/) { /* no-op */ }
extern "C" __declspec(dllexport) void WINAPI gl_glProvokingVertex(GLenum /*mode*/) { /* no-op */ }
// gl_glEnable defined above with cap-mask dedup.
// glFinish: forces full pipeline stall. When config opts in, treat as no-op.
typedef void (WINAPI *PFN_FN)(void);
extern "C" __declspec(dllexport) void WINAPI gl_glFinish(void) {
    if (Config::get().noop_finish) return;
    static PFN_FN p = nullptr;
    if (!p) p = (PFN_FN)glproxy::resolve("glFinish");
    if (p) p();
}
GLP_FORWARD_VOID(glFlush,           (void),                                           ())
// glFrontFace dedup.
typedef void (WINAPI *PFN_FF)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glFrontFace(GLenum mode) {
    static PFN_FF p = nullptr;
    if (!p) p = (PFN_FF)glproxy::resolve("glFrontFace");
    if (Config::get().gl_state_dedup && mode == t_frontFaceMode) return;
    t_frontFaceMode = mode;
    if (p) p(mode);
}
// glGetError: when noop_geterror set, short-circuit. Default is forward to driver.
typedef GLenum (WINAPI *PFN_GE)(void);
extern "C" __declspec(dllexport) GLenum WINAPI gl_glGetError(void) {
    if (Config::get().noop_geterror) return 0 /*GL_NO_ERROR*/;
    static PFN_GE p = nullptr;
    if (!p) p = (PFN_GE)glproxy::resolve("glGetError");
    return p ? p() : 0;
}
// glPolygonMode: NOT in OpenGL ES — but ImGui/Eclipse on desktop call it.
// Without this stub, Eclipse's ImGui caches NULL function pointer at backend init,
// then crashes (DEP exec at 0x0) when drawing. GLES is implicitly always GL_FILL.
extern "C" __declspec(dllexport) void WINAPI gl_glPolygonMode(GLenum /*face*/, GLenum /*mode*/) {
    // intentional no-op: GLES has no polygon mode
}
// glGetIntegerv: handle GL_NUM_EXTENSIONS=0x821D specially with our spoofed count
typedef void (WINAPI *PFNGLGETINTEGERV)(GLenum, GLint*);
extern "C" __declspec(dllexport) void WINAPI gl_glGetIntegerv(GLenum pname, GLint* data) {
    if (data && pname == 0x821D) { // GL_NUM_EXTENSIONS
        *data = 26;
        return;
    }
    static PFNGLGETINTEGERV p = nullptr;
    if (!p) p = (PFNGLGETINTEGERV)glproxy::resolve("glGetIntegerv");
    if (p) p(pname, data);
}

typedef void (WINAPI *PFNGLGETFLOATV)(GLenum, GLfloat*);
extern "C" __declspec(dllexport) void WINAPI gl_glGetFloatv(GLenum pname, GLfloat* data) {
    static PFNGLGETFLOATV p = nullptr;
    if (!p) p = (PFNGLGETFLOATV)glproxy::resolve("glGetFloatv");
    if (p) p(pname, data);
}
GLP_FORWARD_VOID(glGetBooleanv,     (GLenum pname, GLboolean* data),                  (pname, data))
// glGetString: ANGLE returns "OpenGL ES 3.0" but GD's parser expects "X.Y" desktop
// GL format. Intercept and return a faked desktop OpenGL version string.
typedef const GLubyte* (WINAPI *PFNGLGETSTRING)(GLenum);
extern "C" __declspec(dllexport) const GLubyte* WINAPI gl_glGetString(GLenum name) {
    static PFNGLGETSTRING p = nullptr;
    if (!p) p = (PFNGLGETSTRING)glproxy::resolve("glGetString");

    // GL_VENDOR=0x1F00, GL_RENDERER=0x1F01, GL_VERSION=0x1F02, GL_EXTENSIONS=0x1F03,
    // GL_SHADING_LANGUAGE_VERSION=0x8B8C
    switch (name) {
        case 0x1F02: // GL_VERSION
            // Keep "2.1.0 " prefix — cocos2d parses leading "X.Y" floats.
            return (const GLubyte*)"2.1.0 ReviANGLE";
        case 0x8B8C: // GL_SHADING_LANGUAGE_VERSION
            return (const GLubyte*)"1.20";
        case 0x1F00: // GL_VENDOR
            return (const GLubyte*)"ReviANGLE (by Reviusion)";
        case 0x1F01: // GL_RENDERER
            return (const GLubyte*)"ReviANGLE Direct3D11";
        case 0x1F03: { // GL_EXTENSIONS
            // GLEW + cocos2d-x parse GL_EXTENSIONS to detect GL_ARB_*/GL_EXT_*.
            // ANGLE returns GLES-style names which GLEW doesn't recognise -> renderer init fails
            // -> NULL deref later. Provide the desktop names cocos2d + Eclipse-Menu's GLEW
            // need; without GL_ARB_draw_elements_base_vertex GLEW leaves
            // __glewDrawElementsBaseVertex NULL -> Eclipse crashes (DEP at RIP=0).
            static const char* kExt =
                "GL_ARB_vertex_buffer_object "
                "GL_ARB_vertex_array_object "
                "GL_ARB_framebuffer_object "
                "GL_ARB_texture_non_power_of_two "
                "GL_ARB_shader_objects "
                "GL_ARB_vertex_shader "
                "GL_ARB_fragment_shader "
                "GL_ARB_shading_language_100 "
                "GL_ARB_multitexture "
                "GL_ARB_pixel_buffer_object "
                "GL_ARB_depth_texture "
                "GL_ARB_point_sprite "
                "GL_ARB_occlusion_query "
                "GL_ARB_draw_elements_base_vertex "
                "GL_ARB_map_buffer_range "
                "GL_ARB_uniform_buffer_object "
                "GL_ARB_sampler_objects "
                "GL_ARB_instanced_arrays "
                "GL_EXT_framebuffer_object "
                "GL_EXT_blend_func_separate "
                "GL_EXT_blend_equation_separate "
                "GL_EXT_blend_minmax "
                "GL_EXT_packed_depth_stencil "
                "GL_EXT_texture_format_BGRA8888 "
                "GL_EXT_bgra "
                "GL_EXT_abgr ";
            return (const GLubyte*)kExt;
        }
        default:
            return p ? p(name) : (const GLubyte*)"";
    }
}

// GL 3.0+ glGetStringi for extension enumeration (used by GLEW when GL >= 3.0)
typedef const GLubyte* (WINAPI *PFNGLGETSTRINGI)(GLenum, GLuint);
extern "C" __declspec(dllexport) const GLubyte* WINAPI gl_glGetStringi(GLenum name, GLuint index) {
    static PFNGLGETSTRINGI p = nullptr;
    if (!p) p = (PFNGLGETSTRINGI)glproxy::resolve("glGetStringi");
    // For GL_EXTENSIONS, return our spoofed list one extension at a time
    if (name == 0x1F03) {
        static const char* kExts[] = {
            "GL_ARB_vertex_buffer_object", "GL_ARB_vertex_array_object",
            "GL_ARB_framebuffer_object", "GL_ARB_texture_non_power_of_two",
            "GL_ARB_shader_objects", "GL_ARB_vertex_shader",
            "GL_ARB_fragment_shader", "GL_ARB_shading_language_100",
            "GL_ARB_multitexture", "GL_ARB_pixel_buffer_object",
            "GL_ARB_depth_texture", "GL_ARB_point_sprite",
            "GL_ARB_occlusion_query",
            "GL_ARB_draw_elements_base_vertex",
            "GL_ARB_map_buffer_range", "GL_ARB_uniform_buffer_object",
            "GL_ARB_sampler_objects", "GL_ARB_instanced_arrays",
            "GL_EXT_framebuffer_object",
            "GL_EXT_blend_func_separate", "GL_EXT_blend_equation_separate",
            "GL_EXT_blend_minmax", "GL_EXT_packed_depth_stencil",
            "GL_EXT_texture_format_BGRA8888", "GL_EXT_bgra", "GL_EXT_abgr",
        };
        const GLuint kCount = sizeof(kExts)/sizeof(kExts[0]);
        if (index < kCount) return (const GLubyte*)kExts[index];
        return (const GLubyte*)"";
    }
    return p ? p(name, index) : (const GLubyte*)"";
}

// glHint dedup — small fixed set of targets. Most calls in GD set the same.
typedef void (WINAPI *PFN_HT)(GLenum, GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glHint(GLenum target, GLenum mode) {
    static PFN_HT p = nullptr;
    if (!p) p = (PFN_HT)glproxy::resolve("glHint");
    unsigned slot = target & 0xFF;
    if (Config::get().gl_state_dedup && t_hintTgts[slot] == target && t_hintModes[slot] == mode) return;
    t_hintTgts[slot] = target; t_hintModes[slot] = mode;
    if (p) p(target, mode);
}
GLP_FORWARD(GLboolean, glIsEnabled, (GLenum cap),                                     (cap))
// glLineWidth dedup.
typedef void (WINAPI *PFN_LW)(GLfloat);
extern "C" __declspec(dllexport) void WINAPI gl_glLineWidth(GLfloat w) {
    static PFN_LW p = nullptr;
    if (!p) p = (PFN_LW)glproxy::resolve("glLineWidth");
    if (Config::get().gl_state_dedup && w == t_lineWidth) return;
    t_lineWidth = w;
    if (p) p(w);
}
// glPixelStorei dedup — cocos2d sets GL_UNPACK_ALIGNMENT to 1 or 4 every texture
// upload. Direct-mapped 64-slot table by `pname & 0x3F` (collisions just redo
// the call — still correct).
typedef void (WINAPI *PFN_PSI)(GLenum, GLint);
extern "C" __declspec(dllexport) void WINAPI gl_glPixelStorei(GLenum pname, GLint param) {
    static PFN_PSI p = nullptr;
    if (!p) p = (PFN_PSI)glproxy::resolve("glPixelStorei");
    unsigned slot = pname & 0x3F;
    if (Config::get().gl_state_dedup && t_pixelStoreNames[slot] == pname && t_pixelStoreParams[slot] == param) return;
    t_pixelStoreNames[slot] = pname; t_pixelStoreParams[slot] = param;
    if (p) p(pname, param);
}
// glPolygonOffset dedup.
typedef void (WINAPI *PFN_PO)(GLfloat, GLfloat);
extern "C" __declspec(dllexport) void WINAPI gl_glPolygonOffset(GLfloat factor, GLfloat units) {
    static PFN_PO p = nullptr;
    if (!p) p = (PFN_PO)glproxy::resolve("glPolygonOffset");
    if (Config::get().gl_state_dedup && factor == t_polygonOffsetFactor && units == t_polygonOffsetUnits) return;
    t_polygonOffsetFactor = factor; t_polygonOffsetUnits = units;
    if (p) p(factor, units);
}
typedef void (WINAPI *PFN_RP)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*);
extern "C" __declspec(dllexport) void WINAPI gl_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, GLvoid* px) {
    static PFN_RP p = nullptr;
    if (!p) p = (PFN_RP)glproxy::resolve("glReadPixels");
    static int n = 0;
    if (Config::get().megahack_detected && n < 80) {
        angle::forceLog("glReadPixels #%d: x=%d y=%d w=%d h=%d fmt=0x%04X type=0x%04X px=%p", n, x, y, w, h, fmt, type, px);
        n++;
    }
    if (p) p(x, y, w, h, fmt, type, px);
}
typedef void (WINAPI *PFN_SC)(GLint, GLint, GLsizei, GLsizei);
extern "C" __declspec(dllexport) void WINAPI gl_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    static PFN_SC p = nullptr;
    if (!p) p = (PFN_SC)glproxy::resolve("glScissor");
    static int n = 0;
    if (n < 20) angle::forceLog("glScissor #%d: x=%d y=%d w=%d h=%d", n, x, y, w, h);
    n++;
    if (Config::get().gl_state_dedup && x == t_scissorX && y == t_scissorY && w == t_scissorW && h == t_scissorH) return;
    t_scissorX = x; t_scissorY = y; t_scissorW = w; t_scissorH = h;

    if (p) p(x, y, w, h);
}
// glStencilFunc dedup.
typedef void (WINAPI *PFN_SF)(GLenum, GLint, GLuint);
extern "C" __declspec(dllexport) void WINAPI gl_glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    static PFN_SF p = nullptr;
    if (!p) p = (PFN_SF)glproxy::resolve("glStencilFunc");
    if (Config::get().gl_state_dedup && func == t_stencilFuncVal && ref == t_stencilFuncRef && mask == t_stencilFuncMask) return;
    t_stencilFuncVal = func; t_stencilFuncRef = ref; t_stencilFuncMask = mask;
    if (p) p(func, ref, mask);
}
// glStencilMask dedup.
typedef void (WINAPI *PFN_SM)(GLuint);
extern "C" __declspec(dllexport) void WINAPI gl_glStencilMask(GLuint mask) {
    static PFN_SM p = nullptr;
    if (!p) p = (PFN_SM)glproxy::resolve("glStencilMask");
    if (Config::get().gl_state_dedup && mask == t_stencilMask) return;
    t_stencilMask = mask;
    if (p) p(mask);
}
// glStencilOp dedup.
typedef void (WINAPI *PFN_SO)(GLenum, GLenum, GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glStencilOp(GLenum f, GLenum zf, GLenum zp) {
    static PFN_SO p = nullptr;
    if (!p) p = (PFN_SO)glproxy::resolve("glStencilOp");
    if (Config::get().gl_state_dedup && f == t_stencilOpFail && zf == t_stencilOpZFail && zp == t_stencilOpZPass) return;
    t_stencilOpFail = f; t_stencilOpZFail = zf; t_stencilOpZPass = zp;
    if (p) p(f, zf, zp);
}
// glViewport defined above with diagnostic logging
// Blend state dedup — cocos2d sets same blend mode across many batched sprites
typedef void (WINAPI *PFN_BF)(GLenum, GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glBlendFunc(GLenum s, GLenum d) {
    static PFN_BF p = nullptr;
    if (!p) p = (PFN_BF)glproxy::resolve("glBlendFunc");
    static int n = 0;
    if (n < 30) angle::forceLog("glBlendFunc #%d: src=0x%04X dst=0x%04X", n, s, d);
    n++;
    if (Config::get().gl_state_dedup && s == t_blendFuncSrc && d == t_blendFuncDst) return;
    t_blendFuncSrc = s; t_blendFuncDst = d;
    if (p) p(s, d);
}
typedef void (WINAPI *PFN_BE)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glBlendEquation(GLenum mode) {
    static PFN_BE p = nullptr;
    if (!p) p = (PFN_BE)glproxy::resolve("glBlendEquation");
    if (Config::get().gl_state_dedup && mode == t_blendEq) return;
    t_blendEq = mode;
    if (p) p(mode);
}
typedef void (WINAPI *PFN_BFS)(GLenum, GLenum, GLenum, GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glBlendFuncSeparate(GLenum ss, GLenum ds, GLenum sa, GLenum da) {
    static PFN_BFS p = nullptr;
    if (!p) p = (PFN_BFS)glproxy::resolve("glBlendFuncSeparate");
    static int bfn = 0;
    if (Config::get().megahack_detected && bfn < 30) {
        angle::forceLog("glBlendFuncSeparate #%d: srcRGB=0x%04X dstRGB=0x%04X srcA=0x%04X dstA=0x%04X",
            bfn, ss, ds, sa, da);
        bfn++;
    }
    if (Config::get().gl_state_dedup && ss == t_blendFuncSepSrcRGB && ds == t_blendFuncSepDstRGB && sa == t_blendFuncSepSrcAlpha && da == t_blendFuncSepDstAlpha) return;
    t_blendFuncSepSrcRGB = ss; t_blendFuncSepDstRGB = ds; t_blendFuncSepSrcAlpha = sa; t_blendFuncSepDstAlpha = da;
    if (p) p(ss, ds, sa, da);
}
// glDepthFunc dedup.
typedef void (WINAPI *PFN_DF)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glDepthFunc(GLenum func) {
    static PFN_DF p = nullptr;
    if (!p) p = (PFN_DF)glproxy::resolve("glDepthFunc");
    if (Config::get().gl_state_dedup && func == t_depthFunc) return;
    t_depthFunc = func;
    if (p) p(func);
}

// textures — with state dedup (skip redundant calls, reduces driver overhead)
typedef void (WINAPI *PFN_AT)(GLenum);
typedef void (WINAPI *PFN_BT)(GLenum, GLuint);
extern "C" __declspec(dllexport) void WINAPI gl_glActiveTexture(GLenum tex) {
    static PFN_AT p = nullptr;
    if (!p) p = (PFN_AT)glproxy::resolve("glActiveTexture");
    if (Config::get().gl_state_dedup && tex == t_activeTexture) return;
    t_activeTexture = tex;
    if (p) p(tex);
}
extern "C" __declspec(dllexport) void WINAPI gl_glBindTexture(GLenum target, GLuint tex) {
    static PFN_BT p = nullptr;
    if (!p) p = (PFN_BT)glproxy::resolve("glBindTexture");
    unsigned unit = (t_activeTexture >= 0x84C0u) ? (t_activeTexture - 0x84C0u) : 0;
    if (unit >= 32) unit = 0;
    static int n = 0;
    if (Config::get().megahack_detected && n < 80) {
        angle::forceLog("glBindTexture #%d: unit=%u target=0x%04X tex=%u", n, unit, target, tex);
        n++;
    }
    // Track per-texture-unit, per-target. Previous code always used slot [0]
    // which broke multitexturing — when unit 1+ was active, dedup compared
    // against unit 0's binding and silently dropped real bind calls.
    if (target == 0x0DE1 /*GL_TEXTURE_2D*/) {
        if (Config::get().gl_state_dedup && t_bound2D[unit] == tex) return;
        t_bound2D[unit] = tex;
    } else if (target == 0x8513 /*GL_TEXTURE_CUBE_MAP*/) {
        if (Config::get().gl_state_dedup && t_boundCube[unit] == tex) return;
        t_boundCube[unit] = tex;
    }
    if (p) p(target, tex);
}
typedef void (WINAPI *PFN_DT)(GLsizei, const GLuint*);
extern "C" __declspec(dllexport) void WINAPI gl_glDeleteTextures(GLsizei n, const GLuint* t) {
    static PFN_DT p = nullptr;
    if (!p) p = (PFN_DT)glproxy::resolve("glDeleteTextures");
    if (p) p(n, t);
    if (!t || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint dead = t[i];
        if (!dead) continue;
        for (unsigned unit = 0; unit < 32; ++unit) {
            if (t_bound2D[unit] == dead) t_bound2D[unit] = 0;
            if (t_boundCube[unit] == dead) t_boundCube[unit] = 0;
        }
    }
}
GLP_FORWARD_VOID(glGenTextures,     (GLsizei n, GLuint* t),                           (n, t))

// When Geode mods are detected, copy pixel data before passing to ANGLE.
// Mods (e.g. imageplus, happy_textures) may free the pixel buffer before
// ANGLE's deferred command buffer reads it, causing AV inside libGLESv2.dll.
typedef void (WINAPI *PFN_GI_PIX)(GLenum, GLint*);

static int pixelBytesPerPixel(GLenum format, GLenum type) {
    switch (type) {
        case 0x1401: /*GL_UNSIGNED_BYTE*/
            switch (format) {
                case 0x1908: /*GL_RGBA*/
                case 0x80E1: /*GL_BGRA_EXT*/ return 4;
                case 0x1907: /*GL_RGB*/ return 3;
                case 0x190A: /*GL_LUMINANCE_ALPHA*/ return 2;
                case 0x8227: /*GL_RG*/      return 2;
                case 0x8228: /*GL_RG_INTEGER*/ return 2;
                case 0x1903: /*GL_RED*/     return 1;
                case 0x8D94: /*GL_RED_INTEGER*/ return 1;
                case 0x1909: /*GL_LUMINANCE*/
                case 0x1906: /*GL_ALPHA*/ return 1;
                default: return 0;
            }
        case 0x1403: /*GL_UNSIGNED_SHORT*/
        case 0x8033: /*GL_UNSIGNED_SHORT_4_4_4_4*/
        case 0x8034: /*GL_UNSIGNED_SHORT_5_5_5_1*/
        case 0x8363: /*GL_UNSIGNED_SHORT_5_6_5*/
            return 2;
        case 0x1406: /*GL_FLOAT*/
            switch (format) {
                case 0x1908: /*GL_RGBA*/ return 16;
                case 0x1907: /*GL_RGB*/ return 12;
                case 0x190A: /*GL_LUMINANCE_ALPHA*/ return 8;
                case 0x1909: /*GL_LUMINANCE*/
                case 0x1906: /*GL_ALPHA*/ return 4;
                default: return 0;
            }
        default:
            {
                static int warnN = 0;
                if (warnN < 8) {
                    angle::forceLog("pixelBytesPerPixel: UNKNOWN format=0x%X type=0x%X — texture upload may produce garbage", format, type);
                    warnN++;
                }
                return 0;
            }
    }
}

struct PixelUnpackState {
    GLint alignment = 4;
    GLint rowLength = 0;
    GLint skipPixels = 0;
    GLint skipRows = 0;
    GLint unpackBuffer = 0;
};

static PixelUnpackState readPixelUnpackState() {
    PixelUnpackState state;
    static PFN_GI_PIX pGetIntegerv = nullptr;
    if (!pGetIntegerv) pGetIntegerv = (PFN_GI_PIX)glproxy::resolve("glGetIntegerv");
    if (!pGetIntegerv) return state;
    pGetIntegerv(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, &state.alignment);
    pGetIntegerv(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, &state.rowLength);
    pGetIntegerv(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, &state.skipPixels);
    pGetIntegerv(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, &state.skipRows);
    pGetIntegerv(0x88EF /*GL_PIXEL_UNPACK_BUFFER_BINDING*/, &state.unpackBuffer);
    if (state.alignment != 1 && state.alignment != 2 && state.alignment != 4 && state.alignment != 8) {
        state.alignment = 4;
    }
    if (state.rowLength < 0) state.rowLength = 0;
    if (state.skipPixels < 0) state.skipPixels = 0;
    if (state.skipRows < 0) state.skipRows = 0;
    if (state.unpackBuffer < 0) state.unpackBuffer = 0;
    return state;
}

static bool canCopyPixels(const PixelUnpackState& unpack) {
    return unpack.unpackBuffer == 0;
}

static thread_local std::vector<unsigned char> s_pixBufRing[64];
static thread_local unsigned s_pixBufRingIndex = 0;

static void gdangle_logTextureUpload(const char* op, GLenum target, GLint level, GLint x, GLint y,
                                     GLsizei w, GLsizei h, GLint ifmt, GLenum format, GLenum type,
                                     const GLvoid* originalPx, const GLvoid* finalPx,
                                     const PixelUnpackState& unpack, bool copied, bool nullFallback) {
    static int uploadLogCount = 0;
    bool unusual = nullFallback || copied || unpack.unpackBuffer != 0 || unpack.rowLength != 0 ||
                   unpack.skipPixels != 0 || unpack.skipRows != 0 || unpack.alignment != 4;
    if (uploadLogCount >= 200 && !unusual) return;
    angle::forceLog(
        "%s: target=0x%X level=%d x=%d y=%d w=%d h=%d ifmt=0x%X fmt=0x%X type=0x%X src=%p final=%p copied=%d nullFallback=%d unpackBuf=%d align=%d rowLen=%d skipPx=%d skipRows=%d ring=%u",
        op, target, level, x, y, w, h, ifmt, format, type, originalPx, finalPx, copied ? 1 : 0,
        nullFallback ? 1 : 0, unpack.unpackBuffer, unpack.alignment, unpack.rowLength,
        unpack.skipPixels, unpack.skipRows, s_pixBufRingIndex);
    if (uploadLogCount < 200) uploadLogCount++;
}

static bool copyPixelRows(unsigned char* dst, const unsigned char* src, size_t tightRowBytes, size_t sourceRowBytes, GLsizei h) {
    __try {
        for (GLsizei row = 0; row < h; ++row) {
            memcpy(dst + (size_t)row * tightRowBytes, src + (size_t)row * sourceRowBytes, tightRowBytes);
        }
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
              ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
    return true;
}

static const GLvoid* copyPixels(const GLvoid* px, GLenum format, GLenum type, GLsizei w, GLsizei h, const PixelUnpackState& unpack) {
    if (!px || w <= 0 || h <= 0) return px;
    int bpp = pixelBytesPerPixel(format, type);
    if (!bpp) {
        static int unsupportedCount = 0;
        if (unsupportedCount < 20) {
            angle::forceLog("copyPixels unsupported format/type: px=%p w=%d h=%d format=0x%X type=0x%X align=%d rowLength=%d skipPixels=%d skipRows=%d",
                px, w, h, format, type, unpack.alignment, unpack.rowLength, unpack.skipPixels, unpack.skipRows);
            unsupportedCount++;
        }
        return nullptr;
    }

    size_t tightRowBytes = (size_t)w * (size_t)bpp;
    size_t totalBytes = tightRowBytes * (size_t)h;
    std::vector<unsigned char>& buf = s_pixBufRing[s_pixBufRingIndex++ & 63u];
    buf.resize(totalBytes);

    const unsigned char* base = (const unsigned char*)px;
    unsigned char* dst = buf.data();

    // FIX (v7): stride-aware copy ONLY when caller is MegaHack. cocos2d
    // leaves GL_UNPACK_ROW_LENGTH=2048 stale, but its buffers are tightly
    // packed; reading at the stale row stride would walk past the buffer
    // and trigger an SEH access violation (caught as nullFallback -> NULL
    // upload -> garbage texture). MegaHack/ImGui's font atlas updates DO
    // mean rowLength.
    bool fromMegaHack = calledFromMegaHack();
    bool complexLayout = unpack.rowLength != 0 || unpack.skipPixels != 0 || unpack.skipRows != 0;
    if (fromMegaHack && complexLayout) {
        // Stride-aware path — correct for ImGui-style sub-region uploads.
        GLsizei effectiveRowPixels = (unpack.rowLength > 0) ? unpack.rowLength : w;
        size_t srcRowBytes = (size_t)effectiveRowPixels * (size_t)bpp;
        size_t alignMask = (size_t)unpack.alignment - 1;
        if (alignMask) srcRowBytes = (srcRowBytes + alignMask) & ~alignMask;
        const unsigned char* src = base
            + (size_t)unpack.skipRows * srcRowBytes
            + (size_t)unpack.skipPixels * (size_t)bpp;
        if (copyPixelRows(dst, src, tightRowBytes, srcRowBytes, h)) {
            static int n = 0;
            if (n < 8) {
                angle::forceLog("copyPixels MEGAHACK stride-aware: w=%d h=%d bpp=%d rowLen=%d skipPx=%d skipRows=%d srcStride=%zu",
                    w, h, bpp, unpack.rowLength, unpack.skipPixels, unpack.skipRows, srcRowBytes);
                n++;
            }
            return buf.data();
        }
        return nullptr;
    }
    if (complexLayout) {
        if (copyPixelRows(dst, base, tightRowBytes, tightRowBytes, h)) {
            static int fallbackLogCount = 0;
            if (fallbackLogCount < 20) {
                angle::forceLog("copyPixels complex: used caller pointer with tight rows for format=0x%X type=0x%X w=%d h=%d rowLength=%d skipPixels=%d skipRows=%d",
                    format, type, w, h, unpack.rowLength, unpack.skipPixels, unpack.skipRows);
                fallbackLogCount++;
            }
            return buf.data();
        }
        return nullptr;
    }

    size_t sourceRowBytes = tightRowBytes;
    size_t alignMask = (size_t)unpack.alignment - 1;
    if (alignMask) {
        sourceRowBytes = (sourceRowBytes + alignMask) & ~alignMask;
    }
    if (copyPixelRows(dst, base, tightRowBytes, sourceRowBytes, h)) {
        return buf.data();
    }
    return nullptr;
}

extern "C" __declspec(dllexport) void WINAPI gl_glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum type, const GLvoid* px) {
    using Fn = void (WINAPI *)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
    static Fn fn = (Fn)glproxy::resolve("glTexImage2D");
    if (!fn) return;
    if (Config::get().megahack_detected) {
        const GLvoid* originalPx = px;
        PixelUnpackState unpack = readPixelUnpackState();
        bool copied = false;
        bool nullFallback = false;
        if (canCopyPixels(unpack) && px) {
            const GLvoid* copiedPx = copyPixels(px, f, type, w, h, unpack);
            if (copiedPx) {
                px = copiedPx;
                copied = true;
            } else {
                px = nullptr;
                nullFallback = true;
            }
        }
        typedef void (WINAPI *PFN_GI)(GLenum, GLint*);
        typedef void (WINAPI *PFN_PS)(GLenum, GLint);
        static PFN_GI gi = (PFN_GI)glproxy::resolve("glGetIntegerv");
        static PFN_PS ps = (PFN_PS)glproxy::resolve("glPixelStorei");
        GLint oldAlign = 4;
        GLint oldRowLength = 0;
        GLint oldSkipPixels = 0;
        GLint oldSkipRows = 0;
        if (gi) {
            gi(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, &oldAlign);
            gi(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, &oldRowLength);
            gi(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, &oldSkipPixels);
            gi(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, &oldSkipRows);
        }
        if (ps && (copied || nullFallback)) {
            if (oldAlign != 1) ps(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, 1);
            if (oldRowLength != 0) ps(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, 0);
            if (oldSkipPixels != 0) ps(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, 0);
            if (oldSkipRows != 0) ps(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, 0);
        }
        gdangle_logTextureUpload("glTexImage2D", t, l, 0, 0, w, h, ifmt, f, type, originalPx, px, unpack, copied, nullFallback);
        fn(t, l, ifmt, w, h, b, f, type, px);
        if (ps && (copied || nullFallback)) {
            if (oldAlign != 1) ps(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, oldAlign);
            if (oldRowLength != 0) ps(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, oldRowLength);
            if (oldSkipPixels != 0) ps(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, oldSkipPixels);
            if (oldSkipRows != 0) ps(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, oldSkipRows);
        }
        return;
    }
    fn(t, l, ifmt, w, h, b, f, type, px);
}
extern "C" __declspec(dllexport) void WINAPI gl_glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum type, const GLvoid* px) {
    using Fn = void (WINAPI *)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
    static Fn fn = (Fn)glproxy::resolve("glTexSubImage2D");
    if (!fn) return;
    if (Config::get().megahack_detected) {
        const GLvoid* originalPx = px;
        PixelUnpackState unpack = readPixelUnpackState();
        bool copied = false;
        bool nullFallback = false;
        if (canCopyPixels(unpack) && px) {
            const GLvoid* copiedPx = copyPixels(px, f, type, w, h, unpack);
            if (copiedPx) {
                px = copiedPx;
                copied = true;
            } else {
                px = nullptr;
                nullFallback = true;
            }
        }
        typedef void (WINAPI *PFN_GI)(GLenum, GLint*);
        typedef void (WINAPI *PFN_PS)(GLenum, GLint);
        static PFN_GI gi = (PFN_GI)glproxy::resolve("glGetIntegerv");
        static PFN_PS ps = (PFN_PS)glproxy::resolve("glPixelStorei");
        GLint oldAlign = 4;
        GLint oldRowLength = 0;
        GLint oldSkipPixels = 0;
        GLint oldSkipRows = 0;
        if (gi) {
            gi(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, &oldAlign);
            gi(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, &oldRowLength);
            gi(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, &oldSkipPixels);
            gi(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, &oldSkipRows);
        }
        if (ps && (copied || nullFallback)) {
            if (oldAlign != 1) ps(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, 1);
            if (oldRowLength != 0) ps(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, 0);
            if (oldSkipPixels != 0) ps(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, 0);
            if (oldSkipRows != 0) ps(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, 0);
        }
        gdangle_logTextureUpload("glTexSubImage2D", t, l, x, y, w, h, 0, f, type, originalPx, px, unpack, copied, nullFallback);
        fn(t, l, x, y, w, h, f, type, px);
        if (ps && (copied || nullFallback)) {
            if (oldAlign != 1) ps(0x0CF5 /*GL_UNPACK_ALIGNMENT*/, oldAlign);
            if (oldRowLength != 0) ps(0x0CF2 /*GL_UNPACK_ROW_LENGTH*/, oldRowLength);
            if (oldSkipPixels != 0) ps(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, oldSkipPixels);
            if (oldSkipRows != 0) ps(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, oldSkipRows);
        }
        return;
    }
    fn(t, l, x, y, w, h, f, type, px);
}
// glTexParameteri — when mipmap_off is set, downgrade mipmap filters to non-mipmap.
// Saves GPU bandwidth on legacy architectures: each sample no longer reads from mip chain.
typedef void (WINAPI *PFN_TPI)(GLenum, GLenum, GLint);
extern "C" __declspec(dllexport) void WINAPI gl_glTexParameteri(GLenum t, GLenum p, GLint v) {
    static PFN_TPI fn = nullptr;
    if (!fn) fn = (PFN_TPI)glproxy::resolve("glTexParameteri");
    if (Config::get().mipmap_off && p == 0x2801 /*GL_TEXTURE_MIN_FILTER*/) {
        // collapse mipmap variants to plain LINEAR/NEAREST
        switch (v) {
            case 0x2700: /*NEAREST_MIPMAP_NEAREST*/
            case 0x2702: /*NEAREST_MIPMAP_LINEAR*/
                v = 0x2600; /*GL_NEAREST*/
                break;
            case 0x2701: /*LINEAR_MIPMAP_NEAREST*/
            case 0x2703: /*LINEAR_MIPMAP_LINEAR*/
                v = 0x2601; /*GL_LINEAR*/
                break;
        }
    }
    if (fn) fn(t, p, v);
}
GLP_FORWARD_VOID(glTexParameterf,   (GLenum t, GLenum p, GLfloat v),                  (t, p, v))
GLP_FORWARD_VOID(glCompressedTexImage2D, (GLenum t, GLint l, GLenum ifmt, GLsizei w, GLsizei h, GLint b, GLsizei sz, const GLvoid* d), (t, l, ifmt, w, h, b, sz, d))
// glGenerateMipmap — skip entirely when mipmap_off (save startup time + VRAM)
typedef void (WINAPI *PFN_GMM)(GLenum);
extern "C" __declspec(dllexport) void WINAPI gl_glGenerateMipmap(GLenum t) {
    static PFN_GMM fn = nullptr;
    if (!fn) fn = (PFN_GMM)glproxy::resolve("glGenerateMipmap");
    if (Config::get().mipmap_off) return;
    if (fn) fn(t);
}
