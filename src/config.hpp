#pragma once
#include <string>
#include <cstdint>

struct Config {
    // ANGLE
    bool        enabled   = true;
    std::string backend   = "d3d11";
    bool        debug     = false;
    std::string log_file  = "angle_log.txt";

    // Boost (base modules)
    bool     gpu_forcer     = true;
    bool     fast_allocator = true;
    bool     timer_fix      = true;
    bool     thread_boost   = false;
    uint32_t cpu_affinity   = 0;
    bool     sse_math       = true;
    bool     power_boost    = false;

    // BoostAdvanced
    bool        tex_compress         = false;
    bool        nvapi_profile        = false;
    bool        shader_cache         = true;
    std::string shader_cache_dir     = "shader_cache";
    bool        large_address_aware  = true;
    bool        gl_state_dedup       = true;
    bool        working_set_prefetch = false;
    bool        fmod_tuning          = false;
    int         fmod_sample_rate     = 44100;
    bool        async_asset_loader   = false;
    int         async_loader_threads = 2;
    bool        force_no_vsync       = false;
    bool        precise_sleep        = false;
    int         heap_compact_interval= 0;

    // BoostRender
    bool depth_off        = false;
    bool mipmap_off       = false;     // safer default — some shaders rely on mipmap sampling
    // Aggressive perf — masks OpenGL errors / forces pipeline behavior. Off by default for stability.
    bool noop_finish      = true;     // make glFinish a no-op (skip pipeline stalls)
    bool noop_geterror    = true;     // glGetError -> GL_NO_ERROR (skip driver round-trip)
    bool vbo_pool         = false;
    int  vbo_pool_size_mb = 16;
    bool vertex_compress  = false;    // risky by default
    bool instancing       = false;
    bool dyn_resolution   = false;
    int  dyn_res_target_fps = 60;

    // BoostIO
    bool        fast_io        = false;
    bool        ramdisk_cache  = false;
    std::string ramdisk_path   = "";
    bool        loader_cache   = false;

    // ── New (v2.0.0): IOCP-based asset prewarmer (fast NVMe / safe HDD) ──
    bool        iocp_loader              = false;  // opt-in
    int         iocp_loader_concurrency  = 0;      // 0 = auto-detect (1 on HDD, 4 on SSD)

    // BoostCPU
    bool sse_memcpy    = false;
    bool string_intern = false;
    bool mimalloc_full = false;
    bool silent_debug  = false;

    // BoostSystem
    bool wddm_priority   = false;
    bool game_mode       = false;
    bool smart_cpu_pin   = false;
    bool mitigation_off  = false;     // security impact

    // BoostLatency
    bool allow_tearing    = false;  // DISABLED by default - causes dxgi.dll crashes
    bool frame_pacing     = true;
    int  frame_pacing_target = 0;     // 0 = auto-detect monitor refresh; >0 = forced FPS cap
    bool mmcss_pro_audio  = false;
    bool shader_warmup    = true;    // warm up shaders to avoid mid-game compile stutters
    bool low_latency      = false;
    bool safe_latency     = false;    // Geode-compatible latency optimizations (thread priority, affinity, DWM sync)
    bool anti_stutter     = false;     // disable affinity auto-update + EcoQoS thread throttling for jitter

    // BoostGD
    bool skip_intro       = false;
    bool object_pool      = true;
    int  object_pool_size = 4096;
    bool plist_binary     = false;
    std::string plist_cache_dir = "plist_cache";
    bool skip_shake_flash = false;
    bool level_predecode  = false;
    int  predecode_threads = 2;

    // BoostRenderAdv
    int  atlas_size       = 2048;
    bool fbo_cache        = false;
    int  fbo_pool_size    = 8;
    bool triple_buffer    = false;
    bool disable_aa       = false;
    bool blend_optimize   = true;

    // BoostCocos
    bool particle_throttle = true;
    int  particle_max      = 300;
    bool texcache_preload  = false;
    bool label_cache       = true;
    bool scheduler_skip    = true;
    bool drawcall_sort     = false;
    bool index_buffer_gen  = true;

    // BoostSysAdv
    bool ftz_daz           = false;
    bool spectre_off       = false;   // security impact
    bool io_priority       = false;
    bool mem_priority       = false;
    bool stack_trim        = false;

