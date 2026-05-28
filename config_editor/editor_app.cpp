#include "editor_app.hpp"
#include "schema.hpp"
#include "ini_parser.hpp"
#include "imgui.h"

#include <ctime>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>

// The Ini lives as a process-global so the EditorApp methods don't have to
// pass it around — this is a small single-window app, no concurrency.
static Ini g_ini;

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

static std::string nowHHMMSS() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    return buf;
}

// Status-tag color: parses the first non-bracket char (✓, O for ON, OFF, ...).
static ImVec4 statusColor(const char* status) {
    if (!status || !*status) return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    // Scan for ON / OFF tokens.
    if (std::strncmp(status, "ON", 2) == 0) return ImVec4(0.30f, 0.85f, 0.40f, 1.0f);
    if (std::strncmp(status, "OFF", 3) == 0) return ImVec4(0.85f, 0.40f, 0.30f, 1.0f);
    return ImVec4(0.85f, 0.75f, 0.30f, 1.0f);
}

static bool parseBoolStr(const std::string& v) {
    std::string s;
    for (char c : v) s.push_back((char)std::tolower((unsigned char)c));
    return s == "true" || s == "1" || s == "yes" || s == "on";
}

// Find the schema entry for a given section/key. Returns -1 if not in schema.
static int findOptIdx(const std::string& section, const std::string& key) {
    const auto& all = schemaAll();
    for (int i = 0; i < (int)all.size(); ++i) {
        if (all[i].section == section && all[i].key == key) return i;
    }
    return -1;
}

// Filter logic: matches search query across key, section, friendly names and descriptions.
static bool matchOption(const OptionDef& o, const std::string& search, bool isRu) {
    if (search.empty()) return true;
    auto contains = [&](const char* s) {
        if (!s) return false;
        std::string l;
        for (const char* p = s; *p; ++p) l.push_back((char)std::tolower((unsigned char)*p));
        std::string q;
        for (char c : search) q.push_back((char)std::tolower((unsigned char)c));
        return l.find(q) != std::string::npos;
    };
    const char* fName = getFriendlyName(o.section, o.key, isRu);
    const char* fSec = getFriendlySection(o.section, isRu);
    return contains(o.key) ||
           contains(o.section) ||
           contains(fName) ||
           contains(fSec) ||
           (o.desc_ru && contains(o.desc_ru)) ||
           (o.desc_en && contains(o.desc_en));
}


// ────────────────────────────────────────────────────────────────────────────
// EditorApp
// ────────────────────────────────────────────────────────────────────────────

bool EditorApp::init(const std::string& iniPath) {
    m_iniPath = iniPath;
    g_ini.load(iniPath);

    // Migrate old keys to [ANGLE]
    if (g_ini.has("BoostLatency", "frame_pacing") && !g_ini.has("ANGLE", "frame_pacing")) {
        g_ini.set("ANGLE", "frame_pacing", g_ini.get("BoostLatency", "frame_pacing"));
        g_ini.remove("BoostLatency", "frame_pacing");
    }
    if (g_ini.has("BoostLatency", "frame_pacing_target") && !g_ini.has("ANGLE", "frame_pacing_target")) {
        g_ini.set("ANGLE", "frame_pacing_target", g_ini.get("BoostLatency", "frame_pacing_target"));
        g_ini.remove("BoostLatency", "frame_pacing_target");
    }
    if (g_ini.has("BoostAdvanced", "force_no_vsync") && !g_ini.has("ANGLE", "force_no_vsync")) {
        g_ini.set("ANGLE", "force_no_vsync", g_ini.get("BoostAdvanced", "force_no_vsync"));
        g_ini.remove("BoostAdvanced", "force_no_vsync");
    }

    // Load or initialize language
    if (g_ini.has("ANGLE", "language")) {
        m_language = g_ini.get("ANGLE", "language");
    } else {
        m_language = "en";
    }

    // Populate defaults from schema for any missing options
    for (const auto& o : schemaAll()) {
        if (!g_ini.has(o.section, o.key))
            g_ini.set(o.section, o.key, o.default_value);
    }
    return true;
}

void EditorApp::doSave() {
    g_ini.save(m_iniPath);
    m_dirty   = false;
    m_savedAt = "saved " + nowHHMMSS();
}

void EditorApp::doReload() {
    g_ini = Ini{};
    g_ini.load(m_iniPath);
    m_dirty   = false;
    m_savedAt = "reloaded " + nowHHMMSS();
}

void EditorApp::doResetDefaults() {
    for (const auto& o : schemaAll()) {
        g_ini.set(o.section, o.key, o.default_value);
    }
    m_dirty   = true;
    m_savedAt = "defaults loaded — press Save to persist";
}


