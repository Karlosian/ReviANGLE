// ReviANGLE proxy opengl32.dll entry point.
// Initialises config, applies all boost modules, defers ANGLE until wglCreateContext.
//
// ReviANGLE  —  Performance Suite for Geometry Dash  by Reviusion

#include <windows.h>
#include <cstdio>
#include "config.hpp"
#include "angle_loader.hpp"
#include "gl_proxy.hpp"

// ===== Global crash handler — logs exception info before process dies =====
static PVOID s_vehHandle = nullptr;
static LONG WINAPI crashHandler(PEXCEPTION_POINTERS ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Only handle fatal crashes: AV, illegal instruction, stack overflow, etc.
    if (code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH; // C++ exception, not our problem
    if (code == 0x406D1388) return EXCEPTION_CONTINUE_SEARCH; // thread naming
    // Log crash details
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)addr, &mod)) {
        char modName[MAX_PATH] = {};
        GetModuleFileNameA(mod, modName, sizeof(modName));
        char* slash = strrchr(modName, '\\');
        angle::forceLog("!!! CRASH: code=0x%08X addr=%p module=%s !!!",
                        code, addr, slash ? slash + 1 : modName);
    } else {
        angle::forceLog("!!! CRASH: code=0x%08X addr=%p (no module) !!!", code, addr);
    }
    // Log register state for debugging
    angle::forceLog("  RAX=%p RCX=%p RDX=%p R8=%p",
                    (void*)ep->ContextRecord->Rax, (void*)ep->ContextRecord->Rcx,
                    (void*)ep->ContextRecord->Rdx, (void*)ep->ContextRecord->R8);
    angle::forceLog("  RIP=%p RSP=%p RBP=%p",
                    (void*)ep->ContextRecord->Rip, (void*)ep->ContextRecord->Rsp,
                    (void*)ep->ContextRecord->Rbp);
    // Identify the CALLER (return address from stack) — tells us who called cocos2d.
    void** stack = (void**)ep->ContextRecord->Rsp;
    HMODULE callerMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)stack[0], &callerMod)) {
        char callerName[MAX_PATH] = {};
        GetModuleFileNameA(callerMod, callerName, sizeof(callerName));
        char* slash2 = strrchr(callerName, '\\');
        angle::forceLog("  Caller: %p module=%s", stack[0], slash2 ? slash2 + 1 : callerName);
    } else {
        angle::forceLog("  Caller: %p (no module)", stack[0]);
    }
    // Also identify the next 2 return addresses for deeper call chain
    for (int i = 1; i <= 2; i++) {
        HMODULE m = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)stack[i], &m)) {
            char mn[MAX_PATH] = {};
            GetModuleFileNameA(m, mn, sizeof(mn));
            char* s = strrchr(mn, '\\');
            angle::forceLog("  Stack[%d]: %p module=%s", i, stack[i], s ? s + 1 : mn);
        }
    }
    // Dump rest of stack raw
    angle::forceLog("  Stack raw: %p %p %p %p %p %p %p %p",
                    stack[0], stack[1], stack[2], stack[3],
                    stack[4], stack[5], stack[6], stack[7]);
    return EXCEPTION_CONTINUE_SEARCH; // let the crash continue (we just want the log)
}

// base
namespace boost_gpu    { void apply(); }
namespace boost_timer  { void apply(); void restore(); }
namespace boost_thread { void apply(); }
namespace boost_power  { void apply(); }
namespace boost_alloc  { void apply(); void shutdown(); }
namespace boost_math   { void apply(); }

// advanced
namespace boost_tex_compress  { void apply(); }
namespace boost_nvapi         { void apply(); }
namespace boost_shader_cache  { void apply(); }
namespace boost_laa           { void apply(); }
namespace boost_gl_dedup      { void apply(); }
namespace boost_prefetch      { void apply(); }
namespace boost_fmod          { void apply(); }
namespace boost_asset_loader  { void apply(); void shutdown(); }
namespace boost_vsync         { void apply(); }
namespace boost_sleep         { void apply(); void shutdown(); }
namespace boost_heap          { void apply(); void shutdown(); }
namespace boost_no_fs_optimizations { void apply(); }
// render
namespace boost_depth_off        { void apply(); }
namespace boost_mipmap_off       { void apply(); }
namespace boost_vbo_pool         { void apply(); }
namespace boost_vertex_compress  { void apply(); }
namespace boost_instancing       { void apply(); }
namespace boost_dyn_res          { void apply(); }

