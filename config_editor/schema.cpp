#include "schema.hpp"

// Description style notes:
//  - desc_en / desc_ru: 1–3 short lines, plain text, no leading "EN:" prefix.
//  - status: short tag matching what's in angle_config.ini ([✅ ON], etc.).
//    UI strips the brackets and colors based on first ASCII letter.
//  - For Int options, min_int/max_int give the InputInt range. Use sensible
//    defaults (most are 0..65535).

static const OptionDef g_opts[] = {

    // ────────── [ANGLE] ─────────────────────────────────────────────────────
    { "ANGLE", "enabled", OptType::Bool, "true",
      "Enable or disable the ReviANGLE mod entirely. If disabled, all OpenGL calls bypass "
      "the DirectX translation layer and route directly to the system opengl32.dll.",
      "Включить или полностью выключить мод ReviANGLE. Если выключен, игра работает "
      "на стандартном OpenGL в обход трансляции DirectX.",
      "ON" },
    { "ANGLE", "backend", OptType::Enum, "d3d11",
      "[OLD PC] Use d3d11 — works everywhere including integrated GPUs. "
      "[NEW PC] Use d3d11n for DX12 feature level support.",
      "[СТАРЫЙ ПК] d3d11 — работает везде, включая интегрированные GPU. "
      "[НОВЫЙ ПК] d3d11n — поддержка DX12 feature level.",
      "ON", 0, 65535, "d3d11,d3d11n,d3d9" },
    { "ANGLE", "frame_pacing", OptType::Bool, "true",
      "[CRITICAL] QPC-based frame pacing with sleep+spin loop. Best for stability on any PC. "
      "[OLD PC] 60 FPS. [NEW PC] 120-180 FPS.",
      "[КРИТИЧНО] Frame pacing через QPC. Лучшая стабильность на любом ПК. "
      "[СТАРЫЙ ПК] 60 FPS. [НОВЫЙ ПК] 120-180 FPS.",
      "ON — best feel preset" },
    { "ANGLE", "frame_pacing_target", OptType::Int, "180",
      "[OLD PC] Recommended: 60 FPS. [NEW PC] 120-180 FPS (2x refresh on 90Hz).",
      "[СТАРЫЙ ПК] Рекомендуется: 60 FPS. [НОВЫЙ ПК] 120-180 FPS.",
      "180 — 2x refresh on 90 Hz",
      0, 1000 },
    { "ANGLE", "debug", OptType::Bool, "false",
      "Persistent debug log file. One fopen for the process lifetime — no "
      "per-call hitches. Default OFF for production; flip ON only when "
      "diagnosing init issues.",
      "Отладочный лог. Один fopen на весь процесс, без микрофризов. По умолчанию "
      "ВЫКЛ для production; включай только для диагностики init.",
      "OFF — production default" },
    { "ANGLE", "log_file", OptType::String, "angle_log.txt",
      "Path to the debug log file (relative to GD.exe).",
      "Путь к отладочному логу (относительно GD.exe).",
      "ON" },
    { "ANGLE", "force_no_vsync", OptType::Bool, "false",
      "[LOW LATENCY] Forces eglSwapInterval(0) + disables Windows Fullscreen Optimizations. "
      "Required for sub-frame input latency. May cause tearing without adaptive sync monitor.",
      "[LOW LATENCY] Принудительно отключает VSync и FSO Windows. Нужно для максимального "
      "отклика. Может давать tearing без adaptive sync монитора.",
      "OFF — enable for competitive play" },

    // ────────── [Boost] ─────────────────────────────────────────────────────
    { "Boost", "gpu_forcer", OptType::Bool, "true",
      "Exports NvOptimusEnablement=1 / AmdPowerXpressRequest=1 — forces the "
      "discrete GPU on Optimus / switchable-graphics laptops.",
      "Экспортирует NvOptimusEnablement / AmdPowerXpress — переключает "
      "Optimus / switchable-graphics ноут на дискретную видеокарту.",
      "ON" },
    { "Boost", "fast_allocator", OptType::Bool, "true",
      "Replaces the default heap allocator with a lock-free pool. ~3-5% "
      "win on alloc-heavy frames, no risk.",
      "Заменяет стандартный аллокатор на lock-free пул. ~3-5% буст на "
      "frame-ах с аллокациями, без риска.",
      "ON — 3-5% free win" },
    { "Boost", "timer_fix", OptType::Bool, "true",
      "[ALL PC] timeBeginPeriod(1) — sub-millisecond Sleep granularity.",
      "[ВСЕ ПК] Включает 1 ms точность Sleep для точного frame pacing.",
      "ON" },
    { "Boost", "thread_boost", OptType::Bool, "true",
      "ABOVE_NORMAL process priority + main thread HIGHEST. "
      "Helps when background apps compete for CPU.",
      "ABOVE_NORMAL приоритет процесса + HIGHEST для главного потока. "
      "Помогает когда фон жрёт CPU.",
      "ON" },
    { "Boost", "cpu_affinity", OptType::Hex, "0",
      "[OLD PC] Keep 0 — let scheduler pick, manual pinning wastes cores. "
      "[NEW PC] Can pin to specific cores (e.g. 0xF for 4 cores).",
      "[СТАРЫЙ ПК] Ставь 0 — планировщик сам выберет. "
      "[НОВЫЙ ПК] Можно привязать к конкретным ядрам (0xF для 4 ядер).",
      "ON — auto mode" },
    { "Boost", "sse_math", OptType::Bool, "true",
      "[ALL PC] SSE2 fast math intrinsics. Improves CPU vectorization. "
      "Safe on all modern processors.",
      "[ВСЕ ПК] Быстрые инструкции SSE2. Безопасно на всех современных CPU.",
      "ON" },
    { "Boost", "power_boost", OptType::Bool, "true",
      "[ALL PC] Disables Windows PROCESS_POWER_THROTTLING (EcoQoS). "
      "Stops OS from downclocking GD.",
      "[ВСЕ ПК] Отключает EcoQoS — Windows перестаёт даунклокать GD.",
      "ON" },

    // ────────── [BoostAdvanced] ─────────────────────────────────────────────
    { "BoostAdvanced", "tex_compress", OptType::Bool, "false",
      "[OLD PC] Keep OFF — breaks rendering on FL9 path. "
      "[NEW PC] Can enable — BC7/ASTC compression saves VRAM.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — ломает текстуры на FL9. "
      "[НОВЫЙ ПК] Можно включить — BC7/ASTC экономят VRAM.",
      "OFF — breaks textures on FL9 path" },
    { "BoostAdvanced", "nvapi_profile", OptType::Bool, "true",
      "[ALL PC] NVAPI driver profile — signals max perf to NVIDIA driver.",
      "[ВСЕ ПК] NVAPI профиль — включает max-perf режим NVIDIA драйвера.",
      "ON — verified initialized" },
    { "BoostAdvanced", "shader_cache", OptType::Bool, "true",
      "[ALL PC] Saves compiled HLSL to disk. Big win on slow CPU.",
      "[ВСЕ ПК] Кеширует HLSL на диске. Большой плюс на слабом CPU.",
      "ON" },
    { "BoostAdvanced", "shader_cache_dir", OptType::String, "shader_cache",
      "Folder for the shader cache (relative to GD.exe).",
      "Папка для кеша шейдеров (относительно GD.exe).",
      "ON" },
    { "BoostAdvanced", "large_address_aware", OptType::Bool, "true",
      "Patches PE header so GD can use 4 GB virtual memory instead of 2 GB. "
      "Useful for large levels with thousands of objects.",
      "Патчит PE-заголовок чтобы GD имел доступ к 4 GB virtual памяти "
      "вместо 2. Полезно для больших уровней.",
      "ON" },
    { "BoostAdvanced", "gl_state_dedup", OptType::Bool, "true",
      "Skips redundant glBindTexture / glUseProgram / glActiveTexture calls "
      "when state hasn't changed. Saves driver round-trips.",
      "Пропускает повторные glBindTexture/glUseProgram/glActiveTexture. "
      "Экономит обращения к драйверу.",
      "ON" },
    { "BoostAdvanced", "working_set_prefetch", OptType::Bool, "false",
      "Pre-faults GD code pages into RAM. OFF — can cause initialization latency on "
      "certain storage configurations.",
      "Принудительно загружает страницы кода игры в RAM при старте. ВЫКЛ — может "
      "вызывать задержки инициализации на некоторых конфигурациях дисков.",
      "OFF" },
    { "BoostAdvanced", "fmod_tuning", OptType::Bool, "false",
      "FMOD audio engine tuning. OFF — IAT hook on FMOD crashes some builds.",
      "Тюнинг FMOD. ВЫКЛ — IAT hook на FMOD иногда крашит игру.",
      "OFF — IAT hook crash risk" },
    { "BoostAdvanced", "fmod_sample_rate", OptType::Int, "44100",
      "FMOD sample rate when fmod_tuning=true. Standard values: 44100, 48000.",
      "FMOD sample rate когда fmod_tuning=true. Стандарт: 44100 или 48000.",
      "OFF — fmod_tuning is off",
      8000, 96000 },
    { "BoostAdvanced", "async_asset_loader", OptType::Bool, "false",
      "[OLD PC] Keep OFF — parallel threads steal CPU from main loop. "
      "[NEW PC] Enable for faster level loading (4 threads recommended).",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — параллельные потоки отнимают CPU у главного потока. "
      "[НОВЫЙ ПК] Включить для быстрой загрузки уровней (4 потока).",
      "OFF" },
    { "BoostAdvanced", "async_loader_threads", OptType::Int, "2",
      "[OLD PC] Use 0 (disabled). [NEW PC] Use 4 for parallel loading.",
      "[СТАРЫЙ ПК] 0 (выключено). [НОВЫЙ ПК] 4 для параллельной загрузки.",
      "OFF — async loader off",
      0, 16 },
    { "BoostAdvanced", "precise_sleep", OptType::Bool, "true",
      "Replaces Sleep() with high-resolution spin-wait for ≤2 ms. Reduces jitter "
      "but increases CPU usage on the main thread.",
      "Заменяет Sleep() на точный высокоточный spin-wait для интервалов ≤2 ms. "
      "Снижает задержки и микрофризы планировщика за счет загрузки ядра CPU.",
      "ON" },
    { "BoostAdvanced", "heap_compact_interval", OptType::Int, "0",
      "Periodic HeapCompact() interval in seconds. 0 = disabled. Heap compact "
      "during gameplay causes 50-200 ms stutter — keep 0.",
      "Период HeapCompact() в секундах. 0 = выкл. HeapCompact во время "
      "геймплея даёт 50-200 ms заикания — оставь 0.",
      "0 = disabled",
      0, 600 },
    { "BoostAdvanced", "d3d11_multithread", OptType::Bool, "false",
      "[OLD PC] Keep OFF — conflicts with ANGLE threading. "
      "[NEW PC] Can enable — uses all GPU cores.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — конфликтует с threading-ом ANGLE. "
      "[НОВЫЙ ПК] Можно включить — использует все ядра GPU.",
      "OFF — ANGLE conflict" },

    // ────────── [BoostRender] ───────────────────────────────────────────────
    { "BoostRender", "depth_off", OptType::Bool, "false",
      "Disables depth test/clear. OFF — GD uses depth buffer for trigger "
      "ordering on certain levels.",
      "Выключает depth test/clear. ВЫКЛ — GD использует depth для "
      "порядка триггеров.",
      "OFF — breaks triggers" },
    { "BoostRender", "mipmap_off", OptType::Bool, "true",
      "[OLD PC] Enable — saves 30-50% VRAM/bandwidth on weak GPUs. "
      "[NEW PC] Can disable for better texture quality.",
      "[СТАРЫЙ ПК] Включить — экономит 30-50% VRAM. "
      "[НОВЫЙ ПК] Можно выключить для лучшего качества текстур.",
      "ON — big win on weak GPUs" },
    { "BoostRender", "noop_finish", OptType::Bool, "true",
      "glFinish() → no-op. cocos2d sometimes calls Finish after frame which "
      "stalls the pipeline. Safe to skip.",
      "glFinish() становится no-op. cocos2d иногда зовёт Finish после "
      "кадра, это блокирует пайплайн. Безопасно.",
      "ON" },
    { "BoostRender", "noop_geterror", OptType::Bool, "true",
      "glGetError() → GL_NO_ERROR. Saves driver round-trip.",
      "glGetError() всегда GL_NO_ERROR. Экономит обращение к драйверу.",
      "ON" },
    { "BoostRender", "vbo_pool", OptType::Bool, "false",
      "[OLD PC] Keep OFF — conflicts with cocos2d batcher, causes jitter. "
      "[NEW PC] Can enable — VBO cache speeds up repeated objects.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — конфликтует с батчером cocos2d, создаёт jitter. "
      "[НОВЫЙ ПК] Можно включить — кэш VBO ускоряет повторные объекты.",
      "OFF — conflicts with cocos2d batcher" },
    { "BoostRender", "vbo_pool_size_mb", OptType::Int, "16",
      "[OLD PC] 16 MB. [NEW PC] 64-128 MB for better caching.",
      "[СТАРЫЙ ПК] 16 MB. [НОВЫЙ ПК] 64-128 MB для лучшего кэша.",
      "OFF — vbo_pool off",
      4, 256 },
    { "BoostRender", "vertex_compress", OptType::Bool, "false",
      "Vertex format compression FP32 → FP16. OFF — risky, can break UV "
      "interpolation on text/UI sprites.",
      "Сжатие vertex-формата FP32 → FP16. ВЫКЛ — рискованно, может "
      "ломать UV на тексте/UI.",
      "OFF — risky" },
    { "BoostRender", "instancing", OptType::Bool, "false",
      "glDrawElementsInstanced batching. OFF — would require rewriting "
      "cocos2d draw calls.",
      "Инстансинг через glDrawElementsInstanced. ВЫКЛ — потребовал бы "
      "переписать cocos2d-draw.",
      "OFF — vertex layout mismatch" },
    { "BoostRender", "dyn_resolution", OptType::Bool, "false",
      "Dynamic resolution scaling on FPS dips. OFF — causes visible jitter, "
      "predictable FPS via frame pacing is better.",
      "Динамическое масштабирование разрешения. ВЫКЛ — даёт jitter, лучше "
      "предсказуемый FPS через пацер.",
      "OFF — visual jitter" },
    { "BoostRender", "dyn_res_target_fps", OptType::Int, "60",
      "Target FPS for dynamic resolution algorithm.",
      "Целевой FPS для алгоритма dynamic resolution.",
      "OFF — dyn_resolution off",
      30, 240 },

    // ────────── [BoostIO] ───────────────────────────────────────────────────
    { "BoostIO", "fast_io", OptType::Bool, "true",
      "Intercepts CreateFile calls to add FILE_FLAG_SEQUENTIAL_SCAN for optimized "
      "sequential asset reading.",
      "Перехватывает вызовы CreateFile и добавляет флаг FILE_FLAG_SEQUENTIAL_SCAN "
      "для оптимизации последовательного чтения ассетов с диска.",
      "ON" },
    { "BoostIO", "ramdisk_cache", OptType::Bool, "false",
      "Copies Resources/ directory contents directly into RAM memory. Requires "
      "approximately 500 MB of free memory.",
      "Копирует все ресурсы (Resources/) в оперативную память. Требует около "
      "500 МБ свободной RAM для размещения кэша.",
      "OFF" },
    { "BoostIO", "ramdisk_path", OptType::String, "",
      "Custom path for ramdisk cache. Empty = auto.",
      "Свой путь для ramdisk-кеша. Пустой = авто.",
      "OFF — ramdisk_cache off" },
    { "BoostIO", "loader_cache", OptType::Bool, "true",
      "Cache GetProcAddress / wglGetProcAddress lookups. Big win — cocos2d "
      "re-resolves the same procs every frame.",
      "Кеш GetProcAddress / wglGetProcAddress. Большой плюс — cocos2d "
      "ре-резолвит одни и те же процы каждый кадр.",
      "ON" },

    // ── v1.0.4: IOCP-based asset prewarmer ──
    { "BoostIO", "iocp_loader", OptType::Bool, "false",
      "Overlapped I/O + Completion Port asset prewarmer. ~2× faster than "
      "the legacy thread-pool reader on NVMe; auto-falls-back to single-"
      "concurrency on rotational disks (no head thrashing). Read-only — "
      "doesn't risk save corruption. Disable if async_asset_loader is on.",
      "Прогрев файлового кэша через IOCP + Overlapped I/O. На NVMe в ~2× "
      "быстрее старого thread-pool. На HDD автоматически концаррент=1 — "
      "никакого head ping-pong. Read-only — сейвы не страдают. Конфликтует "
      "с async_asset_loader (включай что-то одно).",
      "OFF — opt-in" },
    { "BoostIO", "iocp_loader_concurrency", OptType::Int, "0",
      "Number of concurrent IOCP read operations. 0 = auto-detect (1 on HDD, "
      "4 on SSD/NVMe). Manual range: 1..8.",
      "Количество параллельных IOCP-читалок. 0 = автодетект (1 на HDD, "
      "4 на SSD/NVMe). Ручной диапазон: 1..8.",
      "0 = auto",
      0, 8 },

    // ────────── [BoostCPU] ──────────────────────────────────────────────────
    { "BoostCPU", "sse_memcpy", OptType::Bool, "false",
      "SSE2-optimized memcpy via IAT hook. OFF — hook crashes on some "
      "Windows runtimes (CRT version mismatch).",
      "SSE2 memcpy через IAT hook. ВЫКЛ — hook крашится на разных "
      "версиях Windows runtime.",
      "OFF — IAT hook crash risk" },
    { "BoostCPU", "scene_bvh", OptType::Bool, "false",
      "Bounding volume hierarchy for scene culling. OFF — experimental.",
      "BVH-дерево для отсечения сцены. ВЫКЛ — экспериментально.",
      "OFF — experimental" },
    { "BoostCPU", "string_intern", OptType::Bool, "false",
      "std::string interning via IAT hook. OFF — hooking string ctor "
      "crashes due to ABI variations.",
      "String interning через IAT hook на ctor-е std::string. ВЫКЛ — "
      "крашится из-за ABI-вариаций.",
      "OFF — IAT hook crash risk" },
    { "BoostCPU", "mimalloc_full", OptType::Bool, "false",
      "Replace global new/delete with mimalloc. OFF — mimalloc DLL not "
      "bundled with the mod.",
      "Заменяет global new/delete на mimalloc. ВЫКЛ — DLL не идёт в "
      "комплекте с модом.",
      "OFF — DLL not bundled" },
    { "BoostCPU", "silent_debug", OptType::Bool, "true",
      "Silences printf, OutputDebugString and other debug streams from "
      "cocos2d/GD. Small but free perf win.",
      "Глушит printf, OutputDebugString и debug-потоки cocos2d/GD. "
      "Маленький бесплатный буст.",
      "ON" },

    // ────────── [BoostSystem] ───────────────────────────────────────────────
    { "BoostSystem", "wddm_priority", OptType::Bool, "true",
      "D3DKMTSetSchedulingPriorityClass(HIGH) — Windows gives our render "
      "commands priority over other apps.",
      "Приоритет HIGH для GPU-контекста через WDDM. Windows отдаёт наши "
      "draw-вызовы вперёд других программ.",
      "ON" },
    { "BoostSystem", "game_mode", OptType::Bool, "true",
      "Registers GD with Windows Game Mode — disables non-essential "
      "background services.",
      "Регистрирует GD в Windows Game Mode — Windows вырубает фоновые "
      "сервисы.",
      "ON" },
    { "BoostSystem", "smart_cpu_pin", OptType::Bool, "false",
      "Pin main thread to performance cores on hybrid CPUs (Intel 12th gen and newer). "
      "Safe to leave enabled on older non-hybrid processors.",
      "Привязывает главный поток к производительным P-ядрам на гибридных процессорах. "
      "Безопасно оставлять включенным на старых архитектурах.",
      "ON — no-op on legacy CPUs" },
    { "BoostSystem", "mitigation_off", OptType::Bool, "false",
      "Disables Windows process mitigations (CFG, CET, strict ASLR). OFF — "
      "~1-2% gain not worth the security trade-off.",
      "Отключает Windows mitigations. ВЫКЛ — ~1-2% не стоит снижения "
      "безопасности.",
      "OFF — security trade-off" },

    // ────────── [BoostLatency] ──────────────────────────────────────────────
    { "BoostLatency", "allow_tearing", OptType::Bool, "true",
      "DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING. Gated by isFlipModel(swapEffect) — "
      "safely no-ops when ANGLE picks the legacy BLIT swap chain on older "
      "GPUs / drivers.",
      "Флаг ALLOW_TEARING. Гейчён по isFlipModel — безопасно становится "
      "no-op когда ANGLE выбирает legacy BLIT swap chain на старых "
      "GPU/драйверах.",
      "ON — gated by flip-model" },
    { "BoostLatency", "waitable_swap", OptType::Bool, "true",
      "DXGI waitable swap chain — waits on a handle instead of blocking "
      "inside Present(). Gated by backend==d3d11.",
      "DXGI waitable swap chain — ждёт на handle вместо блокировки в "
      "Present(). Гейчён по backend==d3d11.",
      "ON — D3D11 only" },
    { "BoostLatency", "mmcss_pro_audio", OptType::Bool, "true",
      "Registers main thread as MMCSS Pro Audio class — gets 1 ms scheduling "
      "granularity (default is 15.6 ms quantum).",
      "Регистрирует главный поток как MMCSS Pro Audio — получает 1 ms "
      "гранулярность планирования (вместо 15.6 ms).",
      "ON" },
    { "BoostLatency", "shader_warmup", OptType::Bool, "true",
      "Pre-compile common shaders at init. OFF — can crash on shader "
      "compile errors; shader_cache covers warmup anyway.",
      "Предкомпиляция шейдеров при старте. ВЫКЛ — может падать; кеш на "
      "диске покрывает warmup.",
      "OFF — crash-risky" },
    { "BoostLatency", "low_latency", OptType::Bool, "true",
      "IDXGIDevice1::SetMaximumFrameLatency(1) — reduces input lag by "
      "~2 frames. Now hits correct vtable slot 12 (was slot 11 = "
      "GetGPUThreadPriority(INT*) which crashed with 'write to 0x1').",
      "IDXGIDevice1::SetMaximumFrameLatency(1) — снижает input lag на "
      "~2 кадра. Чинено: slot 12 (раньше 11 = GetGPUThreadPriority, "
      "крашило).",
      "ON — verified MaxFrameLatency=1" },
    { "BoostLatency", "anti_stutter", OptType::Bool, "true",
      "Disables Windows process affinity auto-update + EcoQoS thread "
      "throttling. Eliminates 0.5-2 ms hitches from core migration. "
      "Now also sets HIGH_PRIORITY_CLASS + TIME_CRITICAL for main thread.",
      "Отключает автообновление affinity + EcoQoS-троттлинг потока. "
      "Убирает hitch-и 0.5-2 ms. Теперь также ставит HIGH_PRIORITY_CLASS + "
      "TIME_CRITICAL на главный поток.",
      "ON" },

    // ── v1.0.4: DXGI/DWM low-latency present hints ──
    { "BoostLatency", "present_hints", OptType::Bool, "true",
      "DXGI / DWM low-latency present hints. DwmEnableMMCSS lets the "
      "compositor schedule under MMCSS (lower jitter on Win7/8). On "
      "Win10 1607+ also calls IDXGIDevice4::SetMaximumFrameLatency(1) "
      "as a defence-in-depth duplicate of low_latency. Pure perf, no "
      "visual change — safe everywhere.",
      "Хинты DXGI / DWM для уменьшения задержки кадра. DwmEnableMMCSS — "
      "композитор переходит под MMCSS-планировщик (меньше jitter на "
      "Win7/8). На Win10 1607+ дополнительно вызывает "
      "IDXGIDevice4::SetMaximumFrameLatency(1) — дублирует low_latency, "
      "защищая от device-lost. Никаких визуальных изменений.",
      "ON" },

    // ────────── [BoostGD] ───────────────────────────────────────────────────
    { "BoostGD", "skip_intro", OptType::Bool, "false",
      "Skip RobTop intro splash. OFF — file-loading hooks risk save "
      "corruption; intro is short anyway.",
      "Пропуск splash-экрана. ВЫКЛ — file-hook рискован для сейвов, "
      "интро короткое.",
      "OFF — risk to saves" },
    { "BoostGD", "object_pool", OptType::Bool, "false",
      "Object pool for cocos2d nodes. OFF — vtable corruption risk on "
      "CCNode subclasses.",
      "Пул объектов для cocos2d-нод. ВЫКЛ — риск порчи vtable.",
      "OFF — vtable corruption risk" },
    { "BoostGD", "object_pool_size", OptType::Int, "4096",
      "Pool capacity when object_pool=true.",
      "Ёмкость пула когда object_pool=true.",
      "OFF — object_pool off",
      256, 65535 },
    { "BoostGD", "trigger_cache", OptType::Bool, "false",
      "Cache trigger evaluation results. OFF — caches go stale when "
      "triggers move during playback.",
      "Кеш результатов триггеров. ВЫКЛ — устаревает когда триггеры "
      "двигаются.",
      "OFF — stale on moving triggers" },
    { "BoostGD", "plist_binary", OptType::Bool, "false",
      "Binary plist cache. OFF — risk of corrupting save plist files.",
      "Бинарный кеш plist. ВЫКЛ — риск порчи save-файлов.",
      "OFF — save corruption risk" },
    { "BoostGD", "plist_cache_dir", OptType::String, "plist_cache",
      "Folder for binary plist cache.",
      "Папка для бинарного кеша plist.",
      "OFF — plist_binary off" },
    { "BoostGD", "skip_shake_flash", OptType::Bool, "false",
      "Skip screen shake / flash effects. OFF — heuristic hooks are "
      "glitch-prone, can hide effects on legit levels.",
      "Пропуск shake/flash. ВЫКЛ — эвристика глючит, прячет эффекты "
      "на нормальных уровнях.",
      "OFF — visual glitches" },
    { "BoostGD", "level_predecode", OptType::Bool, "false",
      "[OLD PC] Keep OFF — competes with main thread for CPU. "
      "[NEW PC] Enable for faster level loading (4 threads recommended).",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — конкурирует за CPU с главным потоком. "
      "[НОВЫЙ ПК] Включить для быстрой загрузки уровней (4 потока).",
      "OFF" },
    { "BoostGD", "predecode_threads", OptType::Int, "2",
      "[OLD PC] Use 0 (disabled). [NEW PC] Use 4 for parallel loading.",
      "[СТАРЫЙ ПК] 0 (выключено). [НОВЫЙ ПК] 4 для параллельной загрузки.",
      "OFF — level_predecode off",
      0, 8 },

    // ────────── [BoostRenderAdv] ────────────────────────────────────────────
    { "BoostRenderAdv", "atlas_merge", OptType::Bool, "false",
      "Merge texture atlases on the fly. OFF — UV remapping breaks textures "
      "in custom levels.",
      "Слияние атласов на лету. ВЫКЛ — переназначение UV ломает текстуры "
      "в кастомных уровнях.",
      "OFF — breaks UVs" },
    { "BoostRenderAdv", "atlas_size", OptType::Int, "2048",
      "Atlas page size when atlas_merge=true.",
      "Размер страницы атласа для atlas_merge.",
      "OFF — atlas_merge off",
      512, 8192 },
    { "BoostRenderAdv", "frustum_cull", OptType::Bool, "false",
      "Frustum culling on cocos2d nodes. OFF — can hide objects on screen "
      "edges due to incorrect bounding boxes.",
      "Frustum-отсечение нод. ВЫКЛ — может прятать объекты у краёв "
      "экрана из-за неверных bbox-ов.",
      "OFF — clips visible objects" },
    { "BoostRenderAdv", "fbo_cache", OptType::Bool, "false",
      "FBO pool. OFF — complicates render-target switching, jitter source.",
      "Пул FBO. ВЫКЛ — усложняет переключение RT, источник jitter.",
      "OFF — jitter source" },
    { "BoostRenderAdv", "fbo_pool_size", OptType::Int, "8",
      "FBO pool capacity when fbo_cache=true.",
      "Ёмкость пула FBO когда fbo_cache=true.",
      "OFF — fbo_cache off",
      2, 64 },
    { "BoostRenderAdv", "triple_buffer", OptType::Bool, "false",
      "Triple buffering hint. OFF — ANGLE doesn't honor our buffer-count "
      "hint, queued frames just add input lag.",
      "Тройная буферизация. ВЫКЛ — ANGLE игнорит hint, кадры в очереди = "
      "больше input lag.",
      "OFF — ineffective + adds lag" },
    { "BoostRenderAdv", "disable_aa", OptType::Bool, "true",
      "Disable GL_MULTISAMPLE / MSAA. HUGE win on integrated/weak GPUs — "
      "MSAA 2× costs 30-50% performance with no visible benefit at GD's "
      "resolution.",
      "Отключение MSAA. ОЧЕНЬ большой прирост на слабых/интегрированных GPU "
      "— MSAA 2× жрёт 30-50% без заметной разницы в качестве.",
      "ON — big win on weak GPUs" },
    { "BoostRenderAdv", "blend_optimize", OptType::Bool, "false",
      "Optimize blend modes. OFF — breaks fade transitions and additive "
      "blend.",
      "Оптимизация blend-модов. ВЫКЛ — ломает fade и additive blend.",
      "OFF — breaks transparency" },

    // ────────── [BoostCocos] ────────────────────────────────────────────────
    { "BoostCocos", "particle_throttle", OptType::Bool, "false",
      "[OLD PC] Enable — limits particles to ~30 for performance. "
      "[NEW PC] Can disable — GPU handles it fine.",
      "[СТАРЫЙ ПК] Включить — лимит ~30 частиц для производительности. "
      "[НОВЫЙ ПК] Можно выключить — GPU справится.",
      "OFF by default — visual choice" },
    { "BoostCocos", "particle_max", OptType::Int, "300",
      "[OLD PC] Use 30. [NEW PC] Use 0 (unlimited).",
      "[СТАРЫЙ ПК] 30. [НОВЫЙ ПК] 0 (без лимита).",
      "OFF — particle_throttle off",
      0, 5000 },
    { "BoostCocos", "texcache_preload", OptType::Bool, "false",
      "Pre-warm texture cache at startup. OFF — slows first launch by "
      "~3 s, no measurable in-game gain.",
      "Прогрев texture cache при старте. ВЫКЛ — замедляет первый запуск, "
      "в игре прироста нет.",
      "OFF — startup tax" },
    { "BoostCocos", "batch_force", OptType::Bool, "false",
      "Force-batch all sprites. OFF — modifies render state, sprite "
      "glitches.",
      "Принудительный batching спрайтов. ВЫКЛ — даёт глитчи на спрайтах.",
      "OFF — sprite glitches" },
    { "BoostCocos", "label_cache", OptType::Bool, "false",
      "Cache CCLabel-rendered glyphs. OFF — text recalc is cheap on "
      "cocos2d, not worth the complexity.",
      "Кеш отрендеренных глифов CCLabel. ВЫКЛ — пересчёт и так дешёв.",
      "OFF — minimal gain" },
    { "BoostCocos", "scheduler_skip", OptType::Bool, "true",
      "Skip inactive timers in CCScheduler tick. Free perf, completely safe.",
      "Пропуск неактивных таймеров в CCScheduler. Бесплатный буст, "
      "полностью безопасно.",
      "ON" },
    { "BoostCocos", "drawcall_sort", OptType::Bool, "false",
      "Sort draw calls by texture. OFF — breaks z-ordering of layered "
      "sprites in editor.",
      "Сортировка draw-вызовов по текстуре. ВЫКЛ — ломает z-порядок "
      "спрайтов в редакторе.",
      "OFF — breaks z-order" },
    { "BoostCocos", "index_buffer_gen", OptType::Bool, "false",
      "Generate index buffers for batched sprites. OFF — vertex layout "
      "mismatch with cocos2d expectations.",
      "Генерация index-буферов. ВЫКЛ — vertex layout не совпадает с "
      "cocos2d.",
      "OFF — layout mismatch" },

    // ────────── [BoostSysAdv] ───────────────────────────────────────────────
    { "BoostSysAdv", "ftz_daz", OptType::Bool, "false",
      "FTZ/DAZ FPU flags (flush-to-zero / denormals-are-zero). OFF — "
      "changes float behavior, can affect physics timing.",
      "FTZ/DAZ FPU флаги. ВЫКЛ — меняет поведение float, может повлиять "
      "на физику.",
      "OFF — affects physics" },
    { "BoostSysAdv", "spectre_off", OptType::Bool, "false",
      "Disable Spectre/Meltdown CPU mitigations for the process. OFF — "
      "~1-2% gain not worth security trade-off.",
      "Отключение Spectre/Meltdown mitigation. ВЫКЛ — безопасность не "
      "стоит ~1-2% прироста.",
      "OFF — security trade-off" },
    { "BoostSysAdv", "io_priority", OptType::Bool, "true",
      "NtSetInformationProcess(IoPriority=HIGH) — better disk priority "
      "for GD.",
      "NtSetInformationProcess(IoPriority=HIGH) — приоритет диска для GD.",
      "ON" },
    { "BoostSysAdv", "mem_priority", OptType::Bool, "true",
      "ProcessMemoryPriority=5 (highest non-realtime). Reduces page-out "
      "pressure during gameplay.",
      "ProcessMemoryPriority=5 (макс кроме realtime). Снижает выгрузку "
      "страниц во время игры.",
      "ON" },
    { "BoostSysAdv", "stack_trim", OptType::Bool, "false",
      "Trim main thread stack to 256 KB. OFF — cocos2d's CCNode tree "
      "traversal can recurse deeply, stack overflow risk.",
      "Обрезка стека до 256 KB. ВЫКЛ — обход CCNode-дерева может уходить "
      "в глубокую рекурсию, риск переполнения.",
      "OFF — recursion crash risk" },

    // ────────── [BoostPipeline] (all OFF — hot-path jitter) ────────────────
    { "BoostPipeline", "pipe_drawsort", OptType::Bool, "false",
      "Sort draw calls by state. OFF — hot path, jitter risk on slow CPUs.",
      "Сортировка draw-вызовов по состоянию. ВЫКЛ — hot path, риск jitter на слабых CPU.",
      "OFF" },
    { "BoostPipeline", "scissor_tight", OptType::Bool, "false",
      "[OLD PC] Keep OFF — adds CPU overhead, can clip sprites. "
      "[NEW PC] Can enable — tight scissor optimization.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — добавляет CPU overhead, может обрезать спрайты. "
      "[НОВЫЙ ПК] Можно включить — оптимизация tight scissor.",
      "OFF — can clip sprites" },
    { "BoostPipeline", "vertex_dedup", OptType::Bool, "false",
      "[OLD PC] Keep OFF — hot path, jitter source. "
      "[NEW PC] Can enable — dedup identical vertices.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — hot path, источник jitter. "
      "[НОВЫЙ ПК] Можно включить — дедупликация одинаковых вершин.",
      "OFF — hot-path jitter" },
    { "BoostPipeline", "halfres_effects", OptType::Bool, "false",
      "Render post-effects at half resolution. OFF — blurry visuals.",
      "Пост-эффекты в половинном разрешении. ВЫКЛ — мыло на эффектах.",
      "OFF — blurry visuals" },
    { "BoostPipeline", "shader_simplify", OptType::Bool, "false",
      "Patch shaders to simpler variants. OFF — risky shader rewriting.",
      "Патч шейдеров на упрощённые варианты. ВЫКЛ — рискованно.",
      "OFF — risky shader rewriting" },
    { "BoostPipeline", "batch_coalesce", OptType::Bool, "false",
      "Combines small drawing batches together. Intercepting this hot path can "
      "introduce frame pacing instability on low-end CPUs.",
      "Объединяет небольшие батчи отрисовки в более крупные. Перехват этого горячего пути "
      "может вызвать нестабильность времени кадра на слабых процессорах.",
      "OFF" },

    // ── v1.0.4: adaptive effect LOD + static buffer cache ──
    { "BoostPipeline", "effect_lod", OptType::Bool, "true",
      "[ALL PC] Adaptive LOD for particles. When FPS drops, throttles only "
      "mass-particle batches. Player icon, ground, UI are NEVER touched. "
      "[OLD PC] Enable with effect_lod_min_fps=45. "
      "[NEW PC] Enable with effect_lod_min_fps=30.",
      "[ВСЕ ПК] Адаптивный LOD для частиц. Режет только mass-particle батчи. "
      "Иконка, ground, UI — НИКОГДА не трогаются. "
      "[СТАРЫЙ ПК] Включить с effect_lod_min_fps=45. "
      "[НОВЫЙ ПК] Включить с effect_lod_min_fps=30.",
      "ON — никогда не портит картинку" },
    { "BoostPipeline", "effect_lod_min_fps", OptType::Int, "0",
      "[OLD PC] Use 45 (activate earlier). "
      "[NEW PC] Use 30 (more headroom).",
      "[СТАРЫЙ ПК] 45 (активировать раньше). "
      "[НОВЫЙ ПК] 30 (больше запаса).",
      "0 = auto",
      0, 1000 },
    { "BoostPipeline", "static_batch", OptType::Bool, "false",
      "Static Buffer Cache. Hashes glBufferData uploads and skips "
      "identical re-uploads (cocos2d does this for ground / background "
      "every frame). Off by default: extremely rare hash collisions "
      "(< 2^-64 probability per upload) could replay 1 stale frame — "
      "invisible at 60+ FPS, technically possible.",
      "Кэш статичных VBO uploads. Хэширует glBufferData и пропускает "
      "идентичные перезаливки (cocos2d делает это для ground / "
      "background каждый кадр). ВЫКЛ по умолчанию: микроскопический "
      "шанс хэш-коллизии (< 2^-64) может дать 1 устаревший кадр — "
      "глазом не заметно, но теоретически возможно.",
      "OFF — opt-in" },

    // ── v1.0.5: draw call budget ──
    { "BoostPipeline", "drawcall_budget", OptType::Bool, "false",
      "[NEW PC] Guarantees minimum FPS by skipping decorative draw calls "
      "(particles GL_POINTS, small GL_TRIANGLE < 12 verts). "
      "Never skips: player icon, ground, tiles, UI.",
      "[НОВЫЙ ПК] Гарантирует мин. FPS пропуском декоративных draw-call-ов "
      "(GL_POINTS, мелкие GL_TRIANGLE < 12 вершин). "
      "Не пропускает: иконка, ground, tiles, UI.",
      "OFF — NEW PC recommended" },
    { "BoostPipeline", "drawcall_budget_min_fps", OptType::Int, "28",
      "Minimum FPS target for draw call budget. 28 = safe minimum. "
      "Higher = better quality but less headroom.",
      "Мин. целевой FPS. 28 = безопасно для слабых ПК. "
      "Больше = лучше качество, меньше запас.",
      "28 — safe minimum",
      15, 240 },

    // ────────── [BoostNetwork] ──────────────────────────────────────────────
    { "BoostNetwork", "dns_prefetch", OptType::Bool, "true",
      "Pre-resolve GD server DNS at startup — hides 50-200 ms first-load "
      "latency.",
      "Предрезолв DNS серверов GD при старте — скрывает 50-200 ms первого "
      "запроса.",
      "ON" },
    { "BoostNetwork", "http_pool", OptType::Bool, "true",
      "HTTP keep-alive connection pool — reuses sockets between requests.",
      "HTTP keep-alive pool — переиспользует сокеты между запросами.",
      "ON" },
    { "BoostNetwork", "online_block_gameplay", OptType::Bool, "false",
      "Block all network during gameplay. OFF — would break online features "
      "(daily, gauntlets).",
      "Блокировка сети во время геймплея. ВЫКЛ — поломает онлайн "
      "(daily, gauntlets).",
      "OFF — breaks online features" },
    { "BoostNetwork", "server_cache", OptType::Bool, "true",
      "Cache GET responses (server lists, level previews) for 60 s.",
      "Кеш GET-ответов (списки серверов, превью) на 60 секунд.",
      "ON" },
    { "BoostNetwork", "winsock_opt", OptType::Bool, "true",
      "TCP_NODELAY + 64 KB send/recv buffers. Faster on small-packet "
      "network traffic.",
      "TCP_NODELAY + 64 KB буферы. Быстрее на мелких пакетах.",
      "ON" },

    // ────────── [BoostAudio] ────────────────────────────────────────────────
    { "BoostAudio", "fmod_channel_limit", OptType::Bool, "false",
      "[OLD PC] Enable — limit to 16 channels, saves CPU. "
      "[NEW PC] Can disable — full 32 channels for best audio.",
      "[СТАРЫЙ ПК] Включить — лимит 16 каналов, экономит CPU. "
      "[НОВЫЙ ПК] Можно выключить — полные 32 канала для лучшего звука.",
      "OFF — risk muting SFX" },
    { "BoostAudio", "fmod_max_channels", OptType::Int, "16",
      "[OLD PC] 16. [NEW PC] 32.",
      "[СТАРЫЙ ПК] 16. [НОВЫЙ ПК] 32.",
      "OFF — fmod_channel_limit off",
      4, 64 },
    { "BoostAudio", "fmod_software_mix", OptType::Bool, "false",
      "[OLD PC] Keep OFF — keeps GPU audio offload. "
      "[NEW PC] Can enable — better audio quality.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — сохраняет GPU audio offload. "
      "[НОВЫЙ ПК] Можно включить — лучшее качество звука.",
      "OFF — loses GPU audio offload" },
    { "BoostAudio", "audio_thread_pin", OptType::Bool, "false",
      "[OLD PC] Keep OFF — binding can starve main thread. "
      "[NEW PC] Can enable — stable audio on dedicated cores.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — привязка может замедлить главный поток. "
      "[НОВЫЙ ПК] Можно включить — стабильный звук на выделенных ядрах.",
      "OFF" },
    { "BoostAudio", "sound_preload", OptType::Bool, "false",
      "[OLD PC] Keep OFF — uses 500MB+ RAM. "
      "[NEW PC] Can enable — faster sound loading.",
      "[СТАРЫЙ ПК] ВЫКЛЮЧИТЬ — жрёт 500MB+ RAM. "
      "[НОВЫЙ ПК] Можно включить — быстрая загрузка звуков.",
      "OFF" },
    { "BoostAudio", "audio_ram_compress", OptType::Bool, "false",
      "RAM compression of audio buffers. OFF — adds CPU cost, marginal "
      "gain.",
      "RAM-компрессия аудио. ВЫКЛ — CPU-нагрузка, выгода минимальная.",
      "OFF — CPU cost > gain" },

    // ────────── [BoostExtreme] ──────────────────────────────────────────────
    { "BoostExtreme", "etw_disable", OptType::Bool, "true",
      "Disable ETW event tracing for the process. Tiny but free win.",
      "Отключение ETW трассировки. Мелкий, но бесплатный буст.",
      "ON" },
    { "BoostExtreme", "wer_disable", OptType::Bool, "true",
      "Disable Windows Error Reporting for GD — no Watson dialogs or "
      "telemetry uploads on crashes.",
      "Отключение WER для GD — нет Watson-окон и телеметрии при крашах.",
      "ON" },
    { "BoostExtreme", "smartscreen_off", OptType::Bool, "true",
      "Strip Zone.Identifier ADS from our DLLs — bypasses SmartScreen "
      "\"downloaded from internet\" prompt.",
      "Удаляет Zone.Identifier ADS — обходит SmartScreen \"скачано из "
      "интернета\".",
      "ON" },
    { "BoostExtreme", "numa_aware", OptType::Bool, "true",
      "NUMA-aware memory allocation. Optimizes multi-socket or multi-die CPU memory routing. "
      "Safe to keep enabled on standard systems.",
      "Выделение памяти с учетом архитектуры NUMA. Оптимизирует работу с памятью "
      "на многопроцессорных системах.",
      "ON — no-op on single-NUMA" },
    { "BoostExtreme", "huge_pages", OptType::Bool, "false",
      "Use 2 MB large pages. OFF — needs SeLockMemoryPrivilege (admin).",
      "2 MB large pages. ВЫКЛ — требует SeLockMemoryPrivilege (admin).",
      "OFF — admin required" },
    { "BoostExtreme", "prefetcher_off", OptType::Bool, "false",
      "Disable Windows Superfetch/Prefetcher for GD. OFF — needs admin.",
      "Отключение Superfetch/Prefetcher для GD. ВЫКЛ — нужен admin.",
      "OFF — admin required" },
    { "BoostExtreme", "workingset_lock", OptType::Bool, "true",
      "Hard-pin minimum working set (SetProcessWorkingSetSizeEx + "
      "QUOTA_LIMITS_HARDWS_MIN_ENABLE). Stops Windows from paging our hot "
      "pages out under RAM pressure (browser/IDE/Defender scan). Falls "
      "back to a soft hint if SE_INC_WORKING_SET_NAME privilege denied.",
      "Хард-фиксация минимального working set — Windows не сможет выгрузить "
      "наши горячие страницы при нехватке RAM (браузер/IDE/Defender). Если "
      "привилегия SE_INC_WORKING_SET_NAME недоступна — мягкий hint.",
      "ON — anti-stutter on RAM-tight systems" },
    { "BoostExtreme", "gpu_thread_prio", OptType::Bool, "true",
      "IDXGIDevice::SetGPUThreadPriority(+7). Bumps the user-mode driver's "
      "GPU command-list submission thread priority. Default is 0; range "
      "[-7..+7]. Helps win against DWM compositor / browser GPU process. "
      "Auto-clamps to +5 / +2 if elevation needed; harmless on any case.",
      "Приоритет GPU thread в user-mode драйвере = +7 (макс). Помогает "
      "выиграть конкуренцию с DWM-композитором / GPU-процессом браузера. "
      "При нехватке прав авто-clamp до +5 / +2.",
      "ON — boosts GPU command submission" },

    // ===== (opt-in pipeline) =====
    { "BoostExtreme", "present_skip_idle", OptType::Bool, "false",
      "Skip eglSwapBuffers when the frame had zero draw calls "
      "(idle menus, paused level, dialog open). Capped at 4 consecutive "
      "skips so input latency stays bounded. Saves GPU power on idle "
      "scenes — fan stops ramping up while menus are open.",
      "Пропуск eglSwapBuffers на кадрах без draw-call'ов (главное меню, "
      "пауза, диалог). Не больше 4 подряд — чтобы input lag оставался "
      "ограниченным. Экономит мощность GPU на идле — куллер не разгоняется "
      "пока открыто меню.",
      "OFF" },

    { "BoostExtreme", "halfres_render", OptType::Bool, "false",
      "Render the entire game at half resolution (W/2 × H/2) into an "
      "offscreen FBO, then upscale to the real backbuffer on Present. "
      "Trades visible quality (UI text becomes blurrier/pixelated) for "
      "~30-50% less fragment shading work. Big win on fillrate-bound GPUs.",
      "Рендер всей игры в половинном разрешении (W/2 × H/2) в offscreen "
      "FBO, апскейл до реального backbuffer на Present. Жертвуешь качеством "
      "(UI текст блюрнее/пиксельнее) ради ~30-50% меньше работы по фрагмент-шейдингу. "
      "Огромный выигрыш на слабых видеокартах с низкой скоростью заполнения.",
      "OFF — opt-in (visible quality drop)" },

    { "BoostExtreme", "halfres_filter", OptType::Enum, "nearest",
      "Scaling filter used when upscaling the half-resolution FBO to the screen. "
      "nearest provides sharp pixel art/text with no blur (highly recommended); "
      "linear provides standard bilinear filtering which blurs the image slightly.",
      "Фильтр масштабирования при апскейле кадра 1/2 разрешения. "
      "nearest дает четкие пиксели без мыла (рекомендуется); "
      "linear сглаживает картинку, делая ее немного размытой.",
      "nearest", 0, 0, "nearest,linear" },

    // ── v1.0.4: per-thread EcoQoS opt-out for ALL threads ──
    { "BoostExtreme", "thread_qos", OptType::Bool, "true",
      "Process-wide thread QoS hardening. Walks every existing thread "
      "of GD via Toolhelp32 snapshot and explicitly disables Windows "
      "EcoQoS / Power Throttling. Fixes audio dropouts and FPS jitter "
      "on Win11 laptops where audio mixer / asset loader threads got "
      "auto-classified as 'background' and down-clocked. Costs ~5-10 % "
      "more battery on idle, none in gameplay.",
      "Процесс-широкое выключение EcoQoS для КАЖДОГО потока. Проходит "
      "по snapshot-у потоков GD через Toolhelp32 и явно отключает "
      "Windows EcoQoS / Power Throttling на каждом. Чинит проседания "
      "звука и FPS-jitter на ноутбуках Win11, где audio-mixer и asset-"
      "loader потоки ошибочно классифицировались как 'background' и "
      "тротлились. На батарее +5-10 % к расходу в idle, в игре — 0.",
      "ON" },

};