void EditorApp::applyOldPcPreset() {
    // ── STABLE / LOW-END preset: safe on all PCs, prioritises stability ────
    bool isRu = (m_language == "ru");

    g_ini.set("ANGLE", "backend", "d3d11");
    g_ini.set("ANGLE", "unlock_fps_cap", "false");
    g_ini.set("ANGLE", "force_no_vsync", "false");
    g_ini.set("ANGLE", "frame_pacing", "true");
    g_ini.set("ANGLE", "frame_pacing_target", "0");

    g_ini.set("Boost", "enabled", "true");
    g_ini.set("Boost", "gpu_forcer", "true");
    g_ini.set("Boost", "fast_allocator", "true");
    g_ini.set("Boost", "timer_fix", "true");
    g_ini.set("Boost", "thread_boost", "true");
    g_ini.set("Boost", "cpu_affinity", "0");
    g_ini.set("Boost", "sse_math", "true");
    g_ini.set("Boost", "power_boost", "true");

    g_ini.set("BoostAdvanced", "tex_compress", "false");
    g_ini.set("BoostAdvanced", "nvapi_profile", "true");
    g_ini.set("BoostAdvanced", "shader_cache", "true");
    g_ini.set("BoostAdvanced", "shader_cache_dir", "shader_cache");
    g_ini.set("BoostAdvanced", "large_address_aware", "true");
    g_ini.set("BoostAdvanced", "gl_state_dedup", "true");
    g_ini.set("BoostAdvanced", "working_set_prefetch", "false");
    g_ini.set("BoostAdvanced", "fmod_tuning", "false");
    g_ini.set("BoostAdvanced", "async_asset_loader", "false");
    g_ini.set("BoostAdvanced", "async_loader_threads", "2");
    g_ini.set("BoostAdvanced", "precise_sleep", "true");
    g_ini.set("BoostAdvanced", "heap_compact_interval", "0");
    g_ini.set("BoostAdvanced", "d3d11_multithread", "false");

    g_ini.set("BoostRender", "depth_off", "false");
    g_ini.set("BoostRender", "mipmap_off", "false");
    g_ini.set("BoostRender", "noop_finish", "true");
    g_ini.set("BoostRender", "noop_geterror", "true");
    g_ini.set("BoostRender", "vbo_pool", "false");
    g_ini.set("BoostRender", "vbo_pool_size_mb", "16");
    g_ini.set("BoostRender", "vertex_compress", "false");
    g_ini.set("BoostRender", "instancing", "false");
    g_ini.set("BoostRender", "dyn_resolution", "false");
    g_ini.set("BoostRender", "dyn_res_target_fps", "60");

    g_ini.set("BoostIO", "fast_io", "true");
    g_ini.set("BoostIO", "ramdisk_cache", "false");
    g_ini.set("BoostIO", "ramdisk_path", "");
    g_ini.set("BoostIO", "loader_cache", "true");
    g_ini.set("BoostIO", "iocp_loader", "false");
    g_ini.set("BoostIO", "iocp_loader_concurrency", "0");

    g_ini.set("BoostCPU", "sse_memcpy", "false");
    g_ini.set("BoostCPU", "scene_bvh", "false");
    g_ini.set("BoostCPU", "string_intern", "false");
    g_ini.set("BoostCPU", "mimalloc_full", "false");
    g_ini.set("BoostCPU", "silent_debug", "true");

    g_ini.set("BoostSystem", "wddm_priority", "true");
    g_ini.set("BoostSystem", "game_mode", "true");
    g_ini.set("BoostSystem", "smart_cpu_pin", "false");
    g_ini.set("BoostSystem", "mitigation_off", "false");

    g_ini.set("BoostLatency", "allow_tearing", "false");
    g_ini.set("BoostLatency", "waitable_swap", "true");
    g_ini.set("BoostLatency", "frame_pacing", "true");
    g_ini.set("BoostLatency", "frame_pacing_target", "0");
    g_ini.set("BoostLatency", "mmcss_pro_audio", "true");
    g_ini.set("BoostLatency", "shader_warmup", "true");
    g_ini.set("BoostLatency", "low_latency", "true");
    g_ini.set("BoostLatency", "anti_stutter", "true");
    g_ini.set("BoostLatency", "present_hints", "true");

    g_ini.set("BoostGD", "level_predecode", "false");
    g_ini.set("BoostGD", "predecode_threads", "2");
    g_ini.set("BoostGD", "skip_intro", "true");
    g_ini.set("BoostGD", "object_pool", "true");
    g_ini.set("BoostGD", "object_pool_size", "4096");
    g_ini.set("BoostGD", "trigger_cache", "true");
    g_ini.set("BoostGD", "plist_binary", "true");
    g_ini.set("BoostGD", "plist_cache_dir", "plist_cache");
    g_ini.set("BoostGD", "skip_shake_flash", "false");

    g_ini.set("BoostRenderAdv", "atlas_merge", "false");
    g_ini.set("BoostRenderAdv", "atlas_size", "2048");
    g_ini.set("BoostRenderAdv", "frustum_cull", "true");
    g_ini.set("BoostRenderAdv", "fbo_cache", "false");
    g_ini.set("BoostRenderAdv", "fbo_pool_size", "8");
    g_ini.set("BoostRenderAdv", "triple_buffer", "false");
    g_ini.set("BoostRenderAdv", "disable_aa", "false");
    g_ini.set("BoostRenderAdv", "blend_optimize", "true");

    g_ini.set("BoostCocos", "particle_throttle", "true");
    g_ini.set("BoostCocos", "particle_max", "300");
    g_ini.set("BoostCocos", "texcache_preload", "false");
    g_ini.set("BoostCocos", "batch_force", "false");
    g_ini.set("BoostCocos", "label_cache", "true");
    g_ini.set("BoostCocos", "scheduler_skip", "true");
    g_ini.set("BoostCocos", "drawcall_sort", "false");
    g_ini.set("BoostCocos", "index_buffer_gen", "true");

    g_ini.set("BoostSysAdv", "ftz_daz", "true");
    g_ini.set("BoostSysAdv", "spectre_off", "false");
    g_ini.set("BoostSysAdv", "io_priority", "true");
    g_ini.set("BoostSysAdv", "mem_priority", "true");
    g_ini.set("BoostSysAdv", "stack_trim", "true");

    g_ini.set("BoostPipeline", "pipe_drawsort", "false");
    g_ini.set("BoostPipeline", "scissor_tight", "false");
    g_ini.set("BoostPipeline", "vertex_dedup", "true");
    g_ini.set("BoostPipeline", "halfres_effects", "false");
    g_ini.set("BoostPipeline", "shader_simplify", "false");
    g_ini.set("BoostPipeline", "batch_coalesce", "false");
    g_ini.set("BoostPipeline", "effect_lod", "false");
    g_ini.set("BoostPipeline", "effect_lod_min_fps", "0");
    g_ini.set("BoostPipeline", "static_batch", "false");
    g_ini.set("BoostPipeline", "drawcall_budget", "false");

    g_ini.set("BoostNetwork", "dns_prefetch", "true");
    g_ini.set("BoostNetwork", "http_pool", "true");
    g_ini.set("BoostNetwork", "online_block_gameplay", "true");
    g_ini.set("BoostNetwork", "server_cache", "true");
    g_ini.set("BoostNetwork", "winsock_opt", "true");

    g_ini.set("BoostAudio", "fmod_channel_limit", "true");
    g_ini.set("BoostAudio", "fmod_max_channels", "16");
    g_ini.set("BoostAudio", "fmod_software_mix", "true");
    g_ini.set("BoostAudio", "audio_thread_pin", "true");
    g_ini.set("BoostAudio", "sound_preload", "true");
    g_ini.set("BoostAudio", "audio_ram_compress", "false");

    g_ini.set("BoostExtreme", "etw_disable", "true");
    g_ini.set("BoostExtreme", "wer_disable", "true");
    g_ini.set("BoostExtreme", "smartscreen_off", "true");
    g_ini.set("BoostExtreme", "numa_aware", "true");
    g_ini.set("BoostExtreme", "huge_pages", "false");
    g_ini.set("BoostExtreme", "prefetcher_off", "false");
    g_ini.set("BoostExtreme", "workingset_lock", "true");
    g_ini.set("BoostExtreme", "gpu_thread_prio", "true");
    g_ini.set("BoostExtreme", "thread_qos", "true");
    g_ini.set("BoostExtreme", "present_skip_idle", "false");
    g_ini.set("BoostExtreme", "halfres_render", "false");
    g_ini.set("BoostExtreme", "halfres_filter", "nearest");
    g_ini.set("BoostExtreme", "halfres_scale", "50");
    g_ini.set("BoostExtreme", "halfres_sharpen", "0.6");

    m_dirty = true;
    m_savedAt = isRu ? "загружен пресет СТАБИЛЬНЫЙ — нажмите Сохранить" : "STABLE preset loaded — press Save to persist";
}

