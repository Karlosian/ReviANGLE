// Boost: GetProcAddress cache
// GD / cocos2d calls GetProcAddress thousands of times at startup.
// We cache results in a hash map so repeat lookups are O(1).
// Uses atomic operations for the common case (cache hit) to avoid
// lock contention on the hot path.

#include <windows.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include "config.hpp"
#include "common/iat_hook.hpp"
#include "angle_loader.hpp"

struct ProcKey {
    HMODULE mod;
    std::string name;
    bool operator==(const ProcKey& o) const { return mod == o.mod && name == o.name; }
};

struct ProcKeyHash {
    size_t operator()(const ProcKey& k) const {
        auto h1 = std::hash<void*>()((void*)k.mod);
        auto h2 = std::hash<std::string>()(k.name);
        return h1 ^ (h2 << 1);
    }
};

struct CacheEntry {
    FARPROC proc;
    std::atomic<bool> valid{false};
};

static std::unordered_map<ProcKey, CacheEntry, ProcKeyHash> g_cache;
static std::mutex g_mu;

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
static GetProcAddressFn s_origGetProcAddress = nullptr;

static FARPROC WINAPI hooked_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    // ordinal imports don't have a name string
    if ((uintptr_t)lpProcName <= 0xFFFF) {
        return s_origGetProcAddress(hModule, lpProcName);
    }

    ProcKey key{hModule, lpProcName};

    // Fast path: check cache without lock using find-if-you-can pattern
    // First do a quick lookup
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_cache.find(key);
        if (it != g_cache.end() && it->second.valid.load(std::memory_order_acquire)) {
            return it->second.proc;
        }
    }

    // Cache miss — call the original function
    FARPROC result = s_origGetProcAddress(hModule, lpProcName);

    // Store in cache
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto& entry = g_cache[key];
        entry.proc = result;
        entry.valid.store(true, std::memory_order_release);
    }
    return result;
}

namespace boost_loader_cache {

    void apply() {
        if (!Config::get().loader_cache) return;

        s_origGetProcAddress = (GetProcAddressFn)iat::hookInMainExe(
            "kernel32.dll", "GetProcAddress", (void*)hooked_GetProcAddress);

        if (s_origGetProcAddress) {
            angle::log("loader_cache: active");
        }
    }
}