const std::vector<OptionDef>& schemaAll() {
    static const std::vector<OptionDef> v(std::begin(g_opts), std::end(g_opts));
    return v;
}

const std::vector<std::string>& schemaSections() {
    static const std::vector<std::string> v = {
        "ANGLE",
        "Boost",
        "BoostAdvanced",
        "BoostRender",
        "BoostIO",
        "BoostCPU",
        "BoostSystem",
        "BoostLatency",
        "BoostGD",
        "BoostRenderAdv",
        "BoostCocos",
        "BoostSysAdv",
        "BoostPipeline",
        "BoostNetwork",
        "BoostAudio",
        "BoostExtreme",
    };
    return v;
}

const char* getFriendlySection(const char* section, bool isRussian) {
    std::string s = section;
    if (isRussian) {
        if (s == "ANGLE") return "Базовые настройки";
        if (s == "Boost") return "Основные ускорения";
        if (s == "BoostAdvanced") return "Сложные твики";
        if (s == "BoostRender") return "Настройки графики";
        if (s == "BoostIO") return "Накопитель и ОЗУ";
        if (s == "BoostCPU") return "Ядра процессора";
        if (s == "BoostSystem") return "Приоритеты ОС";
        if (s == "BoostLatency") return "Задержка и FPS";
        if (s == "BoostGD") return "Ускорение игры (GD)";
        if (s == "BoostRenderAdv") return "Сложный рендер";
        if (s == "BoostCocos") return "Твики Cocos2d-x";
        if (s == "BoostSysAdv") return "Твики Windows";
        if (s == "BoostPipeline") return "Конвейер кадров";
        if (s == "BoostNetwork") return "Сеть и сокеты";
        if (s == "BoostAudio") return "Звуковая подсистема";
        if (s == "BoostExtreme") return "Экстремальный буст";
    } else {
        if (s == "ANGLE") return "Core Settings";
        if (s == "Boost") return "Basic Boosts";
        if (s == "BoostAdvanced") return "Advanced Tweaks";
        if (s == "BoostRender") return "Graphics & Rendering";
        if (s == "BoostIO") return "I/O & Disk Buffers";
        if (s == "BoostCPU") return "CPU & Core Affinity";
        if (s == "BoostSystem") return "OS & Power Priority";
        if (s == "BoostLatency") return "Latency & Target FPS";
        if (s == "BoostGD") return "Geometry Dash Tweaks";
        if (s == "BoostRenderAdv") return "Adv Graphics Pipeline";
        if (s == "BoostCocos") return "Cocos2d-x Tweaks";
        if (s == "BoostSysAdv") return "Adv Windows Priority";
        if (s == "BoostPipeline") return "Draw Pipeline Optimizations";
        if (s == "BoostNetwork") return "Network & Sockets";
        if (s == "BoostAudio") return "Audio & FMOD Engine";
        if (s == "BoostExtreme") return "Extreme & Risky Boosts";
    }
    return section;
}