void EditorApp::applyNewPcPreset() {
    // ── PERFORMANCE preset: balanced for modern PCs (4+ cores, discrete GPU) ─
    bool isRu = (m_language == "ru");

    g_ini.set("ANGLE", "backend", "d3d11");
    g_ini.set("ANGLE", "unlock_fps_cap", "false");
    g_ini.set("ANGLE", "force_no_vsync", "false");
    g_ini.set("ANGLE", "frame_pacing", "true");
    g_ini.set("ANGLE", "frame_pacing_target", "0");

    g_ini.set("Boost", "enabled", "true");
    g_ini.set("Boost", "gpu_forcer", "true");
    g_ini.set("Boost", "fast_allocator", "true");
    g_ini.set("Boost", "timer_fix", "true");
    g_ini.set("Boost", "thread_boost", "true");
    g_ini.set("Boost", "cpu_affinity", "0");
    g_ini.set("Boost", "sse_math", "true");
    g_ini.set("Boost", "power_boost", "true");

    g_ini.set("BoostAdvanced", "tex_compress", "false");
    g_ini.set("BoostAdvanced", "nvapi_profile", "true");
    g_ini.set("BoostAdvanced", "shader_cache", "true");
    g_ini.set("BoostAdvanced", "shader_cache_dir", "shader_cache");
    g_ini.set("BoostAdvanced", "large_address_aware", "true");
    g_ini.set("BoostAdvanced", "gl_state_dedup", "true");
    g_ini.set("BoostAdvanced", "working_set_prefetch", "false");
    g_ini.set("BoostAdvanced", "fmod_tuning", "false");
    g_ini.set("BoostAdvanced", "async_asset_loader", "false");
    g_ini.set("BoostAdvanced", "async_loader_threads", "2");
    g_ini.set("BoostAdvanced", "precise_sleep", "true");
    g_ini.set("BoostAdvanced", "heap_compact_interval", "0");
    g_ini.set("BoostAdvanced", "d3d11_multithread", "false");

    g_ini.set("BoostRender", "depth_off", "false");
    g_ini.set("BoostRender", "mipmap_off", "false");
    g_ini.set("BoostRender", "noop_finish", "true");
    g_ini.set("BoostRender", "noop_geterror", "true");
    g_ini.set("BoostRender", "vbo_pool", "false");
    g_ini.set("BoostRender", "vbo_pool_size_mb", "16");
    g_ini.set("BoostRender", "vertex_compress", "false");
    g_ini.set("BoostRender", "instancing", "false");
    g_ini.set("BoostRender", "dyn_resolution", "false");
    g_ini.set("BoostRender", "dyn_res_target_fps", "60");

    g_ini.set("BoostIO", "fast_io", "true");
    g_ini.set("BoostIO", "ramdisk_cache", "false");
    g_ini.set("BoostIO", "ramdisk_path", "");
    g_ini.set("BoostIO", "loader_cache", "true");
    g_ini.set("BoostIO", "iocp_loader", "false");
    g_ini.set("BoostIO", "iocp_loader_concurrency", "0");

    g_ini.set("BoostCPU", "sse_memcpy", "false");
    g_ini.set("BoostCPU", "scene_bvh", "false");
    g_ini.set("BoostCPU", "string_intern", "false");
    g_ini.set("BoostCPU", "mimalloc_full", "false");
    g_ini.set("BoostCPU", "silent_debug", "true");

    g_ini.set("BoostSystem", "wddm_priority", "true");
    g_ini.set("BoostSystem", "game_mode", "true");
    g_ini.set("BoostSystem", "smart_cpu_pin", "false");
    g_ini.set("BoostSystem", "mitigation_off", "false");

    g_ini.set("BoostLatency", "allow_tearing", "false");
    g_ini.set("BoostLatency", "waitable_swap", "true");
    g_ini.set("BoostLatency", "frame_pacing", "true");
    g_ini.set("BoostLatency", "frame_pacing_target", "0");
    g_ini.set("BoostLatency", "mmcss_pro_audio", "true");
    g_ini.set("BoostLatency", "shader_warmup", "true");
    g_ini.set("BoostLatency", "low_latency", "true");
    g_ini.set("BoostLatency", "anti_stutter", "true");
    g_ini.set("BoostLatency", "present_hints", "true");

    g_ini.set("BoostGD", "level_predecode", "false");
    g_ini.set("BoostGD", "predecode_threads", "2");
    g_ini.set("BoostGD", "skip_intro", "true");
    g_ini.set("BoostGD", "object_pool", "true");
    g_ini.set("BoostGD", "object_pool_size", "4096");
    g_ini.set("BoostGD", "trigger_cache", "true");
    g_ini.set("BoostGD", "plist_binary", "true");
    g_ini.set("BoostGD", "plist_cache_dir", "plist_cache");
    g_ini.set("BoostGD", "skip_shake_flash", "false");

    g_ini.set("BoostRenderAdv", "atlas_merge", "false");
    g_ini.set("BoostRenderAdv", "atlas_size", "2048");
    g_ini.set("BoostRenderAdv", "frustum_cull", "true");
    g_ini.set("BoostRenderAdv", "fbo_cache", "false");
    g_ini.set("BoostRenderAdv", "fbo_pool_size", "8");
    g_ini.set("BoostRenderAdv", "triple_buffer", "false");
    g_ini.set("BoostRenderAdv", "disable_aa", "false");
    g_ini.set("BoostRenderAdv", "blend_optimize", "true");

    g_ini.set("BoostCocos", "particle_throttle", "true");
    g_ini.set("BoostCocos", "particle_max", "300");
    g_ini.set("BoostCocos", "texcache_preload", "false");
    g_ini.set("BoostCocos", "batch_force", "false");
    g_ini.set("BoostCocos", "label_cache", "true");
    g_ini.set("BoostCocos", "scheduler_skip", "true");
    g_ini.set("BoostCocos", "drawcall_sort", "false");
    g_ini.set("BoostCocos", "index_buffer_gen", "true");

    g_ini.set("BoostSysAdv", "ftz_daz", "true");
    g_ini.set("BoostSysAdv", "spectre_off", "false");
    g_ini.set("BoostSysAdv", "io_priority", "true");
    g_ini.set("BoostSysAdv", "mem_priority", "true");
    g_ini.set("BoostSysAdv", "stack_trim", "true");

    g_ini.set("BoostPipeline", "pipe_drawsort", "false");
    g_ini.set("BoostPipeline", "scissor_tight", "false");
    g_ini.set("BoostPipeline", "vertex_dedup", "true");
    g_ini.set("BoostPipeline", "halfres_effects", "false");
    g_ini.set("BoostPipeline", "shader_simplify", "false");
    g_ini.set("BoostPipeline", "batch_coalesce", "false");
    g_ini.set("BoostPipeline", "effect_lod", "false");
    g_ini.set("BoostPipeline", "effect_lod_min_fps", "0");
    g_ini.set("BoostPipeline", "static_batch", "false");
    g_ini.set("BoostPipeline", "drawcall_budget", "false");

    g_ini.set("BoostNetwork", "dns_prefetch", "true");
    g_ini.set("BoostNetwork", "http_pool", "true");
    g_ini.set("BoostNetwork", "online_block_gameplay", "true");
    g_ini.set("BoostNetwork", "server_cache", "true");
    g_ini.set("BoostNetwork", "winsock_opt", "true");

    g_ini.set("BoostAudio", "fmod_channel_limit", "true");
    g_ini.set("BoostAudio", "fmod_max_channels", "16");
    g_ini.set("BoostAudio", "fmod_software_mix", "true");
    g_ini.set("BoostAudio", "audio_thread_pin", "true");
    g_ini.set("BoostAudio", "sound_preload", "true");
    g_ini.set("BoostAudio", "audio_ram_compress", "false");

    g_ini.set("BoostExtreme", "etw_disable", "true");
    g_ini.set("BoostExtreme", "wer_disable", "true");
    g_ini.set("BoostExtreme", "smartscreen_off", "true");
    g_ini.set("BoostExtreme", "numa_aware", "true");
    g_ini.set("BoostExtreme", "huge_pages", "false");
    g_ini.set("BoostExtreme", "prefetcher_off", "false");
    g_ini.set("BoostExtreme", "workingset_lock", "true");
    g_ini.set("BoostExtreme", "gpu_thread_prio", "true");
    g_ini.set("BoostExtreme", "thread_qos", "true");
    g_ini.set("BoostExtreme", "present_skip_idle", "false");
    g_ini.set("BoostExtreme", "halfres_render", "false");
    g_ini.set("BoostExtreme", "halfres_filter", "nearest");
    g_ini.set("BoostExtreme", "halfres_scale", "50");
    g_ini.set("BoostExtreme", "halfres_sharpen", "0.6");

    m_dirty = true;
    m_savedAt = isRu ? "загружен пресет ПРОИЗВОДИТЕЛЬНОСТЬ — нажмите Сохранить" : "PERFORMANCE preset loaded — press Save to persist";
}