// IO
namespace boost_fast_io        { void apply(); }
namespace boost_ramdisk        { void apply(); void shutdown(); }
namespace boost_loader_cache   { void apply(); }

// CPU
namespace boost_sse_memcpy     { void apply(); }
namespace boost_string_intern  { void apply(); }
namespace boost_mimalloc_full  { void apply(); }
namespace boost_silent         { void apply(); }

// system
namespace boost_wddm_prio     { void apply(); }
namespace boost_gamemode       { void apply(); }
namespace boost_smart_pin      { void apply(); }
namespace boost_mitigation_off { void apply(); }

// anti-stutter
namespace boost_allow_tearing  { void apply(); }
namespace boost_frame_pacing   { void apply(); void shutdown(); }
namespace boost_mmcss          { void apply(); void shutdown(); }
namespace boost_shader_warmup  { void apply(); }
namespace boost_low_latency    { void apply(); }
namespace boost_d3d11_unlock   { void apply(); }
namespace boost_process_perf  { void apply(); }
namespace boost_pcore_pin     { void apply(); }
namespace boost_gpu_residency { void apply(); }
namespace boost_safe_latency   { void apply(); }
namespace boost_stutter_monitor { void apply(); void shutdown(); }

// GD-specific
namespace boost_skip_intro       { void apply(); }
namespace boost_obj_pool         { void apply(); void shutdown(); }
namespace boost_plist_bin        { void apply(); }
namespace boost_skip_effects     { void apply(); }
namespace boost_level_predecode  { void apply(); void shutdown(); }
namespace boost_anti_stutter     { void apply(); }

// advanced render
namespace boost_fbo_cache      { void apply(); void shutdown(); }
namespace boost_triple_buffer  { void apply(); }
namespace boost_no_aa          { void apply(); }
namespace boost_blend_opt      { void apply(); }
// cocos2d-x
namespace boost_particle_throttle { void apply(); }
namespace boost_texcache_preload  { void apply(); }
namespace boost_label_cache       { void apply(); }
namespace boost_scheduler_skip    { void apply(); }
namespace boost_drawcall_sort     { void apply(); }
namespace boost_index_gen         { void apply(); }

// system advanced
namespace boost_ftz_daz       { void apply(); }
namespace boost_spectre_off   { void apply(); }
namespace boost_io_priority   { void apply(); }
namespace boost_mem_priority  { void apply(); }
namespace boost_stack_trim    { void apply(); }

// pipeline
namespace boost_drawsort        { void apply(); }
namespace boost_scissor_tight   { void apply(); }
namespace boost_vertex_dedup    { void apply(); }
namespace boost_batch_coalesce  { void apply(); }

// network
namespace boost_dns_prefetch  { void apply(); }
namespace boost_http_pool     { void apply(); }
namespace boost_online_block  { void apply(); }
namespace boost_server_cache  { void apply(); }
namespace boost_winsock_opt   { void apply(); }

// audio
namespace boost_fmod_channels   { void apply(); }
namespace boost_fmod_softmix    { void apply(); }
namespace boost_audio_pin       { void apply(); }
namespace boost_sound_preload   { void apply(); }
namespace boost_audio_compress  { void apply(); }

// extreme
namespace boost_etw_off         { void apply(); }
namespace boost_wer_off         { void apply(); }
namespace boost_smartscreen_off { void apply(); }
namespace boost_numa            { void apply(); }
namespace boost_huge_pages      { void apply(); }
namespace boost_prefetcher_off  { void apply(); }

// Windows-level extras
namespace boost_workingset_lock { void apply(); }

// opt-in idle skip
namespace boost_present_skip   { void apply(); }

// ── New modules (v2.0.0): all picture-safe, work on old + new HW ───────────
namespace boost_effect_lod        { void apply(); }
namespace boost_drawcall_budget   { void apply(); }
namespace boost_iocp_loader   { void apply(); void shutdown(); }
namespace boost_thread_qos    { void apply(); }

