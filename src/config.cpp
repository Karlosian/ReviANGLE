#include "config.hpp"

#include <windows.h>
#include <psapi.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

Config& Config::get() {
    static Config c;
    return c;
}

// Geode / mod compatibility: multi-layer detection.
// Any installed Geode mod can hook OpenGL functions via minhook or vtable
// patching, bypassing our proxy. When that happens our state caches and
// tex_compress become stale and cause black frames or AV crashes.
//
// We use four detection layers, each progressively heavier:
//   1. Disk scan: portable -> standard Geode install paths (fast, runs at DLL load)
//   2. Process-space module scan (slower, but catches late-loaded mods)
//   3. Specific MegaHack file check (targeted, most reliable)
//   4. Geode loader DLL presence in process (definitive: if Geode is loaded,
//      mods are active)
//
// All detection results are cached after first successful hit.

// Forward declaration for angle::forceLog (from angle_loader.hpp).
// We cannot include that header here (circular dep with config.hpp),
// but the function exists and is exported from angle_loader.cpp.
namespace angle { void forceLog(const char* fmt, ...); }

// Cache: once we know mods are present, never re-scan.
static bool s_modsKnownPresent = false;
static bool s_megaHackKnownPresent = false;

static bool startsWithIgnoreCase(const std::string& value, const char* prefix) {
    size_t prefixLen = std::strlen(prefix);
    if (value.size() < prefixLen) return false;
    for (size_t i = 0; i < prefixLen; ++i) {
        if (std::tolower((unsigned char)value[i]) != std::tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

static bool hasGeodeModsInDir(const char* baseDir) {
    std::string modsDir = std::string(baseDir) + "\\geode\\mods";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((modsDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool has = false;
    do {
        std::string name(fd.cFileName);
        if (name == "." || name == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            angle::forceLog("mod-detect: found mod directory '%s' in %s", fd.cFileName, modsDir.c_str());
            has = true; break;
        }
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = name.substr(dot);
            if (ext == ".geode" || ext == ".GEODE") {
                angle::forceLog("mod-detect: found .geode file '%s' in %s", fd.cFileName, modsDir.c_str());
                has = true; break;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return has;
}

static bool hasMegaHackInDir(const char* baseDir) {
    std::string modsDir = std::string(baseDir) + "\\geode\\mods";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((modsDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool has = false;
    do {
        std::string name(fd.cFileName);
        if (name == "." || name == "..") continue;
        if (startsWithIgnoreCase(name, "absolllute.hackmega")) {
            angle::forceLog("mod-detect: found MegaHack package '%s' in %s", fd.cFileName, modsDir.c_str());
            has = true;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return has;
}

static bool hasMegaHackOnDisk() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath))) {
        std::string path(exePath);
        auto pos = path.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string exeDir = path.substr(0, pos);
            if (hasMegaHackInDir(exeDir.c_str())) return true;
        }
    }
    char localAppData[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, sizeof(localAppData));
    if (len > 0 && len < sizeof(localAppData)) {
        std::string gdLocal(localAppData);
        gdLocal += "\\GeometryDash";
        if (hasMegaHackInDir(gdLocal.c_str())) return true;
    }
    return false;
}

// Process-space scan: checks if Geode mod loader is active.
// Geode.dll is the universal GD mod loader — ALL mods use it.
static bool hasModLoaderLoadedInProcess() {
    if (GetModuleHandleA("Geode.dll")) {
        angle::forceLog("mod-detect: Geode.dll loaded — mod loader active");
        return true;
    }
    return false;
}

static bool hasMegaHackLoadedInProcess() {
    static const char* kMegaHackModules[] = {
        "absolllute.hackmega.dll",
        "absolllute.hackmega.geode",
        "absolllute.hackmega"
    };
    for (const char* name : kMegaHackModules) {
        if (GetModuleHandleA(name)) {
            angle::forceLog("mod-detect: %s loaded — MegaHack active", name);
            return true;
        }
    }
    return false;
}

static bool hasGeodeMods() {
    if (s_modsKnownPresent) return true;

    bool hasMods = false;

    // Layer 1: On-disk scan — any content in geode/mods/
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath))) {
        std::string path(exePath);
        auto pos = path.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string exeDir = path.substr(0, pos);
            if (hasGeodeModsInDir(exeDir.c_str())) hasMods = true;
        }
    }
    char localAppData[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, sizeof(localAppData));
    if (!hasMods && len > 0 && len < sizeof(localAppData)) {
        std::string gdLocal(localAppData);
        gdLocal += "\\GeometryDash";
        if (hasGeodeModsInDir(gdLocal.c_str())) hasMods = true;
    }

    if (!hasMods && hasModLoaderLoadedInProcess()) hasMods = true;

    if (hasMods) {
        s_modsKnownPresent = true;
        return true;
    }

    angle::forceLog("mod-detect: NO mods found — running in full-performance mode");
    return false;
}

static bool hasMegaHack() {
    if (s_megaHackKnownPresent) return true;
    if (hasMegaHackLoadedInProcess() || hasMegaHackOnDisk()) {
        s_megaHackKnownPresent = true;
        return true;
    }
    return false;
}

void Config::applyModCompat() {
    auto& cfg = *this;
    cfg.megahack_detected = true;

    angle::forceLog("MegaHack compatibility mode active");

    // ── DXGI/D3D11 vtable hooks — guaranteed conflict with mod overlays
    cfg.allow_tearing  = false;
    cfg.low_latency    = false;
    cfg.gpu_thread_prio = false;

    // ── GL state dedup cache — mods call GL directly, bypassing proxy;
    //     cached state goes stale -> wrong bindings -> black frame
    cfg.gl_state_dedup = false;

    // ── Frame pacing locks inside SwapBuffers — conflicts with mod
    //     SwapBuffers hook chains (overlay injection)
    cfg.frame_pacing = false;

    // ── glGetError stub — hides bugs from mod GL calls
    cfg.noop_geterror = false;

    // ── noop_finish — mods may rely on glFinish for timing/sync
    cfg.noop_finish = false;

    // ── tex_compress: mod hooks on glTexImage2D may retain pixel pointers
    //     after our DXT1 upload, crashing ANGLE with EXCEPTION_ACCESS_VIOLATION
    cfg.tex_compress = false;

    // ── Render-pipeline mutations: draw-call reordering, batching, culling,
    //     vertex/index remapping, and blend-mode optimization all break when a
    //     mod (overlay, hitbox viewer, etc.) injects its own geometry into the
    //     frame. blend_optimize is especially bad — a full-screen ImGui dimming
    //     quad with disabled alpha blend renders as solid black.
    cfg.blend_optimize = false;
    cfg.vertex_dedup = false;
    cfg.index_buffer_gen = false;
    cfg.label_cache = false;
    cfg.particle_throttle = false;
    cfg.object_pool = false;
    cfg.drawcall_sort = false;
    cfg.batch_coalesce = false;
    cfg.instancing = false;
    cfg.dyn_resolution = false;
    cfg.effect_lod = false;
    cfg.drawcall_budget = false;
    cfg.fbo_cache = false;
    cfg.triple_buffer = false;
    cfg.disable_aa = false;
    cfg.scissor_tight = false;
    cfg.pipe_drawsort = false;
    cfg.vertex_compress = false;
    cfg.vbo_pool = false;

    // ── Additional features that conflict with mod overlays ──
    // shader_cache: mods may modify/replace shaders at runtime;
    //               cached binaries become invalid -> black triangles
    cfg.shader_cache = false;
    // shader_warmup: validates and JIT-compiles programs at link time;
    //                mod shaders use desktop GLSL that ES rejects -> link failure
    cfg.shader_warmup = false;
    // mipmap_off: mod icons/UI textures rely on mipmap sampling;
    //             forcing nearest/linear causes aliasing artifacts
    cfg.mipmap_off = false;
    // present_skip_idle: when MegaHack's overlay opens, GD stops drawing
    //                    (paused state); if we skip Presents, overlay vanishes
    cfg.present_skip_idle = false;
    // skip_effects: some mods add custom effects (trails, glow) that our
    //               shake/flash suppression incorrectly classifies as game effects
    cfg.skip_shake_flash = false;
    // depth_off: mod 3D rendering (hitbox viewer, GD3D) needs depth testing
    cfg.depth_off = false;
    // d3d11_multithread: SetMultithreadProtected conflicts with mods that
    //                    create their own D3D11 device contexts
    // silent_debug: if a mod patches OutputDebugString, our debug suppression
    //               hook chain could deadlock
    cfg.silent_debug = false;
    // sse_memcpy: some mods (Eclipse) replace memcpy/memset globally;
    //             our IAT hook fights theirs -> heap corruption
    cfg.sse_memcpy = false;
    // string_intern: mods allocate GL resources through string paths we don't
    //                track; intern lookups return stale handles -> crash
    cfg.string_intern = false;

    // ── Asset and texture prewarming can race texture lifetime under modded paths ──
    cfg.async_asset_loader = false;
    cfg.level_predecode = false;
    cfg.iocp_loader = false;
    cfg.texcache_preload = false;

    // ── Timing / visual hacks that still disturb overlay compositing or icon layers ──
    cfg.force_no_vsync = false;
    cfg.safe_latency = false;

    // ── All IAT hooks on the MAIN EXE's import table ──
    // Each of these boost modules calls iat::hookInMainExe(...) to redirect a
    // standard Windows or CRT function (CreateFileA, HeapAlloc, memcpy, Sleep,
    // recv/send, ...). Geode/MegaHack ALSO patches the main exe's IAT to
    // install its own hooks.
    cfg.plist_binary = false;
    cfg.fast_io = false;
    cfg.loader_cache = false;        // also patches CreateFileA
    cfg.http_pool = false;           // patches WinHTTP/WinINET in main exe
    cfg.mimalloc_full = false;       // patches HeapAlloc/HeapFree/HeapReAlloc
    cfg.online_block_gameplay = false; // patches WS2_32 recv/send
    cfg.ramdisk_cache = false;       // patches CreateFileA / ReadFile
    cfg.skip_intro = false;          // patches main-exe code via IAT
    cfg.precise_sleep = false;       // patches kernel32 Sleep/SleepEx
    cfg.stack_trim = false;          // patches Thread CRT entry
    cfg.winsock_opt = false;         // patches WSASocket/setsockopt

    // ── Log what's disabled for diagnostics ──
    angle::forceLog("  DXGI/D3D11: allow_tearing=off low_latency=off safe_latency=off gpu_thread_prio=off");
    angle::forceLog("  GL state:   gl_state_dedup=off noop_geterror=off noop_finish=off tex_compress=off");
    angle::forceLog("  Render:     blend_opt=off vertex_dedup=off index_gen=off");
    angle::forceLog("  Pipeline:   drawcall_sort=off batch_coalesce=off");
    angle::forceLog("  GPU:        shader_cache=off shader_warmup=off mipmap_off=off");
    angle::forceLog("  Assets:     async_loader=off level_predecode=off loader_cache=off iocp_loader=off texcache_preload=off");
    angle::forceLog("  Memory:     object_pool=off label_cache=off sse_memcpy=off string_intern=off");
    angle::forceLog("  IAT hooks:  plist_binary=off fast_io=off http_pool=off mimalloc_full=off ramdisk_cache=off");
    angle::forceLog("              online_block=off skip_intro=off precise_sleep=off stack_trim=off winsock_opt=off");
    angle::forceLog("  Misc:       frame_pacing=off skip_effects=off present_skip=off depth_off=off silent_debug=off");
}

static std::string trim(std::string s) {
    auto notSpace = [](int c) { return !std::isspace(static_cast<unsigned char>(c)); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static bool parseBool(const std::string& v) {
    std::string lower;
    lower.reserve(v.size());
    for (char c : v) lower.push_back((char)std::tolower((unsigned char)c));
    return (lower == "true" || lower == "1" || lower == "yes" || lower == "on");
}

static int parseInt(const std::string& v, int minVal, int maxVal, int defaultVal) {
    if (v.empty()) return defaultVal;
    char* end = nullptr;
    long val = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') {
        return defaultVal;  // invalid input
    }
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return (int)val;
}

static uint32_t parseHex(const std::string& v) {
    if (v.empty()) return 0;
    try {
        if (v.size() > 2 && (v[0] == '0') && (v[1] == 'x' || v[1] == 'X'))
            return (uint32_t)std::stoul(v.substr(2), nullptr, 16);
        return (uint32_t)std::stoul(v, nullptr, 0);
    } catch (...) {
        return 0;
    }
}

static float parseFloat(const std::string& v, float minVal, float maxVal, float defaultVal) {
    if (v.empty()) return defaultVal;
    char* end = nullptr;
    float val = std::strtof(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') {
        return defaultVal;  // invalid input
    }
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

void Config::load(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return;  // keep defaults

    std::string line;
    std::string section;

    megahack_detected = false;
    mod_loader_detected = false;

    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "ANGLE") {
            if      (key == "enabled")             enabled             = parseBool(val);
            else if (key == "backend")             backend             = val;
            else if (key == "debug")               debug               = parseBool(val);
            else if (key == "log_file")            log_file            = val;
            else if (key == "frame_pacing")        frame_pacing        = parseBool(val);
            else if (key == "frame_pacing_target") frame_pacing_target = parseInt(val, 0, 1000, 60);
        } else if (section == "Boost") {
            if      (key == "gpu_forcer")     gpu_forcer     = parseBool(val);
            else if (key == "fast_allocator") fast_allocator = parseBool(val);
            else if (key == "timer_fix")      timer_fix      = parseBool(val);
            else if (key == "thread_boost")   thread_boost   = parseBool(val);
            else if (key == "cpu_affinity")   cpu_affinity   = parseHex(val);
            else if (key == "sse_math")       sse_math       = parseBool(val);
            else if (key == "power_boost")    power_boost    = parseBool(val);
        } else if (section == "BoostAdvanced") {
            if      (key == "tex_compress")         tex_compress         = parseBool(val);
            else if (key == "nvapi_profile")        nvapi_profile        = parseBool(val);
            else if (key == "shader_cache")         shader_cache         = parseBool(val);
            else if (key == "shader_cache_dir")     shader_cache_dir     = val;
            else if (key == "large_address_aware")  large_address_aware  = parseBool(val);
            else if (key == "gl_state_dedup")       gl_state_dedup       = parseBool(val);
            else if (key == "working_set_prefetch") working_set_prefetch = parseBool(val);
            else if (key == "fmod_tuning")          fmod_tuning          = parseBool(val);
            else if (key == "fmod_sample_rate")     fmod_sample_rate     = parseInt(val, 8000, 96000, 44100);
            else if (key == "async_asset_loader")   async_asset_loader   = parseBool(val);
            else if (key == "async_loader_threads") async_loader_threads = parseInt(val, 0, 16, 2);
            else if (key == "precise_sleep")        precise_sleep        = parseBool(val);
            else if (key == "heap_compact_interval")heap_compact_interval= parseInt(val, 0, 600, 0);
            else if (key == "mod_compat_forced")    mod_compat_forced    = parseBool(val);
            else if (key == "apply_mod_compat_on_any_mod") apply_mod_compat_on_any_mod = parseBool(val);
            else if (key == "megahack_force_fbo0_on_swap") megahack_force_fbo0_on_swap = parseBool(val);
            else if (key == "megahack_preserve_swap_chain") megahack_preserve_swap_chain = parseBool(val);
            else if (key == "megahack_dump_draw_state") megahack_dump_draw_state = parseBool(val);
            else if (key == "d3d11_unlock")      d3d11_unlock      = parseBool(val);
            else if (key == "process_perf_lock") process_perf_lock = parseBool(val);
            else if (key == "pcore_pin")         pcore_pin         = parseBool(val);
            else if (key == "gpu_residency")     gpu_residency     = parseBool(val);
        } else if (section == "BoostRender") {
            if      (key == "depth_off")         depth_off         = parseBool(val);
            else if (key == "mipmap_off")        mipmap_off        = parseBool(val);
            else if (key == "noop_finish")       noop_finish       = parseBool(val);
            else if (key == "noop_geterror")     noop_geterror     = parseBool(val);
            else if (key == "vbo_pool")          vbo_pool          = parseBool(val);
            else if (key == "vbo_pool_size_mb")  vbo_pool_size_mb  = parseInt(val, 4, 256, 16);
            else if (key == "vertex_compress")   vertex_compress   = parseBool(val);
            else if (key == "instancing")        instancing        = parseBool(val);
            else if (key == "dyn_resolution")    dyn_resolution    = parseBool(val);
            else if (key == "dyn_res_target_fps")dyn_res_target_fps= parseInt(val, 30, 240, 60);
        } else if (section == "BoostIO") {
            if      (key == "fast_io")        fast_io       = parseBool(val);
            else if (key == "ramdisk_cache")  ramdisk_cache = parseBool(val);
            else if (key == "ramdisk_path")   ramdisk_path  = val;
            else if (key == "loader_cache")   loader_cache  = parseBool(val);
            // v2.0.0
            else if (key == "iocp_loader")             iocp_loader             = parseBool(val);
            else if (key == "iocp_loader_concurrency") iocp_loader_concurrency = parseInt(val, 0, 8, 0);
        } else if (section == "BoostCPU") {
            if      (key == "sse_memcpy")    sse_memcpy    = parseBool(val);
            else if (key == "string_intern") string_intern = parseBool(val);
            else if (key == "mimalloc_full") mimalloc_full = parseBool(val);
            else if (key == "silent_debug")  silent_debug  = parseBool(val);
        } else if (section == "BoostSystem") {
            if      (key == "wddm_priority")  wddm_priority  = parseBool(val);
            else if (key == "game_mode")      game_mode      = parseBool(val);
            else if (key == "smart_cpu_pin")  smart_cpu_pin  = parseBool(val);
            else if (key == "mitigation_off") mitigation_off = parseBool(val);
        } else if (section == "BoostLatency") {
            if      (key == "allow_tearing")        allow_tearing        = parseBool(val);
            else if (key == "mmcss_pro_audio")      mmcss_pro_audio      = parseBool(val);
            else if (key == "shader_warmup")        shader_warmup        = parseBool(val);
            else if (key == "low_latency")          low_latency          = parseBool(val);
            else if (key == "safe_latency")         safe_latency         = parseBool(val);
            else if (key == "anti_stutter")         anti_stutter         = parseBool(val);
        } else if (section == "BoostGD") {
            if      (key == "skip_intro")       skip_intro       = parseBool(val);
            else if (key == "object_pool")      object_pool      = parseBool(val);
            else if (key == "object_pool_size") object_pool_size = parseInt(val, 256, 65535, 4096);
            else if (key == "plist_binary")     plist_binary     = parseBool(val);
            else if (key == "plist_cache_dir")  plist_cache_dir  = val;
            else if (key == "skip_shake_flash") skip_shake_flash = parseBool(val);
            else if (key == "level_predecode")  level_predecode  = parseBool(val);
            else if (key == "predecode_threads")predecode_threads= parseInt(val, 0, 8, 0);
        } else if (section == "BoostRenderAdv") {
            if      (key == "atlas_size")      atlas_size      = parseInt(val, 512, 8192, 2048);
            else if (key == "fbo_cache")       fbo_cache       = parseBool(val);
            else if (key == "fbo_pool_size")   fbo_pool_size   = parseInt(val, 2, 64, 8);
            else if (key == "triple_buffer")   triple_buffer   = parseBool(val);
            else if (key == "disable_aa")      disable_aa      = parseBool(val);
            else if (key == "blend_optimize")  blend_optimize  = parseBool(val);
        } else if (section == "BoostCocos") {
            if      (key == "particle_throttle") particle_throttle = parseBool(val);
            else if (key == "particle_max")      particle_max      = parseInt(val, 0, 5000, 300);
            else if (key == "texcache_preload")  texcache_preload  = parseBool(val);
            else if (key == "label_cache")       label_cache       = parseBool(val);
            else if (key == "scheduler_skip")    scheduler_skip    = parseBool(val);
            else if (key == "drawcall_sort")     drawcall_sort     = parseBool(val);
            else if (key == "index_buffer_gen")  index_buffer_gen  = parseBool(val);
        } else if (section == "BoostSysAdv") {
            if      (key == "ftz_daz")       ftz_daz       = parseBool(val);
            else if (key == "spectre_off")   spectre_off   = parseBool(val);
            else if (key == "io_priority")   io_priority   = parseBool(val);
            else if (key == "mem_priority")  mem_priority   = parseBool(val);
            else if (key == "stack_trim")    stack_trim    = parseBool(val);
        } else if (section == "BoostStutterMonitor") {
            if      (key == "stutter_monitor")        stutter_monitor        = parseBool(val);
            else if (key == "stutter_log_threshold")  stutter_log_threshold  = parseFloat(val, 1.0f, 10.0f, 2.0f);
            else if (key == "stutter_log_interval")   stutter_log_interval   = parseInt(val, 10, 1000, 100);
        } else if (section == "BoostPipeline") {
            if      (key == "pipe_drawsort")    pipe_drawsort    = parseBool(val);
            else if (key == "scissor_tight")    scissor_tight    = parseBool(val);
            else if (key == "vertex_dedup")     vertex_dedup     = parseBool(val);
            else if (key == "batch_coalesce")   batch_coalesce   = parseBool(val);
            else if (key == "drawcall_budget")          drawcall_budget          = parseBool(val);
            else if (key == "drawcall_budget_min_fps")  drawcall_budget_min_fps  = parseInt(val, 15, 240, 28);
            // v2.0.0
            else if (key == "effect_lod")           effect_lod           = parseBool(val);
            else if (key == "effect_lod_min_fps")   effect_lod_min_fps   = parseInt(val, 0, 1000, 0);
        } else if (section == "BoostNetwork") {
            if      (key == "dns_prefetch")          dns_prefetch          = parseBool(val);
            else if (key == "http_pool")             http_pool             = parseBool(val);
            else if (key == "online_block_gameplay") online_block_gameplay = parseBool(val);
            else if (key == "server_cache")          server_cache          = parseBool(val);
            else if (key == "winsock_opt")           winsock_opt           = parseBool(val);
        } else if (section == "BoostAudio") {
            if      (key == "fmod_channel_limit") fmod_channel_limit = parseBool(val);
            else if (key == "fmod_max_channels")  fmod_max_channels  = parseInt(val, 4, 64, 16);
            else if (key == "fmod_software_mix")  fmod_software_mix  = parseBool(val);
            else if (key == "audio_thread_pin")   audio_thread_pin   = parseBool(val);
            else if (key == "sound_preload")      sound_preload      = parseBool(val);
            else if (key == "audio_ram_compress") audio_ram_compress = parseBool(val);
        } else if (section == "BoostExtreme") {
            if      (key == "etw_disable")     etw_disable     = parseBool(val);
            else if (key == "wer_disable")     wer_disable     = parseBool(val);
            else if (key == "smartscreen_off") smartscreen_off = parseBool(val);
            else if (key == "numa_aware")      numa_aware      = parseBool(val);
            else if (key == "huge_pages")      huge_pages      = parseBool(val);
            else if (key == "prefetcher_off")  prefetcher_off  = parseBool(val);
            else if (key == "workingset_lock") workingset_lock = parseBool(val);
            else if (key == "gpu_thread_prio") gpu_thread_prio = parseBool(val);
            else if (key == "present_skip_idle") present_skip_idle = parseBool(val);
            // v2.0.0
            else if (key == "thread_qos")        thread_qos        = parseBool(val);
        }
    }

    // Auto-detect mod presence after config load so runtime modules can report it.
    // Compat mode itself is now narrower: only MegaHack-specific detection
    // enables it automatically, unless the user explicitly broadens it.
    mod_loader_detected = hasGeodeMods();
    bool megaHackPresent = hasMegaHack();
    if (megaHackPresent) {
        mod_loader_detected = true;
    }

    if (mod_compat_forced || megaHackPresent || (apply_mod_compat_on_any_mod && mod_loader_detected)) {
        this->applyModCompat();
    }
}