// ────────────────────────────────────────────────────────────────────────────
// Render
// ────────────────────────────────────────────────────────────────────────────

void EditorApp::renderFrame() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##root", nullptr, wflags);
    ImGui::PopStyleVar(3);

    renderTopBar();

    // Add small margin around content panels
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

    ImGui::BeginGroup();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x -= 20.0f; // offset right margin
    float footerH = 64.0f;
    ImVec2 contentSize(avail.x, avail.y - footerH);

    float sidebarW = 280.0f; // vertical sidebar width

    // Left Sidebar: Categories Navigation
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 12));
    ImGui::BeginChild("##sidebar", ImVec2(sidebarW, contentSize.y), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
        renderSidebar();
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 10.0f);

    // Right Area: Option List Table
    ImGui::BeginChild("##content", ImVec2(0, contentSize.y), ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
        renderOptionList();
    ImGui::EndChild();
    ImGui::PopStyleVar(1);

    ImGui::EndGroup();

    renderFooter();
    renderConfirmModal();

    ImGui::End();
}

void EditorApp::renderTopBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.071f, 0.071f, 0.090f, 1.0f));
    ImGui::BeginChild("##topbar", ImVec2(0, 56), false);
    ImGui::Dummy(ImVec2(12, 6));
    ImGui::SameLine();
    ImGui::PushFont(nullptr);
    ImGui::TextColored(ImVec4(0.612f, 0.494f, 1.000f, 1.0f), "ReviANGLE Studio");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1.0f), "  by Reviusion");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();

    bool isRu = (m_language == "ru");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
                       isRu ? "%s   |   %d настроек в %d разделах" : "%s   |   %d options across %d sections",
                       m_iniPath.c_str(),
                       (int)schemaAll().size(),
                       (int)schemaSections().size());

    // Search bar (right-aligned).
    char buf[128];
    std::strncpy(buf, m_search.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    float searchW = 280.0f;
    float langBtnW = 85.0f;

    // Language selector (RU / EN)
    ImGui::SameLine(ImGui::GetWindowWidth() - searchW - 16 - langBtnW - 12);
    if (ImGui::Button(isRu ? "Язык: RU" : "Lang: EN", ImVec2(langBtnW, 0))) {
        m_language = isRu ? "en" : "ru";
        g_ini.set("ANGLE", "language", m_language);
        m_dirty = true;
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - searchW - 16);
    ImGui::SetNextItemWidth(searchW);
    if (ImGui::InputTextWithHint("##search", isRu ? "поиск настроек..." : "search options...", buf, sizeof(buf))) {
        m_search = buf;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void EditorApp::renderSidebar() {
    const auto& secs = schemaSections();
    bool isRu = (m_language == "ru");

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f)); // Left align button text
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));

    for (int i = 0; i < (int)secs.size(); ++i) {
        const std::string& sec = secs[i];
        
        int totalInSec = 0;
        int matchInSec = 0;
        for (const auto& o : schemaAll()) {
            if (o.section == sec) {
                totalInSec++;
                if (matchOption(o, m_search, isRu)) {
                    matchInSec++;
                }
            }
        }

        // If search is active and nothing matches in this section, skip it
        if (!m_search.empty() && matchInSec == 0) {
            continue;
        }

        bool isActive = (m_currentSectionIdx == i && m_search.empty());

        char label[128];
        if (!m_search.empty()) {
            std::snprintf(label, sizeof(label), "%s (%d)###btn_%d", getFriendlySection(sec.c_str(), isRu), matchInSec, i);
        } else {
            std::snprintf(label, sizeof(label), "%s###btn_%d", getFriendlySection(sec.c_str(), isRu), i);
        }

        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.498f, 0.353f, 0.941f, 1.0f)); // Active Neon Violet
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.498f, 0.353f, 0.941f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.612f, 0.494f, 1.000f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.149f, 0.149f, 0.192f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.196f, 0.196f, 0.251f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.78f, 1.0f));
        }

        ImVec2 size(ImGui::GetContentRegionAvail().x, 38.0f);
        
        if (ImGui::Button(label, size)) {
            m_currentSectionIdx = i;
            m_search.clear(); // Clear search on explicit tab click
        }

        if (isActive) {
            ImVec2 pMin = ImGui::GetItemRectMin();
            ImVec2 pMax = ImGui::GetItemRectMax();
            pMax.x = pMin.x + 4.0f; // 4px active vertical bar
            ImGui::GetWindowDrawList()->AddRectFilled(pMin, pMax, ImGui::ColorConvertFloat4ToU32(ImVec4(0.173f, 0.714f, 0.490f, 1.0f)), 6.0f, ImDrawFlags_RoundCornersLeft);
        }

        ImGui::PopStyleColor(4);
    }

    if (!m_search.empty()) {
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.173f, 0.714f, 0.490f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.173f, 0.714f, 0.490f, 0.3f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.173f, 0.714f, 0.490f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.173f, 0.714f, 0.490f, 1.0f));

        int totalMatches = 0;
        for (const auto& o : schemaAll()) {
            if (matchOption(o, m_search, isRu)) {
                totalMatches++;
            }
        }
        char searchLabel[128];
        std::snprintf(searchLabel, sizeof(searchLabel), isRu ? "Результаты (%d)" : "Search Results (%d)", totalMatches);
        
        ImGui::Button(searchLabel, ImVec2(ImGui::GetContentRegionAvail().x, 38.0f));
        ImGui::PopStyleColor(4);
    }

    // ── Preset Buttons ─────────────────────────────────────────────────────
    // Push remaining content to the bottom of the sidebar
    float remaining = ImGui::GetContentRegionAvail().y;
    if (remaining > 120.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remaining - 116.0f);
    } else {
        ImGui::Dummy(ImVec2(0, 8));
    }
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));

    // ── OLD PC preset button (orange) ────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.45f, 0.20f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.55f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.75f, 0.35f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button(isRu ? "СТАБИЛЬНЫЙ" : "STABLE",
                      ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
        applyOldPcPreset();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isRu ? "Безопасные настройки для любого ПК"
                               : "Safe defaults for any PC");
    }
    ImGui::PopStyleColor(4);

    ImGui::Dummy(ImVec2(0, 4));

    // ── NEW PC preset button (cyan/blue) ──────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.60f, 0.85f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.70f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.50f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button(isRu ? "ПРОИЗВОДИТЕЛЬНОСТЬ" : "PERFORMANCE",
                      ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
        applyNewPcPreset();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isRu ? "Сбалансированный пресет для производительности"
                               : "Balanced performance preset");
    }
    ImGui::PopStyleColor(4);

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PopStyleVar(2);

    ImGui::PopStyleVar(3);
}

