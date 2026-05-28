#include "wgl_proxy.hpp"
#include "angle_loader.hpp"
#include "config.hpp"
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <psapi.h>
#include "gl_proxy.hpp"

// ── Deep state dump for MegaHack diagnostics ──
// Queries ALL critical GL state directly from ANGLE and dumps to log.
// Called before and after frame processing to identify MegaHack's state corruption.
typedef void (WINAPI *PFN_GL_GETINTEGERV)(unsigned int, int*);
typedef void (WINAPI *PFN_GL_GETFLOATV)(unsigned int, float*);
typedef void (WINAPI *PFN_GL_GETBOOLEANV)(unsigned int, unsigned char*);
typedef unsigned char (WINAPI *PFN_GL_ISENABLED)(unsigned int);
typedef void (WINAPI *PFN_GL_ACTIVETEXTURE)(unsigned int);

static void dumpGLState(const char* tag) {
    auto& a = angle::state();
    if (!a.gles2) return;

    static PFN_GL_GETINTEGERV   pfnGI = nullptr;
    static PFN_GL_GETFLOATV     pfnGF = nullptr;
    static PFN_GL_GETBOOLEANV   pfnGB = nullptr;
    static PFN_GL_ISENABLED     pfnIE = nullptr;
    static PFN_GL_ACTIVETEXTURE pfnAT = nullptr;
    if (!pfnGI) {
        pfnGI = (PFN_GL_GETINTEGERV)GetProcAddress(a.gles2, "glGetIntegerv");
        pfnGF = (PFN_GL_GETFLOATV)GetProcAddress(a.gles2, "glGetFloatv");
        pfnGB = (PFN_GL_GETBOOLEANV)GetProcAddress(a.gles2, "glGetBooleanv");
        pfnIE = (PFN_GL_ISENABLED)GetProcAddress(a.gles2, "glIsEnabled");
        pfnAT = (PFN_GL_ACTIVETEXTURE)GetProcAddress(a.gles2, "glActiveTexture");
    }

    #define QI1(pname, dst) do { if (pfnGI) pfnGI(pname, &(dst)); } while(0)
    #define QF1(pname, dst) do { if (pfnGF) pfnGF(pname, &(dst)); } while(0)
    #define QB4(pname, dst) do { if (pfnGB) pfnGB(pname, (dst)); } while(0)
    #define EN(cap) (pfnIE ? (int)(pfnIE(cap) != 0) : 0)

    int fboDraw=0, fboRead=0;
    QI1(0x8CA6, fboDraw);   // GL_DRAW_FRAMEBUFFER_BINDING
    QI1(0x8CAA, fboRead);   // GL_READ_FRAMEBUFFER_BINDING

    int curProg=0; QI1(0x8B8D, curProg);  // GL_CURRENT_PROGRAM
    int vao=0; QI1(0x85B5, vao);          // GL_VERTEX_ARRAY_BINDING
    int ebo=0; QI1(0x8895, ebo);          // GL_ELEMENT_ARRAY_BUFFER_BINDING
    int arrayBuf=0; QI1(0x8894, arrayBuf);// GL_ARRAY_BUFFER_BINDING

    int blend=EN(0x0BE2);                 // GL_BLEND
    int blendSrcRGB=0; QI1(0x80C9, blendSrcRGB);  // GL_BLEND_SRC_RGB
    int blendDstRGB=0; QI1(0x80C8, blendDstRGB);  // GL_BLEND_DST_RGB
    int blendSrcA=0; QI1(0x80CB, blendSrcA);      // GL_BLEND_SRC_ALPHA
    int blendDstA=0; QI1(0x80CA, blendDstA);      // GL_BLEND_DST_ALPHA
    int blendEqRGB=0; QI1(0x8009, blendEqRGB);    // GL_BLEND_EQUATION_RGB
    int blendEqA=0; QI1(0x883D, blendEqA);        // GL_BLEND_EQUATION_ALPHA
    float blendColor[4]={0,0,0,0};
    if (pfnGF) pfnGF(0x8005, blendColor);         // GL_BLEND_COLOR

    unsigned char cm[4]={0,0,0,0};
    QB4(0x0C23, cm);                               // GL_COLOR_WRITEMASK
    int colorMask[4]={(int)cm[0], (int)cm[1], (int)cm[2], (int)cm[3]};

    int depthTest=EN(0x0B71);                     // GL_DEPTH_TEST
    int depthFunc=0; QI1(0x0B74, depthFunc);     // GL_DEPTH_FUNC
    int depthMask=0; if (pfnGB) { unsigned char dm=0; pfnGB(0x0B72, &dm); depthMask=(int)dm; } // GL_DEPTH_WRITEMASK
    float depthRange[2]={0,1}; if (pfnGF) pfnGF(0x0B70, depthRange);  // GL_DEPTH_RANGE

    int stencilTest=EN(0x0B90);                  // GL_STENCIL_TEST
    int cullFace=EN(0x0B44);                     // GL_CULL_FACE
    int cullMode=0; QI1(0x0B45, cullMode);       // GL_CULL_FACE_MODE

    int scissorTest=EN(0x0C11);                  // GL_SCISSOR_TEST
    int scissorBox[4]={0,0,0,0}; if (pfnGI) pfnGI(0x0C10, scissorBox); // GL_SCISSOR_BOX

    int viewport[4]={0,0,0,0}; if (pfnGI) pfnGI(0x0BA2, viewport);      // GL_VIEWPORT

    int activeTex=0; QI1(0x84E0, activeTex);     // GL_ACTIVE_TEXTURE

    int drawBuf0=0; QI1(0x8825, drawBuf0);       // GL_DRAW_BUFFER0

    int tex2D[4]={};
    for (int i=0; i<4; i++) {
        if (pfnAT) pfnAT(0x84C0 + i);
        QI1(0x8069, tex2D[i]);  // GL_TEXTURE_BINDING_2D
    }
    // Restore active texture
    if (pfnAT) pfnAT((unsigned int)activeTex);

    #undef QI1
    #undef QF1
    #undef QB4
    #undef EN

    angle::forceLog("GLSTATE[%s]:", tag);
    angle::forceLog("  FBO: draw=%d read=%d", fboDraw, fboRead);
    angle::forceLog("  PROG: cur=%d VAO=%d EBO=%d ARRAY=%d", curProg, vao, ebo, arrayBuf);
    angle::forceLog("  BLEND: en=%d eqRGB=0x%04X eqA=0x%04X srcRGB=0x%04X dstRGB=0x%04X srcA=0x%04X dstA=0x%04X bcol=%.2f,%.2f,%.2f,%.2f",
        blend, blendEqRGB, blendEqA, blendSrcRGB, blendDstRGB, blendSrcA, blendDstA,
        blendColor[0], blendColor[1], blendColor[2], blendColor[3]);
    angle::forceLog("  COLORMASK: r=%d g=%d b=%d a=%d", colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    angle::forceLog("  DEPTH: test=%d func=0x%04X mask=%d range=%.2f,%.2f", depthTest, depthFunc, depthMask, depthRange[0], depthRange[1]);
    angle::forceLog("  STENCIL: test=%d  CULL: face=%d mode=0x%04X  SCISSOR: test=%d box=%d,%d,%d,%d",
        stencilTest, cullFace, cullMode, scissorTest, scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    angle::forceLog("  VIEWPORT: %d,%d %dx%d  DRAW_BUF0: 0x%04X  ACTIVE_TEX: %d",
        viewport[0], viewport[1], viewport[2], viewport[3], drawBuf0, activeTex - 0x84C0);
    angle::forceLog("  TEX2D[0-3]: %u,%u,%u,%u", tex2D[0], tex2D[1], tex2D[2], tex2D[3]);
}

// Draw counters from gl_proxy.cpp — per-frame draw count visibility.
extern std::atomic<unsigned long long> g_drawArrays;
extern std::atomic<unsigned long long> g_drawElements;

// FPS counter — thread-safe atomic FPS and QPC
static std::atomic<int> s_realFps{0};
static std::atomic<LONGLONG> s_realFps_lastQpc{0};
static std::atomic<int> s_realFps_framesThisSec{0};

extern "C" __declspec(dllexport) int WINAPI gdangle_getRealFps() {
    return s_realFps.load();
}

// called once after first successful MakeCurrent
extern void gdangle_postGLInit();
static std::atomic<bool> g_glInitDone{false};

// forward decls at global scope (extern "C" linkage cannot be in function bodies)
extern "C" bool gdangle_shouldSkipPresent();
extern "C" void gdangle_invalidateAllStateCaches();
extern "C" void gdangle_invalidateProxyStateCaches();  // per-frame dedup cache reset
extern "C" void gdangle_resetDrawDisabled();

extern "C" unsigned long long gdangle_getDrawArraysCount();
extern "C" unsigned long long gdangle_getDrawElementsCount();
extern "C" void gdangle_dcBudgetBeginFrame();
extern "C" void gdangle_dcBudgetTick(bool skipped);
extern "C" void gdangle_invalidateProgramCache();

// Frame pacing — declared in boost_frame_pacing.cpp
namespace boost_frame_pacing { void prePresent(); bool isActive(); int getTargetFps(); }
// Stutter monitor — declared in boost_stutter_monitor.cpp
extern "C" void gdangle_stutterMonitorEndFrame();

// Draw call booster flush stubs — actual implementations in each boost module.
// Called at frame boundary (wglSwapBuffers) to ensure all buffered commands
// are sent before present. Prevents stale texture bindings from accumulating
// across frames and causing ANGLE D3D11 pipeline stalls.
extern "C" void boost_drawcall_sort_flush();
extern "C" void boost_batch_coalesce_flush();
extern "C" void boost_instancing_flush();

extern "C" void gdangle_flushDrawcallSort()    { boost_drawcall_sort_flush(); }
extern "C" void gdangle_flushBatchCoalesce()   { boost_batch_coalesce_flush(); }
extern "C" void gdangle_flushBatchForce()       { /* boost_batch_force removed in v12 */ }
extern "C" void gdangle_flushInstancing()      { boost_instancing_flush(); }

// EGL constants
constexpr EGLint_t EGL_CONTEXT_CLIENT_VERSION = 0x3098;
constexpr EGLint_t EGL_RED_SIZE               = 0x3024;
constexpr EGLint_t EGL_GREEN_SIZE             = 0x3023;
constexpr EGLint_t EGL_BLUE_SIZE              = 0x3022;
constexpr EGLint_t EGL_ALPHA_SIZE             = 0x3021;
constexpr EGLint_t EGL_DEPTH_SIZE             = 0x3025;
constexpr EGLint_t EGL_STENCIL_SIZE           = 0x3026;
constexpr EGLint_t EGL_SURFACE_TYPE           = 0x3033;
constexpr EGLint_t EGL_WINDOW_BIT             = 0x0004;
constexpr EGLint_t EGL_RENDERABLE_TYPE        = 0x3040;
constexpr EGLint_t EGL_OPENGL_ES2_BIT         = 0x0004;
constexpr EGLint_t EGL_NONE                   = 0x3038;
// Preserved-swap-chain attributes — request ANGLE to keep backbuffer
// contents across eglSwapBuffers (required for MegaHack overlay so
// previous frame stays available when game pauses its render).
constexpr EGLint_t EGL_SWAP_BEHAVIOR              = 0x3093;
constexpr EGLint_t EGL_BUFFER_PRESERVED           = 0x3094;
constexpr EGLint_t EGL_BUFFER_DESTROYED           = 0x3095;
constexpr EGLint_t EGL_SWAP_BEHAVIOR_PRESERVED_BIT = 0x0400;

struct FakeContext {
    EGLContext_t eglCtx = nullptr;
    EGLSurface_t surface = nullptr;
    HDC          hdc = nullptr;
    HWND         hwnd = nullptr;
};

static std::unordered_map<HGLRC, FakeContext*> g_contexts;
static std::mutex g_mutex;

static thread_local FakeContext* t_current = nullptr;
static thread_local HDC           t_currentDC = nullptr;

static bool isHelperWindow(HWND hwnd) {
    char cls[64] = {};
    char ttl[128] = {};
    GetClassNameA(hwnd, cls, 63);
    GetWindowTextA(hwnd, ttl, 127);
    return strcmp(cls, "GLFW30") == 0 && strcmp(ttl, "GLFW message window") == 0;
}

static EGLConfig_t pickConfig(EGLDisplay_t display) {
    auto& a = angle::state();
    EGLint_t attribs[] = {
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,     24,
        EGL_STENCIL_SIZE,    8,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,            EGL_NONE
    };
    EGLConfig_t config = nullptr;
    EGLint_t    num = 0;
    if (!a.eglChooseConfig(display, attribs, &config, 1, &num) || num < 1) {
        return nullptr;
    }
    return config;
}

extern "C" {

static HMODULE getSystemOpengl32() {
    static HMODULE hMod = nullptr;
    if (!hMod) {
        char path[MAX_PATH];
        UINT len = GetSystemDirectoryA(path, MAX_PATH);
        if (len && len < MAX_PATH - 15) {
            strcat_s(path, "\\opengl32.dll");
            hMod = LoadLibraryA(path);
        }
    }
    return hMod;
}

#define WGL_FORWARD_ENABLED_CHECK(ret, name, sig, args) \
    if (!Config::get().enabled) { \
        using Fn = ret (WINAPI *) sig; \
        static Fn fn = nullptr; \
        if (!fn) { \
            HMODULE sys = getSystemOpengl32(); \
            if (sys) fn = (Fn)GetProcAddress(sys, #name); \
        } \
        if (fn) return fn args; \
        return (ret)0; \
    }

HGLRC WINAPI wgl_wglCreateContext(HDC hdc) {
    if (!angle::init()) return nullptr;
    WGL_FORWARD_ENABLED_CHECK(HGLRC, wglCreateContext, (HDC), (hdc));
    auto& a = angle::state();

    HWND hwnd = WindowFromDC(hdc);
    if (!hwnd) {
        angle::log("wglCreateContext: no HWND for HDC %p", hdc);
        return nullptr;
    }
    angle::log("wglCreateContext: hdc=%p hwnd=%p", hdc, hwnd);

    EGLConfig_t cfg = pickConfig(a.display);
    if (!cfg) {
        angle::log("wglCreateContext: eglChooseConfig failed: 0x%x", a.eglGetError());
        return nullptr;
    }

    EGLint_t surfAttribs[] = { EGL_NONE, EGL_NONE };
    EGLSurface_t surf = a.eglCreateWindowSurface(a.display, cfg, hwnd, surfAttribs);
    if (!surf) {
        angle::log("wglCreateContext: eglCreateWindowSurface failed: 0x%x", a.eglGetError());
        return nullptr;
    }

    // Request preserved-buffer swap behaviour. With the underlay capture/restore
    // mechanism removed, this is the cheap safety net: if any frame happens to
    // leave the default framebuffer untouched (a mod redirects rendering, the
    // game pauses its render loop, etc.) the previous frame's pixels stay on
    // screen instead of presenting a black surface. Best-effort: if ANGLE rejects
    // the attribute we silently fall back to the default EGL_BUFFER_DESTROYED.
    if (Config::get().megahack_preserve_swap_chain) {
        typedef EGLBoolean_t (*PFN_EGL_SURFACE_ATTRIB)(EGLDisplay_t, EGLSurface_t, EGLint_t, EGLint_t);
        PFN_EGL_SURFACE_ATTRIB pfnSurfaceAttrib = nullptr;
        if (a.eglGetProcAddress) {
            pfnSurfaceAttrib = (PFN_EGL_SURFACE_ATTRIB)a.eglGetProcAddress("eglSurfaceAttrib");
        }
        if (!pfnSurfaceAttrib && a.egl) {
            pfnSurfaceAttrib = (PFN_EGL_SURFACE_ATTRIB)GetProcAddress(a.egl, "eglSurfaceAttrib");
        }
        if (pfnSurfaceAttrib) {
            EGLBoolean_t ok = pfnSurfaceAttrib(a.display, surf, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
            angle::forceLog("wglCreateContext: eglSurfaceAttrib(SWAP_BEHAVIOR=PRESERVED) = %d (err=0x%x)",
                            (int)ok, a.eglGetError());
        } else {
            angle::forceLog("wglCreateContext: eglSurfaceAttrib not exported by ANGLE — keeping default swap behaviour");
        }
    }

    EGLint_t ctxAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION,         3,
        EGL_NONE,                           EGL_NONE
    };
    EGLContext_t ctx = a.eglCreateContext(a.display, cfg, nullptr, ctxAttribs);
    if (ctx) {
        angle::log("wglCreateContext: OpenGL ES 3.0 context created successfully");
    }

    if (!ctx) {
        EGLint_t ctxAttribsES2[] = {
            EGL_CONTEXT_CLIENT_VERSION,         2,
            EGL_NONE,                           EGL_NONE
        };
        ctx = a.eglCreateContext(a.display, cfg, nullptr, ctxAttribsES2);
        if (ctx) {
            angle::log("wglCreateContext: OpenGL ES 3.0 failed, fell back to OpenGL ES 2.0");
        }
    }
    if (!ctx) {
        angle::log("wglCreateContext: eglCreateContext failed: 0x%x", a.eglGetError());
        a.eglDestroySurface(a.display, surf);
        return nullptr;
    }

    auto* fc = new FakeContext{ctx, surf, hdc, hwnd};

    std::lock_guard<std::mutex> lock(g_mutex);
    HGLRC fake = (HGLRC)(uintptr_t)(g_contexts.size() + 1);
    // ensure uniqueness
    while (g_contexts.count(fake)) {
        fake = (HGLRC)((uintptr_t)fake + 1);
    }
    g_contexts[fake] = fc;

    angle::log("wglCreateContext -> %p (egl=%p surf=%p)", fake, ctx, surf);
    return fake;
}

BOOL WINAPI wgl_wglDeleteContext(HGLRC hglrc) {
    WGL_FORWARD_ENABLED_CHECK(BOOL, wglDeleteContext, (HGLRC), (hglrc));
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_contexts.find(hglrc);
    if (it == g_contexts.end()) return FALSE;

    auto& a = angle::state();
    auto* fc = it->second;
    if (fc == t_current) {
        a.eglMakeCurrent(a.display, nullptr, nullptr, nullptr);
        t_current = nullptr;
        t_currentDC = nullptr;
        gdangle_invalidateAllStateCaches();
    }
    if (fc->eglCtx)  a.eglDestroyContext(a.display, fc->eglCtx);
    if (fc->surface) a.eglDestroySurface(a.display, fc->surface);
    delete fc;
    g_contexts.erase(it);
    return TRUE;
}

BOOL WINAPI wgl_wglMakeCurrent(HDC hdc, HGLRC hglrc) {
    WGL_FORWARD_ENABLED_CHECK(BOOL, wglMakeCurrent, (HDC, HGLRC), (hdc, hglrc));
    auto& a = angle::state();

    if (!hglrc) {
        angle::forceLog("wglMakeCurrent: hdc=%p hglrc=NULL (unbind)", hdc);
        a.eglMakeCurrent(a.display, nullptr, nullptr, nullptr);
        t_current = nullptr;
        t_currentDC = nullptr;
        gdangle_invalidateAllStateCaches();
        return TRUE;
    }

    static bool s_checkedEarlyMods = false;
    if (!s_checkedEarlyMods) {
        s_checkedEarlyMods = true;
        const char* megaHackNames[] = {
            "absolllute.hackmega.geode", "absolllute.hackmega.dll",
            "absolllute.hackmega",
        };
        for (int ni = 0; ni < 3; ni++) {
            HMODULE m = GetModuleHandleA(megaHackNames[ni]);
            angle::forceLog("EARLY_MOD_DETECT: GetModuleHandle(\"%s\") = %p", megaHackNames[ni], m);
            if (m && !Config::get().megahack_detected) {
                Config::get().mod_loader_detected = true;
                Config::get().applyModCompat();
                angle::forceLog("EARLY RUNTIME MEGAHACK DETECTED: %s handle=%p. Compat mode ON before postGLInit.", megaHackNames[ni], m);
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_contexts.find(hglrc);
    if (it == g_contexts.end()) return FALSE;

    auto* fc = it->second;

    FakeContext* prev = t_current;
    BOOL ok = a.eglMakeCurrent(a.display, fc->surface, fc->surface, fc->eglCtx) ? TRUE : FALSE;
    if (ok) {
        static int n = 0;
        if (n < 15) {
            angle::forceLog("wglMakeCurrent #%d: hdc=%p hglrc=%p eglCtx=%p surf=%p hwnd=%p (context %s)",
                n, hdc, hglrc, fc->eglCtx, fc->surface, fc->hwnd,
                (prev != fc) ? "SWITCHED" : "same");
        }
        n++;
        if (prev != fc) {
            gdangle_invalidateAllStateCaches();
        }
        t_current = fc;
        t_currentDC = hdc;
        bool expected = false;
        if (!g_glInitDone.load()) {
            char cls[64] = {};
            GetClassNameA(fc->hwnd, cls, 63);
            char ttl[128] = {};
            GetWindowTextA(fc->hwnd, ttl, 127);
            bool helperWindow = isHelperWindow(fc->hwnd);
            bool isRealWindow = IsWindowVisible(fc->hwnd) || (ttl[0] != '\0' && !helperWindow);
            if (isRealWindow && g_glInitDone.compare_exchange_strong(expected, true)) {
                angle::forceLog("gdangle_postGLInit: using hwnd=%p cls='%s' title='%s'", fc->hwnd, cls, ttl);
                gdangle_postGLInit();
            } else if (!isRealWindow) {
                angle::forceLog("gdangle_postGLInit: deferred for helper window hwnd=%p cls='%s' title='%s'", fc->hwnd, cls, ttl);
            }
        }
    } else {
        angle::forceLog("wglMakeCurrent FAILED: eglMakeCurrent error=0x%x", a.eglGetError());
    }
    return ok;
}

HGLRC WINAPI wgl_wglGetCurrentContext() {
    WGL_FORWARD_ENABLED_CHECK(HGLRC, wglGetCurrentContext, (), ());
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& kv : g_contexts) {
        if (kv.second == t_current) return kv.first;
    }
    return nullptr;
}

HDC WINAPI wgl_wglGetCurrentDC() {
    WGL_FORWARD_ENABLED_CHECK(HDC, wglGetCurrentDC, (), ());
    return t_currentDC;
}

// Forward declarations of our WGL extension implementations
extern "C" BOOL WINAPI wgl_wglSwapIntervalEXT(int interval);
static int WINAPI wgl_wglGetSwapIntervalEXT() { return 0; }
static const char* WINAPI wgl_wglGetExtensionsStringEXT() {
    return "WGL_EXT_swap_control WGL_ARB_extensions_string WGL_EXT_extensions_string";
}
static const char* WINAPI wgl_wglGetExtensionsStringARB(HDC) {
    return "WGL_EXT_swap_control WGL_ARB_extensions_string WGL_EXT_extensions_string";
}

// Last-resort no-op fallback so GLEW pointer stays non-NULL for unknown gl/glu names.
// Eclipse's GLEW resolves dozens of desktop GL 3.x/4.x funcs that ANGLE GLES2 lacks;
// returning NULL leaves __glewXxx pointer null, which crashes (DEP at RIP=0) on call.
// Returns 0 in RAX so handle/bool/int returns are safely "null/false".
extern "C" __declspec(dllexport) intptr_t WINAPI gdangle_glNoOp() { return 0; }

PROC WINAPI wgl_wglGetProcAddress(LPCSTR name) {
    if (!name) return nullptr;
    if (!angle::init()) return nullptr;
    WGL_FORWARD_ENABLED_CHECK(PROC, wglGetProcAddress, (LPCSTR), (name));
    auto& a = angle::state();

    // PREFER our exported wrappers (so our spoofs/stubs fire).
    // This must come BEFORE ANGLE init check — Eclipse may call wglGetProcAddress
    // before our gdangle_postGLInit() completes; we should still resolve our stubs.
    if (name[0] == 'g' && name[1] == 'l') {
        static HMODULE selfMod = nullptr;
        if (!selfMod) {
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (LPCSTR)&wgl_wglGetProcAddress, &selfMod);
        }
        if (selfMod) {
            PROC p = GetProcAddress(selfMod, name);
            if (p) {
                static int sn = 0;
                if (sn < 240) { angle::log("wglGPA SELF %s -> %p", name, p); sn++; }
                return p;
            }
        }
    }

    if (!a.initialized && !angle::init()) {
        angle::log("wglGetProcAddress: ANGLE not initialized for '%s'", name);
        return nullptr;
    }

    // WGL extensions cocos2d / GLEW look up via wglGetProcAddress.
    // ANGLE's libGLESv2 doesn't export these — provide our own wrappers.
    if (name[0] == 'w' && name[1] == 'g' && name[2] == 'l') {
        if (!strcmp(name, "wglSwapIntervalEXT"))         return (PROC)wgl_wglSwapIntervalEXT;
        if (!strcmp(name, "wglGetSwapIntervalEXT"))      return (PROC)wgl_wglGetSwapIntervalEXT;
        if (!strcmp(name, "wglGetExtensionsStringEXT"))  return (PROC)wgl_wglGetExtensionsStringEXT;
        if (!strcmp(name, "wglGetExtensionsStringARB"))  return (PROC)wgl_wglGetExtensionsStringARB;
    }

    // try GLES2 DLL directly first (fast path)
    if (a.gles2) {
        PROC p = (PROC)GetProcAddress(a.gles2, name);
        if (p) {
            static int gn = 0;
            if (gn < 240) { angle::log("wglGPA GLES2 %s -> %p", name, p); gn++; }
            return p;
        }
    }
    // fall back to eglGetProcAddress (for GL extensions)
    if (a.eglGetProcAddress) {
        PROC p = (PROC)a.eglGetProcAddress(name);
        if (p) {
            static int en = 0;
            if (en < 240) { angle::log("wglGPA EGL  %s -> %p", name, p); en++; }
            return p;
        }
    }

    // Alias common ARB/EXT-suffixed names to core ANGLE entry points
    // (GLEW/cocos2d may request glGenBuffersARB etc.)
    static const struct { const char* suffix; size_t len; } kSuffixes[] = {
        {"ARB", 3}, {"EXT", 3}, {"OES", 3}, {"ANGLE", 5}, {"NV", 2},
    };
    size_t nameLen = strlen(name);
    for (auto& s : kSuffixes) {
        if (nameLen > s.len && !strcmp(name + nameLen - s.len, s.suffix)) {
            char base[128];
            size_t baseLen = nameLen - s.len;
            if (baseLen >= sizeof(base)) break;
            memcpy(base, name, baseLen);
            base[baseLen] = '\0';
            if (a.gles2) {
                PROC p = (PROC)GetProcAddress(a.gles2, base);
                if (p) return p;
            }
            if (a.eglGetProcAddress) {
                PROC p = (PROC)a.eglGetProcAddress(base);
                if (p) return p;
            }
        }
    }

    // CRITICAL: Eclipse + GLEW request many desktop-only GL 3.x/4.x funcs that
    // ANGLE doesn't have (glPolygonMode, glDrawElementsBaseVertex, glPatchParameteri,
    // glProgramBinary, ...). Returning NULL -> __glewXxx pointer = NULL -> DEP crash.
    // Returning a no-op is harmless: GLEW only calls them if the mod actually invokes
    // the function, and we'd rather skip a render feature than crash the game.
    if (name[0] == 'g' && name[1] == 'l') {
        static int n = 0;
        if (n < 240) { angle::log("wglGPA STUB %s -> no-op", name); n++; }
        return (PROC)gdangle_glNoOp;
    }
    return nullptr;
}

BOOL WINAPI wgl_wglShareLists(HGLRC h1, HGLRC h2) {
    WGL_FORWARD_ENABLED_CHECK(BOOL, wglShareLists, (HGLRC, HGLRC), (h1, h2));
    angle::forceLog("wglShareLists unsupported: src=%p dst=%p", h1, h2);
    return FALSE;
}

// SEH-protected eglSwapBuffers wrapper. Lives in its own translation unit / function
// so MSVC doesn't hoist locals with destructors across the __try boundary.
// The function pointer is resolved once and stored in a static, so no destructor-
// requiring objects exist at the __except level. If ANGLE hits EXCEPTION_ILLEGAL_INSTRUCTION
// (AVX2 on old CPU) or EXCEPTION_ACCESS_VIOLATION (D3D11 device lost), we catch it,
// log it once, and return FALSE so the game can keep running.
static BOOL safeEglSwap(EGLDisplay_t display, EGLSurface_t surface) {
    auto fn = angle::state().eglSwapBuffers;
    if (!fn) return FALSE;
    __try {
        return fn(display, surface) ? TRUE : FALSE;
    } __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ||
                GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        static int sc = 0;
        if (sc < 4) {
            angle::log("eglSwapBuffers SEH crash: 0x%08X #%d",
                       GetExceptionCode(), sc);
            sc++;
        }
        return FALSE;
    }
}

BOOL WINAPI wgl_wglSwapBuffers(HDC hdc) {
    WGL_FORWARD_ENABLED_CHECK(BOOL, wglSwapBuffers, (HDC), (hdc));
    auto& a = angle::state();
    FakeContext* fc = t_current;
    if (!fc) {
        // find by HDC
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& kv : g_contexts) {
            if (kv.second->hdc == hdc) { fc = kv.second; break; }
        }
    }
    if (!fc) {
        static int logCount = 0;
        if (logCount++ < 3) angle::log("wglSwapBuffers: NO CONTEXT for hdc=%p", hdc);
        return FALSE;
    }

    // Runtime mod detection: runs once at first SwapBuffers.
    {
        static bool s_checked = false;
        if (!s_checked) {
            s_checked = true;
            angle::forceLog("MOD_DETECT: starting module enumeration...");
            HMODULE hMods[1024]; DWORD cbNeeded;
            HANDLE hProc = GetCurrentProcess();
            if (EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded)) {
                DWORD count = cbNeeded / sizeof(HMODULE);
                angle::forceLog("MOD_DETECT: enumerated %u modules", count);
                for (DWORD i = 0; i < count && i < 1024; i++) {
                    CHAR name[MAX_PATH] = {};
                    if (GetModuleBaseNameA(hProc, hMods[i], name, sizeof(name))) {
                        // Log ALL modules containing common mod keywords
                        if (strstr(name, "hack") || strstr(name, "mega") ||
                            strstr(name, "Mega") || strstr(name, "absol") ||
                            strstr(name, "Hack") || strstr(name, "geode") ||
                            strstr(name, "Geode") || strstr(name, "mod") ||
                            strstr(name, "Mod") || strstr(name, "silicate") ||
                            strstr(name, "Silicate") || strstr(name, "peony") ||
                            strstr(name, "Peony")) {
                            angle::forceLog("MOD_DETECT: [%u] %s (handle=%p)", i, name, hMods[i]);
                        }
                    }
                }
            } else {
                angle::forceLog("MOD_DETECT: EnumProcessModules FAILED err=%u", GetLastError());
            }
            // MegaHack-specific runtime check; Geode alone is informational.
            const char* megaHackNames[] = {
                "absolllute.hackmega.geode", "absolllute.hackmega.dll",
                "absolllute.hackmega",
            };
            const char* informationalNames[] = { "Geode.dll", "Geode.geode" };
            for (int ni = 0; ni < 3; ni++) {
                HMODULE m = GetModuleHandleA(megaHackNames[ni]);
                angle::forceLog("MOD_DETECT: GetModuleHandle(\"%s\") = %p", megaHackNames[ni], m);
                if (m && !Config::get().megahack_detected) {
                    Config::get().mod_loader_detected = true;
                    Config::get().applyModCompat();
                    angle::forceLog("RUNTIME MEGAHACK DETECTED: %s handle=%p. Compat mode ON.", megaHackNames[ni], m);
                }
            }
            for (int ni = 0; ni < 2; ni++) {
                HMODULE m = GetModuleHandleA(informationalNames[ni]);
                angle::forceLog("MOD_DETECT: GetModuleHandle(\"%s\") = %p", informationalNames[ni], m);
                if (m) {
                    Config::get().mod_loader_detected = true;
                }
            }
        }
    }

    // Deep state dump: snapshot ALL GL state at frame START (before any restore).
    // Shows what MegaHack left behind from the previous frame.
    static int dumpN = 0;
    if (Config::get().megahack_detected && (dumpN < 8 || (dumpN % 30) == 0)) {
        char tag[32]; snprintf(tag, sizeof(tag), "PRE_FRAME_%d", dumpN);
        dumpGLState(tag);
    }
    if (Config::get().megahack_detected) dumpN++;

    // Reset draw-disable flag — new frame starts clean.
    gdangle_resetDrawDisabled();
    // Reset all proxy state dedup caches — foreign mods (silicate, Eclipse, MegaHack) change
    // GL state directly via libGLESv2.dll without going through our proxy, leaving
    // our caches stale. A full invalidation at frame boundary is the safest fix.
    gdangle_invalidateProxyStateCaches();
    // Sync program cache with ANGLE — catches foreign deletions (silicate).
    gdangle_invalidateProgramCache();
    // Mark start of new frame for draw call budget timer.
    gdangle_dcBudgetBeginFrame();

    // CRITICAL: flush ALL draw-call booster buffers before present.
    // boost_drawcall_sort buffers GL_TRIANGLES commands and only flushes when
    // buffer reaches 256 OR on non-TRIANGLES draws. If a frame has <256
    // TRIANGLES draws, the buffer is never flushed — g_currentTexId goes stale,
    // and flushSorted() binds wrong textures -> ANGLE D3D11 pipeline stall ->
    // 1-2 second freeze. Same for batch_coalesce.
    gdangle_flushDrawcallSort();
    gdangle_flushBatchCoalesce();
    gdangle_flushBatchForce();
    gdangle_flushInstancing();

    // Frame pacing — gate this present until target_dt has elapsed.
    // Combined with allow_tearing this delivers stable target FPS
    // (e.g. 165) without DXGI vsync waits or jitter from driver scheduling.
    boost_frame_pacing::prePresent();

    // Re-invalidate caches after present so foreign mods see clean state.
    gdangle_invalidateProxyStateCaches();

    // Safety: mods (MegaHack hitbox viewer, etc.) sometimes bind offscreen
    // FBOs during the frame and forget to restore the default framebuffer.
    // ANGLE's eglSwapBuffers presents the EGLSurface, but the GL state we
    // hand to ANGLE on present should still leave FBO 0 bound so the next
    // frame starts in a clean state.
    //
    // The old code skipped this in MegaHack mode because of a (mistaken)
    // belief that it broke MegaHack's overlay compositing. In practice
    // MegaHack always finishes drawing its overlay (to FBO 0 or to its own
    // FBO + blit) BEFORE the game calls SwapBuffers, so force-binding FBO 0
    // here is safe and removes a class of "black/frozen frame" bugs.
    if (!Config::get().megahack_detected || Config::get().megahack_force_fbo0_on_swap) {
        static auto pfnBindFramebuffer = (void(WINAPI*)(unsigned int, unsigned int))GetProcAddress(a.gles2, "glBindFramebuffer");
        if (pfnBindFramebuffer) {
            constexpr unsigned int GL_FRAMEBUFFER = 0x8D40;
            pfnBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

// FPS counter — inject into the present gate so the counter reflects real
// present rate, not the scheduler's virtual tick count.
// Thread-safe: atomic FPS and last-QPC shared between present thread
// and any polling thread (e.g. mod's on-screen counter).

    // Present-skip: when present_skip_idle is enabled and no draws happened
    // since last frame, skip the actual eglSwapBuffers call.

// SEH guard: if ANGLE crashes inside eglSwapBuffers (D3D11 device lost,
    // driver reset, etc.) — caught inside safeEglSwap() helper, which lives
    // in its own scope so it can use __try alongside lock_guard / vector.

    bool skipped = gdangle_shouldSkipPresent();
    BOOL ok;
    if (skipped) {
        ok = TRUE;
    } else {
        ok = safeEglSwap(a.display, fc->surface);
    }
    static int swapN = 0;
    int curSwapN = swapN;
    if (swapN < 30 || (Config::get().megahack_detected && (swapN % 30) == 0)) {
        angle::forceLog("wglSwapBuffers #%d: hdc=%p skipped=%d ok=%d draws=%d drawElem=%d",
            swapN, hdc, skipped ? 1 : 0, ok ? 1 : 0,
            (int)g_drawArrays.load(), (int)g_drawElements.load());
    }
    // Reset draw counters each frame for per-frame draw count visibility
    g_drawArrays.store(0); g_drawElements.store(0);
    swapN++;

    // Deep state dump: snapshot AFTER restore+present.
    // Compare with PRE_FRAME to see what our restore changed.
    if (Config::get().megahack_detected && (curSwapN < 8 || (curSwapN % 30) == 0)) {
        char tag[32]; snprintf(tag, sizeof(tag), "POST_FRAME_%d", curSwapN);
        dumpGLState(tag);
    }

    // Stutter monitor: track frame time after present
    gdangle_stutterMonitorEndFrame();


    // FPS / profile sampler: per-second snapshot to fps_log.csv.
    // Microfreeze-safe: persistent FILE* (no per-second fopen/fclose hitch),
    // and per-frame max/p99 tracking so any spike is visible in the log.
    static LARGE_INTEGER s_freq           = {};
    static LARGE_INTEGER s_lastWrite      = {};
    static LARGE_INTEGER s_lastFrameQpc   = {};
    static int   s_swapsThisSec  = 0;
    static unsigned long long s_lastDA = 0, s_lastDE = 0;
    static FILE* s_fpsLog        = nullptr;
    static bool  s_init          = false;
    // Frame time histogram for this 1-second window (max 240 entries; capped
    // to avoid unbounded growth at extreme FPS). Stored as ticks for cheapness.
    static constexpr int kMaxSamples = 256;
    static LONGLONG s_dtTicks[kMaxSamples] = {};
    static int      s_dtCount = 0;
    static LONGLONG s_dtMaxTicks = 0;

    if (!s_init) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_lastWrite);
        QueryPerformanceCounter(&s_lastFrameQpc);
        s_fpsLog = std::fopen("fps_log.csv", "a");
        if (s_fpsLog) {
            std::setvbuf(s_fpsLog, nullptr, _IOFBF, 4096);
            // CSV header on first launch (only if file was empty).
            // (Cheap heuristic: just always emit; analysis tools can dedupe.)
            std::fprintf(s_fpsLog, "qpc,fps,frames,da_per_frame,de_per_frame,max_dt_ms,p99_dt_ms\n");
        }
        s_init = true;
    }
    if (!skipped) s_swapsThisSec++;  // count only real presents, not idle skips
    gdangle_dcBudgetTick(skipped);   // update adaptive draw call throttle

    LARGE_INTEGER now; QueryPerformanceCounter(&now);

    // Per-frame dt sample for spike tracking — only for real presents.
    if (!skipped) {
        LONGLONG dtTicks = now.QuadPart - s_lastFrameQpc.QuadPart;
        s_lastFrameQpc = now;
        if (dtTicks > s_dtMaxTicks) s_dtMaxTicks = dtTicks;
        if (s_dtCount < kMaxSamples) s_dtTicks[s_dtCount++] = dtTicks;
    }

    LONGLONG delta = now.QuadPart - s_lastWrite.QuadPart;
    if (delta >= s_freq.QuadPart) {
        double seconds = (double)delta / (double)s_freq.QuadPart;
        double fps = (double)s_swapsThisSec / seconds;
        unsigned long long da = gdangle_getDrawArraysCount();
        unsigned long long de = gdangle_getDrawElementsCount();
        unsigned long long daPerFrame = s_swapsThisSec ? (da - s_lastDA) / s_swapsThisSec : 0;
        unsigned long long dePerFrame = s_swapsThisSec ? (de - s_lastDE) / s_swapsThisSec : 0;
        s_lastDA = da; s_lastDE = de;

        // Compute p99 frame time for the window using nth_element — O(n)
        // instead of O(n²) insertion sort. Runs once per second, off hot path.
        double maxDtMs = (double)s_dtMaxTicks * 1000.0 / (double)s_freq.QuadPart;
        double p99DtMs = 0.0;
        if (s_dtCount > 0) {
            int p99idx = (s_dtCount * 99) / 100;
            if (p99idx >= s_dtCount) p99idx = s_dtCount - 1;
            std::nth_element(s_dtTicks, s_dtTicks + p99idx, s_dtTicks + s_dtCount);
            p99DtMs = (double)s_dtTicks[p99idx] * 1000.0 / (double)s_freq.QuadPart;
        }

        if (s_fpsLog) {
            std::fprintf(s_fpsLog, "%lld,%.2f,%d,%llu,%llu,%.2f,%.2f\n",
                         (long long)now.QuadPart, fps, s_swapsThisSec,
                         daPerFrame, dePerFrame, maxDtMs, p99DtMs);
            // fflush every 5 s — pushes the CRT buffer to the OS write cache.
            // This is a pure WriteFile (no metadata journal, no AV scan), so it
            // costs <0.1 ms — orders of magnitude cheaper than the original
            // fopen/fclose-per-second pattern that caused the microfreeze.
            // Ensures data survives a hard kill (e.g. Task Manager / Kill()).
            static int s_flushCounter = 0;
            if (++s_flushCounter >= 5) {
                std::fflush(s_fpsLog);
                s_flushCounter = 0;
            }
        }
        s_realFps.store((int)(fps + 0.5));
        s_swapsThisSec = 0;
        s_lastWrite    = now;
        s_dtCount      = 0;
        s_dtMaxTicks   = 0;
    }

    return ok;
}

extern "C" BOOL WINAPI wgl_wglSwapIntervalEXT(int interval) {
    if (!Config::get().enabled) {
        HMODULE sys = getSystemOpengl32();
        if (!sys) return FALSE;
        using GPAFn = PROC (WINAPI *)(LPCSTR);
        static GPAFn sysGPA = nullptr;
        if (!sysGPA) sysGPA = (GPAFn)GetProcAddress(sys, "wglGetProcAddress");
        using Fn = BOOL (WINAPI *)(int);
        static Fn fn = nullptr;
        if (sysGPA && !fn) fn = (Fn)sysGPA("wglSwapIntervalEXT");
        return fn ? fn(interval) : FALSE;
    }
    auto& a = angle::state();
    if (!a.eglSwapInterval) return FALSE;
    return a.eglSwapInterval(a.display, interval) ? TRUE : FALSE;
}

// pixel format — we don't really negotiate, we just accept whatever GD asks
int WINAPI wgl_wglChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR* ppfd) {
    WGL_FORWARD_ENABLED_CHECK(int, wglChoosePixelFormat, (HDC, const PIXELFORMATDESCRIPTOR*), (hdc, ppfd));
    return 1;
}

BOOL WINAPI wgl_wglSetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR* ppfd) {
    WGL_FORWARD_ENABLED_CHECK(BOOL, wglSetPixelFormat, (HDC, int, const PIXELFORMATDESCRIPTOR*), (hdc, format, ppfd));
    return TRUE;
}

int WINAPI wgl_wglDescribePixelFormat(HDC hdc, int format, UINT size, LPPIXELFORMATDESCRIPTOR ppfd) {
    WGL_FORWARD_ENABLED_CHECK(int, wglDescribePixelFormat, (HDC, int, UINT, LPPIXELFORMATDESCRIPTOR), (hdc, format, size, ppfd));
    if (ppfd && size >= sizeof(PIXELFORMATDESCRIPTOR)) {
        ZeroMemory(ppfd, sizeof(*ppfd));
        ppfd->nSize        = sizeof(PIXELFORMATDESCRIPTOR);
        ppfd->nVersion     = 1;
        ppfd->dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        ppfd->iPixelType   = PFD_TYPE_RGBA;
        ppfd->cColorBits   = 32;
        ppfd->cDepthBits   = 24;
        ppfd->cStencilBits = 8;
    }
    return 1;
}

int WINAPI wgl_wglGetPixelFormat(HDC hdc) {
    WGL_FORWARD_ENABLED_CHECK(int, wglGetPixelFormat, (HDC), (hdc));
    return 1;
}

extern "C" bool gdangle_hasCurrentContext() {
    return t_current != nullptr;
}

} // extern "C"
