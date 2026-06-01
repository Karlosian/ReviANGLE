#include "angle_loader.hpp"
#include "config.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>

// EGL constants we need (copied from EGL/egl.h to avoid header dependency)
constexpr EGLint_t EGL_PLATFORM_ANGLE_ANGLE             = 0x3202;
constexpr EGLint_t EGL_PLATFORM_ANGLE_TYPE_ANGLE        = 0x3203;
constexpr EGLint_t EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE  = 0x3208;
constexpr EGLint_t EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE   = 0x3207;
constexpr EGLint_t EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE = 0x3450;
constexpr EGLint_t EGL_DEFAULT_DISPLAY                  = 0;
constexpr EGLint_t EGL_NONE                             = 0x3038;

static angle::Loaded g_state;
static std::mutex    g_logMutex;
static FILE*         g_logFile      = nullptr;
static bool          g_logOpenTried = false;
static HMODULE       g_selfModule   = nullptr;

namespace angle {

Loaded& state() { return g_state; }

void setModuleHandle(HMODULE module) {
    g_selfModule = module;
}

static std::string moduleDir() {
    HMODULE self = g_selfModule;
    if (!self) {
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&moduleDir),
                           &self);
    }

    char path[MAX_PATH] = {};
    if (!self || !GetModuleFileNameA(self, path, MAX_PATH)) {
        return "";
    }

    char* slash = std::strrchr(path, '\\');
    if (!slash) {
        return "";
    }
    slash[1] = '\0';
    return path;
}

static HMODULE loadBundledDll(const char* name, DWORD& errorOut) {
    errorOut = ERROR_SUCCESS;

    const std::string dir = moduleDir();
    std::string fullPath = dir + name;
    if (!dir.empty()) {
        SetLastError(ERROR_SUCCESS);
        HMODULE mod = LoadLibraryExA(fullPath.c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                     LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!mod) {
            mod = LoadLibraryExA(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        if (mod) {
            log("loaded %s from %s", name, fullPath.c_str());
            return mod;
        }

        errorOut = GetLastError();
        log("failed to load %s from %s, GetLastError=%lu", name, fullPath.c_str(), errorOut);
    }

    SetLastError(ERROR_SUCCESS);
    HMODULE mod = LoadLibraryA(name);
    if (!mod) {
        errorOut = GetLastError();
        log("failed to load %s from default DLL search path, GetLastError=%lu", name, errorOut);
    }
    return mod;
}

// Persistent log file: opened once, then reused for every angle::log() call.
// Eliminates the per-call fopen+fclose hitch (~5–15 ms on NTFS due to metadata
// journal + AV scan) which was a primary microfreeze source on the hot path.
// Buffer is fully buffered (8 KB); flushed on process exit via atexit handler.
static void closeLogOnExit() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile) {
        std::fflush(g_logFile);
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void log(const char* fmt, ...) {
    auto& cfg = Config::get();
    if (!cfg.debug) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile && !g_logOpenTried) {
        g_logOpenTried = true;
        g_logFile = std::fopen(cfg.log_file.c_str(), "a");
        if (g_logFile) {
            // Full buffering — CRT flushes only when buffer fills (~50+ lines).
            std::setvbuf(g_logFile, nullptr, _IOFBF, 8192);
            std::atexit(&closeLogOnExit);
        }
    }
    if (!g_logFile) return;

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_logFile);
    std::fflush(g_logFile);
    // No fclose per call. Also keep the old counter below as a cheap backstop;
    // init/error traces are now flushed immediately so GLFW/EGL failures do
    // not disappear when the process aborts early.
    static int s_flushCounter = 0;
    if (++s_flushCounter >= 8) {
        std::fflush(g_logFile);
        s_flushCounter = 0;
    }
}