void EditorApp::renderOptionList() {
    const auto& secs = schemaSections();
    bool isRu = (m_language == "ru");
    const auto& all = schemaAll();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2(8, 12));

    auto renderOptionsTable = [&](const std::vector<int>& indices) {
        ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | 
                                ImGuiTableFlags_RowBg | 
                                ImGuiTableFlags_BordersOuter | 
                                ImGuiTableFlags_BordersInnerH;
        if (ImGui::BeginTable("##options_table", 2, flags)) {
            ImGui::TableSetupColumn("OptionInfo", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("OptionControl", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            
            for (int idx : indices) {
                const auto& o = all[idx];
                ImGui::PushID(idx);
                ImGui::TableNextRow();
                
                // Column 0: Details
                ImGui::TableNextColumn();
                
                ImVec4 dotCol = statusColor(o.status);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float fontSize = ImGui::GetFontSize();
                ImVec2 center(pos.x + 6.0f, pos.y + fontSize * 0.5f);
                float radius = 4.0f;
                ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(dotCol));
                
                ImGui::Dummy(ImVec2(12.0f, fontSize));
                ImGui::SameLine(0.0f, 6.0f);
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::Text("%s", getFriendlyName(o.section, o.key, isRu));
                ImGui::PopStyleColor();
                
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.65f, 1.0f));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(isRu ? o.desc_ru : o.desc_en);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
                char metaBuf[512];
                std::snprintf(metaBuf, sizeof(metaBuf), 
                              isRu ? "[%s > %s]  |  По умолчанию: %s  |  Статус: %s" 
                                   : "[%s > %s]  |  Default: %s  |  Status: %s",
                              o.section, o.key, o.default_value, o.status);
                ImGui::TextUnformatted(metaBuf);
                ImGui::PopStyleColor();
                
                // Column 1: Control
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                
                const std::string current = g_ini.get(o.section, o.key);
                switch (o.type) {
                    case OptType::Bool: {
                        bool v = parseBoolStr(current);
                        if (ImGui::Checkbox("##checkbox", &v)) {
                            g_ini.set(o.section, o.key, v ? "true" : "false");
                            m_dirty = true;
                        }
                        break;
                    }
                    case OptType::Int: {
                        int v = std::atoi(current.c_str());
                        if (ImGui::InputInt("##input", &v, 1, 10)) {
                            if (v < o.min_int) v = o.min_int;
                            if (v > o.max_int) v = o.max_int;
                            char buf[24];
                            std::snprintf(buf, sizeof(buf), "%d", v);
                            g_ini.set(o.section, o.key, buf);
                            m_dirty = true;
                        }
                        break;
                    }
                    case OptType::String:
                    case OptType::Hex: {
                        char buf[256];
                        std::strncpy(buf, current.c_str(), sizeof(buf) - 1);
                        buf[sizeof(buf) - 1] = '\0';
                        if (ImGui::InputText("##input_text", buf, sizeof(buf))) {
                            g_ini.set(o.section, o.key, buf);
                            m_dirty = true;
                        }
                        break;
                    }
                    case OptType::Enum: {
                        std::vector<std::string> choices;
                        {
                            std::string s = o.enum_values ? o.enum_values : "";
                            size_t start = 0;
                            while (start <= s.size()) {
                                size_t comma = s.find(',', start);
                                if (comma == std::string::npos) comma = s.size();
                                std::string item = s.substr(start, comma - start);
                                while (!item.empty() && std::isspace((unsigned char)item.front())) item.erase(item.begin());
                                while (!item.empty() && std::isspace((unsigned char)item.back()))  item.pop_back();
                                if (!item.empty()) choices.push_back(item);
                                if (comma == s.size()) break;
                                start = comma + 1;
                            }
                        }
                        int currentIdx = 0;
                        for (int j = 0; j < (int)choices.size(); ++j) {
                            if (choices[j] == current) { currentIdx = j; break; }
                        }
                        std::vector<const char*> cstrs;
                        cstrs.reserve(choices.size());
                        for (auto& c : choices) cstrs.push_back(c.c_str());
                        
                        if (ImGui::Combo("##combo", &currentIdx, cstrs.data(), (int)cstrs.size())) {
                            g_ini.set(o.section, o.key, choices[currentIdx]);
                            m_dirty = true;
                        }
                        break;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };

    if (m_search.empty()) {
        if (m_currentSectionIdx >= 0 && m_currentSectionIdx < (int)secs.size()) {
            const std::string& sec = secs[m_currentSectionIdx];
            std::vector<int> indices;
            for (int i = 0; i < (int)all.size(); ++i) {
                if (all[i].section == sec) {
                    indices.push_back(i);
                }
            }
            if (!indices.empty()) {
                renderOptionsTable(indices);
            }
        }
    } else {
        bool firstHeader = true;
        for (int s = 0; s < (int)secs.size(); ++s) {
            const std::string& sec = secs[s];
            std::vector<int> indices;
            for (int i = 0; i < (int)all.size(); ++i) {
                if (all[i].section == sec && matchOption(all[i], m_search, isRu)) {
                    indices.push_back(i);
                }
            }
            if (!indices.empty()) {
                if (!firstHeader) {
                    ImGui::Dummy(ImVec2(0, 14));
                }
                firstHeader = false;

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.612f, 0.494f, 1.000f, 1.0f));
                ImGui::Text("%s", getFriendlySection(sec.c_str(), isRu));
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 4));

                renderOptionsTable(indices);
            }
        }
    }

    ImGui::PopStyleVar(3);
}