    // BoostStutterMonitor
    bool stutter_monitor   = false;   // OFF by default - enable for diagnostics
    float stutter_log_threshold = 2.0f;  // log frames > 2x target frame time
    int  stutter_log_interval = 100;  // log every N frames

    // BoostPipeline
    bool pipe_drawsort     = false;
    bool scissor_tight     = false;   // can clip sprites
    bool vertex_dedup      = true;
    bool batch_coalesce    = false;
    bool drawcall_budget        = false;   // adaptive draw call throttle for min-FPS floor
    int  drawcall_budget_min_fps = 28;    // target minimum FPS before throttling kicks in

    // ── New (v2.0.0): low-impact, картинку не портят, безопасны на любых ПК ──
    // Adaptive Effect LOD: при просадке FPS режет mass-particle batches,
    // основная картинка/иконка/UI/ground остаются нетронутыми.
    bool effect_lod          = false;
    int  effect_lod_min_fps  = 0;     // 0 = взять frame_pacing_target
    // Static Buffer Cache: пропускает identical glBufferData uploads
    // (защищает от лишней работы драйвера на статичных батчах фон/ground).

    // BoostNetwork
    bool dns_prefetch          = false;
    bool http_pool             = false;
    bool online_block_gameplay = false;
    bool server_cache          = false;
    bool winsock_opt           = false;

    // BoostAudio
    bool fmod_channel_limit = false;
    int  fmod_max_channels  = 16;
    bool fmod_software_mix  = false;
    bool audio_thread_pin   = false;
    bool sound_preload      = false;
    bool audio_ram_compress = false;

    // BoostExtreme
    bool etw_disable       = false;
    bool wer_disable       = false;
    bool smartscreen_off   = false;
    bool numa_aware        = false;
    bool huge_pages        = false;   // needs SeLockMemoryPrivilege
    bool prefetcher_off    = false;   // needs admin

    // BoostExtreme — additional Windows-level perf wins
    bool workingset_lock   = false;    // hard-pin min working set so OS can't page us out
    bool gpu_thread_prio   = false;    // IDXGIDevice::SetGPUThreadPriority(+7)

    // ── New (v2.0.0): per-thread EcoQoS opt-out for ALL threads ──
    bool thread_qos        = false;    // disables Windows EcoQoS for every existing thread

    // Optional modules: idle skip + halfres rendering
    bool present_skip_idle = false;   // skip Present when 0 draws since last frame (opt-in)

    // MegaHack / external mod compat
    bool megahack_detected = false;          // compat mode active
    bool mod_loader_detected = false;        // informational: Geode / external mod presence seen
    bool mod_compat_forced = false;          // user override: always apply compat mode
    bool apply_mod_compat_on_any_mod = false;// broader auto-compat for non-MegaHack mod setups
    // Force-bind FBO 0 before eglSwapBuffers when MegaHack mod is present.
    // Off by default in the previous code (mistakenly — see wgl_proxy.cpp);
    // re-enabled here so the next frame always starts with FBO 0 bound.
    bool megahack_force_fbo0_on_swap = true;
    // Request EGL_SWAP_BEHAVIOR = EGL_BUFFER_PRESERVED. OFF by default —
    // forces ANGLE into a BLIT-model swap chain on D3D11, which adds an
    // extra full-screen copy per present and induces micro-stutter even
    // at high frame-rates (observed on 90 Hz monitor at 180 fps). Turn
    // on as a band-aid if you see black frames between presents.
    bool megahack_preserve_swap_chain = false;
    // Verbose draw diagnostic: log full GL state before every 200th draw
    // call. OFF by default — only enable when chasing a "game appears to
    // draw but screen is black" issue. Adds non-trivial overhead.
    bool megahack_dump_draw_state = false;

    // ── Elite D3D11 perf (v13) ──
    bool d3d11_unlock      = true;   // ID3D11Multithread::SetMultithreadProtected(FALSE)
    bool process_perf_lock = true;   // Windows power throttling OFF for process+render thread
    bool pcore_pin         = true;   // Intel hybrid: pin render thread to P-cores; disable boost
    bool gpu_residency     = true;   // Reserve VRAM via IDXGIAdapter3 to prevent eviction stutters

    static Config& get();
    void load(const char* path);
    void applyModCompat();
};
