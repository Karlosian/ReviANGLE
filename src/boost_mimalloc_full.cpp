// Boost: full mimalloc allocator
// Microsoft's mimalloc is 2-5x faster than the default CRT heap for small
// allocations (<256 bytes), which dominate in cocos2d-x (CCObject, CCNode, etc.)
//
// Two modes:
// 1. If mimalloc.dll is present in GD folder → LoadLibrary + redirect
// 2. Otherwise, fall back to the slab allocator in boost_allocator.cpp
//
// To use mode 1: download mimalloc-override.dll (x86) from
// https://github.com/microsoft/mimalloc/releases and rename to mimalloc.dll.

#include <windows.h>
#include <atomic>
#include "config.hpp"
#include "common/iat_hook.hpp"
#include "angle_loader.hpp"

using MallocFn   = void*(*)(size_t);
using FreeFn     = void(*)(void*);
using ReallocFn  = void*(*)(void*, size_t);
using CallocFn   = void*(*)(size_t, size_t);

static std::atomic<MallocFn>  s_mi_malloc{nullptr};
static std::atomic<FreeFn>   s_mi_free{nullptr};
static std::atomic<ReallocFn> s_mi_realloc{nullptr};
static std::atomic<CallocFn> s_mi_calloc{nullptr};

static std::atomic<MallocFn>  s_origMalloc{nullptr};
static std::atomic<FreeFn>    s_origFree{nullptr};
static std::atomic<ReallocFn> s_origRealloc{nullptr};
static std::atomic<CallocFn>  s_origCalloc{nullptr};

// FIX: Use stdatomic to prevent race conditions on active flag
static std::atomic<bool> s_active{false};

static void* hooked_malloc(size_t n) {
    MallocFn fn = s_mi_malloc.load(std::memory_order_acquire);
    if (fn) return fn(n);
    // Fallback to original if mimalloc not loaded
    fn = s_origMalloc.load(std::memory_order_acquire);
    if (fn) return fn(n);
    return nullptr;
}

static void hooked_free(void* p) {
    FreeFn fn = s_mi_free.load(std::memory_order_acquire);
    if (fn) { fn(p); return; }
    fn = s_origFree.load(std::memory_order_acquire);
    if (fn) fn(p);
}

static void* hooked_realloc(void* p, size_t n) {
    ReallocFn fn = s_mi_realloc.load(std::memory_order_acquire);
    if (fn) return fn(p, n);
    fn = s_origRealloc.load(std::memory_order_acquire);
    if (fn) return fn(p, n);
    return nullptr;
}

static void* hooked_calloc(size_t nmemb, size_t size) {
    CallocFn fn = s_mi_calloc.load(std::memory_order_acquire);
    if (fn) return fn(nmemb, size);
    fn = s_origCalloc.load(std::memory_order_acquire);
    if (fn) return fn(nmemb, size);
    return nullptr;
}

namespace boost_mimalloc_full {

    void apply() {
        if (!Config::get().mimalloc_full) return;

        HMODULE mi = LoadLibraryA("mimalloc.dll");
        if (!mi) {
            mi = LoadLibraryA("mimalloc-override.dll");
        }
        if (!mi) {
            angle::log("mimalloc: DLL not found, using default allocator");
            return;
        }

        // FIX: Get function pointers first
        MallocFn mi_malloc = (MallocFn)GetProcAddress(mi, "mi_malloc");
        FreeFn mi_free = (FreeFn)GetProcAddress(mi, "mi_free");
        ReallocFn mi_realloc = (ReallocFn)GetProcAddress(mi, "mi_realloc");
        CallocFn mi_calloc = (CallocFn)GetProcAddress(mi, "mi_calloc");

        if (!mi_malloc || !mi_free || !mi_realloc) {
            angle::log("mimalloc: critical exports not found in DLL");
            FreeLibrary(mi);
            return;
        }

        // FIX: Store pointers AFTER getting them
        s_mi_malloc.store(mi_malloc, std::memory_order_release);
        s_mi_free.store(mi_free, std::memory_order_release);
        s_mi_realloc.store(mi_realloc, std::memory_order_release);
        if (mi_calloc) s_mi_calloc.store(mi_calloc, std::memory_order_release);

        // hook CRT malloc/free in GD.exe
        const char* crtDlls[] = {"msvcrt.dll", "ucrtbase.dll", "api-ms-win-crt-heap-l1-1-0.dll"};
        for (auto* dll : crtDlls) {
            auto* origM = iat::hookInMainExe(dll, "malloc", (void*)hooked_malloc);
            auto* origF = iat::hookInMainExe(dll, "free", (void*)hooked_free);
            auto* origR = iat::hookInMainExe(dll, "realloc", (void*)hooked_realloc);
            auto* origC = iat::hookInMainExe(dll, "calloc", (void*)hooked_calloc);
            if (origM && !s_origMalloc.load(std::memory_order_acquire)) s_origMalloc.store((MallocFn)origM);
            if (origF && !s_origFree.load(std::memory_order_acquire)) s_origFree.store((FreeFn)origF);
            if (origR && !s_origRealloc.load(std::memory_order_acquire)) s_origRealloc.store((ReallocFn)origR);
            if (origC && !s_origCalloc.load(std::memory_order_acquire)) s_origCalloc.store((CallocFn)origC);
        }

        s_active.store(true, std::memory_order_release);
        angle::log("mimalloc: active (mi_malloc=%p)", mi_malloc);
    }

    bool isActive() {
        return s_active.load(std::memory_order_acquire);
    }

    void shutdown() {
        s_active.store(false, std::memory_order_release);
        // Clear function pointers
        s_mi_malloc.store(nullptr);
        s_mi_free.store(nullptr);
        s_mi_realloc.store(nullptr);
        s_mi_calloc.store(nullptr);
    }
}