void EditorApp::renderFooter() {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 6));

    bool isRu = (m_language == "ru");

    // Save (highlighted when dirty).
    if (m_dirty) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.173f, 0.714f, 0.490f, 1.0f)); // Mint green
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.224f, 0.812f, 0.569f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.137f, 0.596f, 0.404f, 1.0f));
    }
    if (ImGui::Button(isRu ? "Сохранить  (Ctrl+S)" : "Save  (Ctrl+S)")) doSave();
    if (m_dirty) ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::Button(isRu ? "Перезагрузить с диска" : "Reload from disk")) m_pendingConfirm = ConfirmKind::Reload;
    ImGui::SameLine();
    if (ImGui::Button(isRu ? "Сбросить по умолчанию" : "Reset to defaults")) m_pendingConfirm = ConfirmKind::ResetDefaults;

    // Status text on the right.
    ImGui::SameLine();
    if (m_dirty) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), isRu ? "  *  есть несохраненные изменения" : "  *  unsaved changes");
    } else if (!m_savedAt.empty()) {
        std::string displaySaved = m_savedAt;
        if (isRu) {
            if (displaySaved.rfind("saved ", 0) == 0) {
                displaySaved = "сохранено в " + displaySaved.substr(6);
            } else if (displaySaved.rfind("reloaded ", 0) == 0) {
                displaySaved = "перезагружено в " + displaySaved.substr(9);
            } else if (displaySaved == "defaults loaded — press Save to persist") {
                displaySaved = "загружены настройки по умолчанию — нажмите Сохранить";
            } else if (displaySaved == "old PC preset loaded — press Save to persist") {
                displaySaved = "загружен пресет СТАРОГО ПК — нажмите Сохранить";
            } else if (displaySaved == "new PC preset loaded — press Save to persist") {
                displaySaved = "загружен пресет НОВОГО ПК — нажмите Сохранить";
            }
        }
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "  [OK]  %s", displaySaved.c_str());
    }

    ImGui::PopStyleVar();

    // Ctrl+S keyboard shortcut.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) doSave();
}