const char* getFriendlyName(const char* section, const char* key, bool isRussian) {
    std::string k = key;
    if (isRussian) {
        if (k == "enabled") return "Включить мод";
        if (k == "backend") return "Рендерер (DirectX/OpenGL)";
        if (k == "debug") return "Отладочный лог";
        if (k == "log_file") return "Файл лога";
        if (k == "frame_pacing") return "Управление кадрами (Frame Pacing)";
        if (k == "frame_pacing_target") return "Лимит кадров (FPS)";
        if (k == "force_no_vsync") return "Принудительно выключить VSync";
        if (k == "gpu_forcer") return "Принудительно дискретная видеокарта";
        if (k == "fast_allocator") return "Быстрый аллокатор памяти";
        if (k == "timer_fix") return "Таймер Windows (1ms точность)";
        if (k == "thread_boost") return "Повышенный приоритет потока";
        if (k == "cpu_affinity") return "Маска ядер CPU (Affinity)";
        if (k == "sse_math") return "SSE2 инструкции";
        if (k == "power_boost") return "Отключить троттлинг питания (EcoQoS)";
        if (k == "tex_compress") return "Сжатие текстур в DXT1";
        if (k == "nvapi_profile") return "Профиль драйвера NVAPI";
        if (k == "shader_cache") return "Кеширование шейдеров";
        if (k == "shader_cache_dir") return "Папка для кеша шейдеров";
        if (k == "large_address_aware") return "Доступ к 4 ГБ памяти (LAA)";
        if (k == "gl_state_dedup") return "Дедупликация вызовов OpenGL";
        if (k == "working_set_prefetch") return "Предзагрузка рабочих страниц";
        if (k == "fmod_tuning") return "Тюнинг звука FMOD";
        if (k == "fmod_sample_rate") return "Частота дискретизации FMOD";
        if (k == "async_asset_loader") return "Асинхронная загрузка ассетов";
        if (k == "async_loader_threads") return "Потоки загрузки ассетов";
        if (k == "precise_sleep") return "Точный Sleep (Spin-wait)";
        if (k == "heap_compact_interval") return "Интервал сжатия кучи";
        if (k == "d3d11_multithread") return "Многопоточный рендер D3D11";
        if (k == "depth_off") return "Отключить буфер глубины";
        if (k == "mipmap_off") return "Отключить генерацию мипмапов";
        if (k == "noop_finish") return "Игнорировать glFinish";
        if (k == "noop_geterror") return "Игнорировать glGetError";
        if (k == "vbo_pool") return "Пул буферов VBO";
        if (k == "vbo_pool_size_mb") return "Размер пула VBO (МБ)";
        if (k == "vertex_compress") return "Сжатие вершинных данных";
        if (k == "instancing") return "Использовать инстансинг";
        if (k == "dyn_resolution") return "Динамическое разрешение";
        if (k == "dyn_res_target_fps") return "Целевой FPS разрешения";
        if (k == "fast_io") return "Быстрое чтение диска";
        if (k == "ramdisk_cache") return "Кеширование в оперативную память";
        if (k == "ramdisk_path") return "Путь к RAM-диску";
        if (k == "loader_cache") return "Кеширование GetProcAddress";
        if (k == "sse_memcpy") return "Оптимизация memcpy через SSE2";
        if (k == "scene_bvh") return "Ускорение отрисовки сцены (BVH)";
        if (k == "string_intern") return "Кеширование строк (Interning)";
        if (k == "mimalloc_full") return "Использовать аллокатор mimalloc";
        if (k == "silent_debug") return "Скрыть отладочный вывод";
        if (k == "wddm_priority") return "Приоритет планировщика WDDM";
        if (k == "game_mode") return "Поддержка Windows Game Mode";
        if (k == "smart_cpu_pin") return "Умный пиннинг потоков CPU";
        if (k == "mitigation_off") return "Отключить защиты Windows";
        if (k == "allow_tearing") return "Разрешить разрывы кадров (Tearing)";
        if (k == "waitable_swap") return "Ожидаемый буфер обмена DXGI";
        if (k == "mmcss_pro_audio") return "MMCSS приоритет (Pro Audio)";
        if (k == "shader_warmup") return "Предкомпиляция шейдеров";
        if (k == "low_latency") return "Минимальный буфер кадров DXGI";
        if (k == "anti_stutter") return "Защита от фризов (Affinity)";
        if (k == "skip_intro") return "Пропускать интро";
        if (k == "object_pool") return "Пул объектов Cocos2d";
        if (k == "object_pool_size") return "Размер пула объектов";
        if (k == "trigger_cache") return "Кеширование триггеров";
        if (k == "plist_binary") return "Бинарный кеш plist-файлов";
        if (k == "plist_cache_dir") return "Папка для кеша plist-файлов";
        if (k == "skip_shake_flash") return "Отключить тряску экрана";
        if (k == "level_predecode") return "Предварительный декод уровней";
        if (k == "predecode_threads") return "Потоки декодирования";
        if (k == "atlas_merge") return "Слияние текстурных атласов";
        if (k == "atlas_size") return "Размер страницы атласа";
        if (k == "frustum_cull") return "Отсечение невидимых объектов";
        if (k == "fbo_cache") return "Пул буферов FBO";
        if (k == "fbo_pool_size") return "Ёмкость пула FBO";
        if (k == "triple_buffer") return "Тройная буферизация";
        if (k == "disable_aa") return "Отключить сглаживание (MSAA)";
        if (k == "blend_optimize") return "Оптимизация смешивания цветов";
        if (k == "particle_throttle") return "Ограничить количество частиц";
        if (k == "particle_max") return "Лимит количества частиц";
        if (k == "texcache_preload") return "Предзагрузка текстур";
        if (k == "batch_force") return "Принудительный батчинг";
        if (k == "label_cache") return "Кеширование CCLabel";
        if (k == "scheduler_skip") return "Пропуск неактивных таймеров";
        if (k == "drawcall_sort") return "Сортировка по текстуре";
        if (k == "index_buffer_gen") return "Генерация индексных буферов";
        if (k == "ftz_daz") return "FTZ/DAZ режимы процессора";
        if (k == "spectre_off") return "Отключить защиты Spectre";
        if (k == "io_priority") return "Приоритет ввода-вывода диска";
        if (k == "mem_priority") return "Приоритет использования памяти";
        if (k == "stack_trim") return "Ограничить стек потока";
        if (k == "pipe_drawsort") return "Сортировка отрисовки пайплайна";
        if (k == "scissor_tight") return "Более точный отсев scissor";
        if (k == "vertex_dedup") return "Дедупликация вершин";
        if (k == "halfres_effects") return "Пост-эффекты в 1/2 разрешении";
        if (k == "shader_simplify") return "Упрощение кода шейдеров";
        if (k == "batch_coalesce") return "Объединение мелких батчей";
        if (k == "dns_prefetch") return "Предварительный резолв DNS";
        if (k == "http_pool") return "Пул сетевых соединений HTTP";
        if (k == "online_block_gameplay") return "Блокировать сеть во время игры";
        if (k == "server_cache") return "Кеширование ответов сервера";
        if (k == "winsock_opt") return "Оптимизация сокетов (Winsock)";
        if (k == "fmod_channel_limit") return "Ограничить звуковые каналы";
        if (k == "fmod_max_channels") return "Лимит звуковых каналов";
        if (k == "fmod_software_mix") return "Программное микширование";
        if (k == "audio_thread_pin") return "Выделить звуковой поток на ядро";
        if (k == "sound_preload") return "Предзагрузка звуковых файлов";
        if (k == "audio_ram_compress") return "Сжатие звуков в ОЗУ";
        if (k == "etw_disable") return "Отключить трассировку ETW";
        if (k == "wer_disable") return "Отключить отчеты об ошибках";
        if (k == "smartscreen_off") return "Обход проверок SmartScreen";
        if (k == "numa_aware") return "Учет архитектуры NUMA";
        if (k == "huge_pages") return "Использовать Huge Pages";
        if (k == "prefetcher_off") return "Отключить Superfetch/Prefetcher";
        if (k == "workingset_lock") return "Заблокировать рабочее множество";
        if (k == "gpu_thread_prio") return "Приоритет потока отправки GPU";
        if (k == "present_skip_idle") return "Пропускать отрисовку при простое";
        if (k == "halfres_render") return "Рендер в 1/2 разрешения с апскейлом";
        if (k == "halfres_filter") return "Фильтрация апскейла 1/2 разрешения";
        // ── v1.0.4 ──
        if (k == "effect_lod") return "Адаптивный LOD эффектов";
        if (k == "effect_lod_min_fps") return "Целевой FPS для LOD эффектов";
        if (k == "static_batch") return "Кэш статичных VBO uploads";
        if (k == "iocp_loader") return "IOCP-прогрев ассетов (NVMe)";
        if (k == "iocp_loader_concurrency") return "Параллельность IOCP";
        if (k == "present_hints") return "DXGI/DWM хинты задержки";
        if (k == "thread_qos") return "EcoQoS off для всех потоков";
    } else {
        if (k == "enabled") return "Enable Mod";
        if (k == "backend") return "Renderer (DirectX/OpenGL)";
        if (k == "debug") return "Debug Log";
        if (k == "log_file") return "Log File Path";
        if (k == "frame_pacing") return "Frame Pacing (Smoothness)";
        if (k == "frame_pacing_target") return "Target FPS Cap";
        if (k == "force_no_vsync") return "Force Disable VSync";
        if (k == "gpu_forcer") return "Force Discrete GPU";
        if (k == "fast_allocator") return "Fast Memory Allocator";
        if (k == "timer_fix") return "Windows Timer Resolution (1ms)";
        if (k == "thread_boost") return "Thread Priority Boost";
        if (k == "cpu_affinity") return "CPU Affinity Mask";
        if (k == "sse_math") return "SSE2 Math Intrinsics";
        if (k == "power_boost") return "Disable Power Throttling";
        if (k == "tex_compress") return "Texture Compression DXT1";
        if (k == "nvapi_profile") return "NVIDIA API Driver Profile";
        if (k == "shader_cache") return "Disk Shader Cache";
        if (k == "shader_cache_dir") return "Shader Cache Directory";
        if (k == "large_address_aware") return "4GB Memory Patch (LAA)";
        if (k == "gl_state_dedup") return "OpenGL State De-duplication";
        if (k == "working_set_prefetch") return "Working Set Prefetch";
        if (k == "fmod_tuning") return "FMOD Audio Engine Tuning";
        if (k == "fmod_sample_rate") return "FMOD Sample Rate";
        if (k == "async_asset_loader") return "Asynchronous Asset Loading";
        if (k == "async_loader_threads") return "Asset Loader Threads";
        if (k == "precise_sleep") return "Precise Spin-Wait Sleep";
        if (k == "heap_compact_interval") return "Heap Compacting Interval";
        if (k == "d3d11_multithread") return "D3D11 Multithreading Guard";
        if (k == "depth_off") return "Disable Depth Testing";
        if (k == "mipmap_off") return "Skip Mipmap Generation";
        if (k == "noop_finish") return "No-op glFinish() Calls";
        if (k == "noop_geterror") return "No-op glGetError() Calls";
        if (k == "vbo_pool") return "VBO Buffer Pool";
        if (k == "vbo_pool_size_mb") return "VBO Pool Size (MB)";
        if (k == "vertex_compress") return "Vertex Float Compression";
        if (k == "instancing") return "Instanced Drawing";
        if (k == "dyn_resolution") return "Dynamic Resolution Scaling";
        if (k == "dyn_res_target_fps") return "Dynamic Resolution Target FPS";
        if (k == "fast_io") return "Fast Disk I/O Flags";
        if (k == "ramdisk_cache") return "RAM-Disk Cache";
        if (k == "ramdisk_path") return "RAM-Disk Directory Path";
        if (k == "loader_cache") return "GetProcAddress Cache";
        if (k == "sse_memcpy") return "SSE2 Optimized Memcpy";
        if (k == "scene_bvh") return "BVH Scene Frustum Culling";
        if (k == "string_intern") return "String Interning Optimization";
        if (k == "mimalloc_full") return "Use mimalloc Allocator";
        if (k == "silent_debug") return "Silence Cocos Debug Output";
        if (k == "wddm_priority") return "WDDM Scheduling Priority";
        if (k == "game_mode") return "Windows Game Mode Support";
        if (k == "smart_cpu_pin") return "Smart CPU Thread Pinning";
        if (k == "mitigation_off") return "Disable Security Mitigations";
        if (k == "allow_tearing") return "Allow Screen Tearing";
        if (k == "waitable_swap") return "DXGI Waitable Swap Chain";
        if (k == "mmcss_pro_audio") return "MMCSS Pro Audio Priority";
        if (k == "shader_warmup") return "Pre-compile Common Shaders";
        if (k == "low_latency") return "DXGI Max Frame Latency (1)";
        if (k == "anti_stutter") return "Anti-Stutter Threading Fixes";
        if (k == "skip_intro") return "Skip Video Intro";
        if (k == "object_pool") return "Cocos2d Node Object Pool";
        if (k == "object_pool_size") return "Object Pool Max Size";
        if (k == "trigger_cache") return "Cache Trigger Evaluation";
        if (k == "plist_binary") return "Binary Plist File Cache";
        if (k == "plist_cache_dir") return "Plist Cache Directory";
        if (k == "skip_shake_flash") return "Disable Screen Shake/Flash";
        if (k == "level_predecode") return "Pre-decode Levels";
        if (k == "predecode_threads") return "Pre-decode Threads";
        if (k == "atlas_merge") return "On-the-fly Atlas Merging";
        if (k == "atlas_size") return "Texture Atlas Page Size";
        if (k == "frustum_cull") return "Frustum Node Culling";
        if (k == "fbo_cache") return "FBO Buffer Pool";
        if (k == "fbo_pool_size") return "FBO Pool Capacity";
        if (k == "triple_buffer") return "Triple Buffering";
        if (k == "disable_aa") return "Disable Anti-Aliasing (AA)";
        if (k == "blend_optimize") return "Optimize Blend Calculations";
        if (k == "particle_throttle") return "Limit Active Particles";
        if (k == "particle_max") return "Max Particles Limit";
        if (k == "texcache_preload") return "Pre-warm Texture Cache";
        if (k == "batch_force") return "Force Batch Draw Calls";
        if (k == "label_cache") return "CCLabel Glyphs Cache";
        if (k == "scheduler_skip") return "Skip Inactive CCScheduler";
        if (k == "drawcall_sort") return "Sort Draw Calls by Texture";
        if (k == "index_buffer_gen") return "Generate Index Buffers";
        if (k == "ftz_daz") return "FPU Flush-to-Zero (FTZ/DAZ)";
        if (k == "spectre_off") return "Disable Spectre Protections";
        if (k == "io_priority") return "I/O Scheduling Priority";
        if (k == "mem_priority") return "Memory Priority Boost";
        if (k == "stack_trim") return "Trim Main Thread Stack";
        if (k == "pipe_drawsort") return "Pipeline Draw Sorting";
        if (k == "scissor_tight") return "Tighter Scissor Clipping";
        if (k == "vertex_dedup") return "Deduplicate Vertices";
        if (k == "halfres_effects") return "Half-Resolution Post Effects";
        if (k == "shader_simplify") return "Use Simpler Shaders";
        if (k == "batch_coalesce") return "Coalesce Small Draw Batches";
        if (k == "dns_prefetch") return "Pre-resolve Server DNS";
        if (k == "http_pool") return "HTTP Connection Pooling";
        if (k == "online_block_gameplay") return "Block Network in Levels";
        if (k == "server_cache") return "Cache Server GET Responses";
        if (k == "winsock_opt") return "Winsock Performance Optimizations";
        if (k == "fmod_channel_limit") return "Limit FMOD Audio Channels";
        if (k == "fmod_max_channels") return "Max Sound Channels Limit";
        if (k == "fmod_software_mix") return "FMOD Software Mixing";
        if (k == "audio_thread_pin") return "Pin Audio Thread to Core";
        if (k == "sound_preload") return "Preload Audio Buffers";
        if (k == "audio_ram_compress") return "Audio Buffer Compression";
        if (k == "etw_disable") return "Disable Windows ETW Tracing";
        if (k == "wer_disable") return "Disable Error Reporting (WER)";
        if (k == "smartscreen_off") return "Bypass SmartScreen Prompts";
        if (k == "numa_aware") return "NUMA-Aware Memory Alloc";
        if (k == "huge_pages") return "Use 2MB Huge Pages";
        if (k == "prefetcher_off") return "Disable Superfetch/Prefetcher";
        if (k == "workingset_lock") return "Lock Process Working Set";
        if (k == "gpu_thread_prio") return "GPU Command Thread Priority";
        if (k == "present_skip_idle") return "Skip Present on Idle Scenes";
        if (k == "halfres_render") return "Half-Resolution Upscaling (3D)";
        if (k == "halfres_filter") return "Half-Resolution Upscaling Filter";
        // ── v1.0.4 ──
        if (k == "effect_lod") return "Adaptive Effect LOD";
        if (k == "effect_lod_min_fps") return "Effect LOD Target FPS";
        if (k == "static_batch") return "Static Buffer Upload Cache";
        if (k == "iocp_loader") return "IOCP Asset Prewarmer (NVMe)";
        if (k == "iocp_loader_concurrency") return "IOCP Concurrency";
        if (k == "present_hints") return "DXGI/DWM Latency Hints";
        if (k == "thread_qos") return "Per-Thread EcoQoS Off";
    }
    return key;
}