static bool loadSymbols() {
    #define L(name) \
        g_state.name = (decltype(g_state.name))GetProcAddress(g_state.egl, #name); \
        if (!g_state.name) { log("missing export: " #name); return false; }

    L(eglGetDisplay);
    L(eglInitialize);
    L(eglChooseConfig);
    L(eglCreateWindowSurface);
    L(eglCreateContext);
    L(eglMakeCurrent);
    L(eglSwapBuffers);
    L(eglSwapInterval);
    L(eglDestroyContext);
    L(eglDestroySurface);
    L(eglTerminate);
    L(eglGetProcAddress);
    L(eglGetError);
    L(eglQueryString);

    // optional, may not exist on older ANGLE
    g_state.eglGetPlatformDisplayEXT = (decltype(g_state.eglGetPlatformDisplayEXT))
        GetProcAddress(g_state.egl, "eglGetPlatformDisplayEXT");
    g_state.eglGetPlatformDisplay = (decltype(g_state.eglGetPlatformDisplay))
        GetProcAddress(g_state.egl, "eglGetPlatformDisplay");

    #undef L
    return true;
}

static EGLDisplay_t openDisplayWithBackend(const std::string& backend) {
    EGLint_t platformType_check = EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
    if (backend == "d3d9") platformType_check = EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE;
    else if (backend == "vulkan") platformType_check = EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;

    if (g_state.eglGetPlatformDisplay) {
        intptr_t attribs[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, (intptr_t)platformType_check, EGL_NONE };
        EGLDisplay_t dpy = g_state.eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*)(uintptr_t)EGL_DEFAULT_DISPLAY, attribs);
        if (dpy) {
            log("eglGetPlatformDisplay succeeded for %s", backend.c_str());
            return dpy;
        }
    }

    if (!g_state.eglGetPlatformDisplayEXT) {
        log("eglGetPlatformDisplayEXT not available, using default display");
        return g_state.eglGetDisplay((void*)(uintptr_t)EGL_DEFAULT_DISPLAY);
    }

    EGLint_t platformType = EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
    if (backend == "d3d9") {
        platformType = EGL_PLATFORM_ANGLE_TYPE_D3D9_ANGLE;
    } else if (backend == "vulkan") {
        platformType = EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
    }

    EGLint_t attribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, platformType,
        EGL_NONE, EGL_NONE
    };

    return g_state.eglGetPlatformDisplayEXT(
        EGL_PLATFORM_ANGLE_ANGLE,
        (void*)(uintptr_t)EGL_DEFAULT_DISPLAY,
        attribs
    );
}

bool init() {
    if (g_state.initialized) return true;

    auto& cfg = Config::get();
    log("ReviANGLE init, requested backend: %s", cfg.backend.c_str());

    DWORD eglLoadError = ERROR_SUCCESS;
    DWORD glesLoadError = ERROR_SUCCESS;
    g_state.egl = loadBundledDll("libEGL.dll", eglLoadError);
    g_state.gles2 = loadBundledDll("libGLESv2.dll", glesLoadError);

    if (!g_state.egl || !g_state.gles2) {
        DWORD err = !g_state.egl ? eglLoadError : glesLoadError;
        if (err == ERROR_SUCCESS) {
            err = ERROR_MOD_NOT_FOUND;
        }
        SetLastError(err);
        log("failed to load ANGLE DLLs (egl=%p err=%lu gles2=%p err=%lu)",
            g_state.egl, eglLoadError, g_state.gles2, glesLoadError);
        return false;
    }

    if (!loadSymbols()) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        log("failed to load EGL symbols");
        return false;
    }

    const char* clientExts = g_state.eglQueryString(nullptr, 0x3055);
    log("EGL Client Extensions: %s", clientExts ? clientExts : "null");

    const char* backendsToTry[3] = {};
    int numBackends = 0;

    backendsToTry[numBackends++] = cfg.backend.c_str();
    if (cfg.backend == "vulkan") {
        backendsToTry[numBackends++] = "d3d11";
        backendsToTry[numBackends++] = "d3d9";
    } else if (cfg.backend == "d3d11") {
        backendsToTry[numBackends++] = "d3d9";
    }

    bool initialized = false;
    EGLint_t major = 0, minor = 0;
    std::string activeBackend = cfg.backend;

    for (int i = 0; i < numBackends; ++i) {
        const char* backendName = backendsToTry[i];
        log("trying EGL backend: %s", backendName);

        g_state.display = openDisplayWithBackend(backendName);
        if (!g_state.display) {
            log("openDisplayWithBackend failed for %s, eglGetError=0x%x", backendName, g_state.eglGetError ? g_state.eglGetError() : 0);
            continue;
        }

        if (g_state.eglInitialize(g_state.display, &major, &minor)) {
            activeBackend = backendName;
            initialized = true;
            break;
        } else {
            log("eglInitialize failed for %s: 0x%x", backendName, g_state.eglGetError());
            if (g_state.eglTerminate) {
                g_state.eglTerminate(g_state.display);
            }
            g_state.display = nullptr;
        }
    }

    if (!initialized) {
        SetLastError(ERROR_INVALID_FUNCTION);
        log("could not initialize any EGL display backend");
        return false;
    }

    cfg.backend = activeBackend;
    cfg.applyBackendSafetyOverrides();
    log("EGL initialized %d.%d using backend: %s", major, minor, activeBackend.c_str());
    if (cfg.isVulkanBackend()) {
        log("Vulkan safety mode active: validation/sync preserved, experimental effect pipeline disabled");
    }
    g_state.initialized = true;
    return true;
}

void shutdown() {
    if (!g_state.initialized) return;
    if (g_state.display && g_state.eglTerminate) {
        g_state.eglTerminate(g_state.display);
    }
    if (g_state.gles2) FreeLibrary(g_state.gles2);
    if (g_state.egl)   FreeLibrary(g_state.egl);
    g_state = {};
}

} // namespace angle