void EditorApp::renderConfirmModal() {
    if (m_pendingConfirm == ConfirmKind::None) return;

    bool isRu = (m_language == "ru");
    const char* title = (m_pendingConfirm == ConfirmKind::Reload)
        ? (isRu ? "Перезагрузить с диска?###confirm" : "Reload from disk?###confirm")
        : (isRu ? "Сбросить по умолчанию?###confirm" : "Reset to defaults?###confirm");

    ImGui::OpenPopup(title);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_pendingConfirm == ConfirmKind::Reload) {
            ImGui::TextWrapped(isRu ? "Сбросить несохраненные изменения и перезагрузить %s с диска?"
                                    : "Discard unsaved changes and reload %s from disk?",
                               m_iniPath.c_str());
        } else {
            ImGui::TextWrapped(isRu ? "Сбросить все настройки на значения по умолчанию из схемы? "
                                      "Комментарии и описания будут сохранены; изменятся только строки key=value. "
                                      "Чтобы сохранить изменения на диск, вам все равно нужно будет нажать Сохранить."
                                    : "Reset every option to its schema-defined default? "
                                      "Comments and descriptions are preserved; only "
                                      "key=value lines change. You'll still need to "
                                      "press Save to persist to disk.");
        }
        ImGui::Dummy(ImVec2(0, 8));
        if (ImGui::Button(isRu ? "Да" : "Yes", ImVec2(120, 0))) {
            if (m_pendingConfirm == ConfirmKind::Reload) doReload();
            else                                          doResetDefaults();
            m_pendingConfirm = ConfirmKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(isRu ? "Отмена" : "Cancel", ImVec2(120, 0))) {
            m_pendingConfirm = ConfirmKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