static void onAttach(HMODULE self) {
    DisableThreadLibraryCalls(self);
    Config::get().load("angle_config.ini");

    // Register crash handler — last-chance logging before process dies.
    // Registered early so we catch crashes in boost module init too.
    if (!s_vehHandle) {
        s_vehHandle = AddVectoredExceptionHandler(1 /*first in chain*/, crashHandler);
    }

    // ---- base modules (safe, no GL dependency) ----
    boost_gpu::apply();
    boost_timer::apply();
    boost_thread::apply();
    boost_power::apply();
    boost_alloc::apply();
    boost_math::apply();

    // ---- advanced (system-level, no GL) ----
    boost_laa::apply();
    // boost_nvapi::apply() — moved to gdangle_postGLInit() (see below).
    // Calling LoadLibraryA("nvapi64.dll") from DllMain re-enters the loader
    // lock; nvapi64.dll's own DllMain pulls in heavy Nvidia driver DLLs
    // (nvldumdx.dll, etc.), some of which fail under nested loader-lock
    // and abort the entire DLL load with STATUS_DLL_INIT_FAILED (0xC0000142).
    // boost_prefetch::apply() — moved to gdangle_postGLInit() (may create threads).
    boost_fmod::apply();
    // boost_asset_loader::apply() — moved to gdangle_postGLInit() (creates worker threads).
    boost_sleep::apply();
    boost_heap::apply();

    // ---- IO (moved to gdangle_postGLInit to avoid Loader Lock) ----
    // boost_fast_io::apply() — moved (registers I/O hooks, may allocate).
    // boost_ramdisk::apply() — moved (memory-mapped I/O, large allocations).
    // boost_loader_cache::apply() — moved (cache initialization).

    // ---- CPU ----
    boost_mimalloc_full::apply();
    boost_sse_memcpy::apply();
    boost_string_intern::apply();
    boost_silent::apply();

    // ---- system ----
    boost_wddm_prio::apply();
    boost_gamemode::apply();
    boost_smart_pin::apply();
    boost_mitigation_off::apply();

    // ---- anti-stutter (system-level, no GL) ----
    // boost_mmcss::apply() — moved to gdangle_postGLInit() (MMCSS calls can block).
    boost_anti_stutter::apply();   // disable affinity update + EcoQoS thread throttling

    // ---- GD-specific (IAT hooks, no GL) ----
    boost_skip_intro::apply();
    boost_obj_pool::apply();
    boost_plist_bin::apply();
    // boost_level_predecode::apply() — moved to gdangle_postGLInit() (creates threads).

    // ---- system advanced (no GL) ----
    boost_ftz_daz::apply();
    boost_spectre_off::apply();
    boost_io_priority::apply();
    boost_mem_priority::apply();
    boost_stack_trim::apply();

    // ---- cocos2d-x (no GL) ----
    boost_texcache_preload::apply();
    boost_scheduler_skip::apply();

    // ---- network (moved to gdangle_postGLInit to avoid Loader Lock) ----
    // boost_dns_prefetch::apply() — moved (may call networking APIs).
    // boost_http_pool::apply() — moved (creates threads).
    // boost_online_block::apply() — moved (Winsock hooks).
    // boost_server_cache::apply() — moved (cache init).
    // boost_winsock_opt::apply() — moved (socket configuration).

    // ---- audio (moved to gdangle_postGLInit to avoid Loader Lock) ----
    // boost_fmod_channels::apply() — moved (FMOD channel configuration).
    // boost_fmod_softmix::apply() — moved (FMOD mixer settings).
    // boost_audio_pin::apply() — moved (thread priority changes).
    // boost_sound_preload::apply() — moved (I/O operations).
    // boost_audio_compress::apply() — moved (codec initialization).

    // ---- extreme ----
    boost_etw_off::apply();
    boost_wer_off::apply();
    boost_smartscreen_off::apply();
    boost_numa::apply();
    boost_huge_pages::apply();
    boost_prefetcher_off::apply();
    boost_workingset_lock::apply();

    // — opt-in optimizations
    boost_present_skip::apply();

    // ---- GL-dependent modules (deferred until context creation) ----
    // Called from wgl_wglMakeCurrent -> gdangle_postGLInit()

    if (Config::get().megahack_detected) {
        angle::forceLog("MODS DETECTED — compatibility mode active:");
        angle::forceLog("  disabled: allow_tearing, low_latency, gpu_thread_prio (DXGI/D3D11 vtable hooks)");
        angle::forceLog("  disabled: gl_state_dedup (stale cache risk), frame_pacing (SwapBuffers hook timing)");
        angle::forceLog("  disabled: noop_geterror (foreign GL error visibility)");
        angle::forceLog("  disabled: blend_optimize, vertex_dedup, shader_cache (render pipeline)");
        angle::forceLog("  disabled: mipmap_off, drawcall_sort, batch_coalesce (mod overlay compat)");
    }

    angle::log("ReviANGLE attached \u2014 84 boost modules, backend=%s  (by Reviusion)",
               Config::get().backend.c_str());
}

// Called once after wglMakeCurrent succeeds — safe to touch GL now.
// Also: we are well outside the DLL loader lock here, so it's safe to
// LoadLibrary heavy DLLs like nvapi64.dll without risking 0xC0000142.
static void writeLoadMarkerDeferred();   // forward decl, defined below
void gdangle_postGLInit() {
    angle::forceLog("[MAIN] === gdangle_postGLInit() START ===");

    // Deferred load marker — safe to do file I/O here (outside Loader Lock).
    writeLoadMarkerDeferred();

    // ---- IO (safe outside Loader Lock) ----
    angle::forceLog("[MAIN] boost_fast_io::apply()");
    boost_fast_io::apply();
    angle::forceLog("[MAIN] boost_ramdisk::apply()");
    boost_ramdisk::apply();
    angle::forceLog("[MAIN] boost_loader_cache::apply()");
    boost_loader_cache::apply();

    // ---- anti-stutter (moved from onAttach) ----
    angle::forceLog("[MAIN] boost_mmcss::apply()");
    boost_mmcss::apply();

    // ---- GD-specific (moved from onAttach) ----
    angle::forceLog("[MAIN] boost_level_predecode::apply()");
    boost_level_predecode::apply();

    // ---- network (safe outside Loader Lock) ----
    angle::forceLog("[MAIN] network boosts...");
    boost_dns_prefetch::apply();
    boost_http_pool::apply();
    boost_online_block::apply();
    boost_server_cache::apply();
    boost_winsock_opt::apply();

    // ---- audio (safe outside Loader Lock) ----
    angle::forceLog("[MAIN] audio boosts...");
    boost_fmod_channels::apply();
    boost_fmod_softmix::apply();
    boost_audio_pin::apply();
    boost_sound_preload::apply();
    boost_audio_compress::apply();

    // ---- prefetch (moved from onAttach) ----
    angle::forceLog("[MAIN] boost_prefetch::apply()");
    boost_prefetch::apply();

    // ---- asset loader (moved from onAttach) ----
    angle::forceLog("[MAIN] boost_asset_loader::apply()");
    boost_asset_loader::apply();

    // Diagnostic: log the *real* GPU ANGLE picked. On Optimus laptops this
    // confirms whether NvOptimusEnablement actually kicked the dGPU in.
    // Format from ANGLE: "ANGLE (NVIDIA, NVIDIA GeForce dGPU Direct3D11 ...)"
    {
        HMODULE gles = GetModuleHandleA("libGLESv2.dll");
        if (gles) {
            using PFN_GetString = const char* (WINAPI*)(unsigned int);
            auto getStr = (PFN_GetString)GetProcAddress(gles, "glGetString");
            if (getStr) {
                const char* vendor   = getStr(0x1F00); // GL_VENDOR
                const char* renderer = getStr(0x1F01); // GL_RENDERER
                const char* version  = getStr(0x1F02); // GL_VERSION
                angle::forceLog("GPU active: vendor='%s' renderer='%s' version='%s'",
                           vendor   ? vendor   : "?",
                           renderer ? renderer : "?",
                           version  ? version  : "?");
            }
        }
    }

    // Deferred from onAttach because nvapi64.dll's DllMain re-enters loader
    // lock and aborts process startup if loaded too early.
    boost_nvapi::apply();

    // GL-dependent
    angle::forceLog("[MAIN] GL-dependent modules...");
    boost_tex_compress::apply();
    boost_gl_dedup::apply();
    boost_shader_cache::apply();
    boost_depth_off::apply();
    boost_mipmap_off::apply();
    boost_vbo_pool::apply();
    boost_vertex_compress::apply();
    boost_instancing::apply();
    boost_dyn_res::apply();
    boost_vsync::apply();
    boost_no_fs_optimizations::apply();  // Disable Windows FSO for lower input lag

    // anti-stutter (GL-dependent)
    angle::forceLog("[MAIN] anti-stutter GL-dependent...");
    boost_allow_tearing::apply();
    boost_frame_pacing::apply();
    boost_shader_warmup::apply();
    boost_low_latency::apply();
    boost_d3d11_unlock::apply();   // D3D11 single-thread unlock (CPU)
    boost_process_perf::apply();   // Win power throttling OFF
    boost_pcore_pin::apply();      // Hybrid CPU P-core pinning
    boost_gpu_residency::apply();  // VRAM reservation to prevent eviction
    boost_safe_latency::apply();
    boost_stutter_monitor::apply();

    // GD-specific (GL-dependent)
    boost_skip_effects::apply();

    // advanced render
    angle::forceLog("[MAIN] advanced render...");
    boost_fbo_cache::apply();
    boost_triple_buffer::apply();
    boost_no_aa::apply();
    boost_blend_opt::apply();

    // cocos2d-x (GL-dependent)
    angle::forceLog("[MAIN] cocos2d-x GL-dependent...");
    boost_particle_throttle::apply();
    boost_label_cache::apply();
    boost_drawcall_sort::apply();
    boost_index_gen::apply();

    // pipeline (GL-dependent)
    angle::forceLog("[MAIN] pipeline GL-dependent...");
    boost_drawsort::apply();
    boost_scissor_tight::apply();
    boost_vertex_dedup::apply();
    boost_batch_coalesce::apply();

    // ── v2.0.0: 5 new low-impact modules (safe on old + new HW) ──
    angle::forceLog("[MAIN] v2.0.0 modules...");
    boost_effect_lod::apply();          // adaptive LOD when FPS drops
    boost_drawcall_budget::apply();     // adaptive draw call budget — guarantees min FPS floor
    boost_iocp_loader::apply();     // overlapped IOCP asset prewarm (opt-in)
    // boost_present_hints::apply();   // DISABLED - suspected Geode conflict
    boost_thread_qos::apply();      // per-thread EcoQoS opt-out for ALL threads

    angle::forceLog("ReviANGLE post-GL-init complete");
}

static void onDetach() {
    boost_timer::restore();
    boost_sleep::shutdown();
    boost_heap::shutdown();
    boost_asset_loader::shutdown();
    boost_mmcss::shutdown();
    boost_obj_pool::shutdown();
    boost_level_predecode::shutdown();
    boost_fbo_cache::shutdown();
    boost_frame_pacing::shutdown();
    boost_iocp_loader::shutdown();    // v2.0.0
    boost_stutter_monitor::shutdown();  // stutter monitor
    boost_alloc::shutdown();         // free slab pools
    angle::shutdown();
}

// Unconditional load marker — proves our DLL was loaded.
// Only OutputDebugStringA here: fopen/fclose under Loader Lock can deadlock
// on slow disks (NTFS journal + AV scan). File write is deferred to
// gdangle_writeLoadMarkerDeferred(), called from gdangle_postGLInit().
static HMODULE s_selfModule = nullptr;
static void writeLoadMarker(HMODULE self, DWORD /*reason*/) {
    s_selfModule = self;
    OutputDebugStringA("[ReviANGLE] DllMain DLL_PROCESS_ATTACH\n");
}

// Called from gdangle_postGLInit() — outside Loader Lock, safe to do file I/O.
static void writeLoadMarkerDeferred() {
    char dllPath[MAX_PATH] = {};
    if (s_selfModule) GetModuleFileNameA(s_selfModule, dllPath, MAX_PATH);
    char* slash = strrchr(dllPath, '\\');
    if (!slash) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - dllPath), "gdangle_loaded.txt");

    FILE* f = nullptr;
    fopen_s(&f, dllPath, "a");
    if (!f) {
        char tmp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, tmp);
        strcat_s(tmp, "gdangle_loaded.txt");
        fopen_s(&f, tmp, "a");
        if (!f) return;
    }
    SYSTEMTIME st; GetLocalTime(&st);
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fprintf(f, "[%02d:%02d:%02d.%03d] pid=%lu exe=%s dll=%s\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentProcessId(), exePath, dllPath);
    fclose(f);
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) writeLoadMarker(self, reason);
    switch (reason) {
        case DLL_PROCESS_ATTACH: onAttach(self); break;
        case DLL_PROCESS_DETACH: onDetach();     break;
    }
    return TRUE;
}
