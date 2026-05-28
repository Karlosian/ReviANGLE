// ReviANGLE Uninstaller
// Removes opengl32.dll and related files from the Geometry Dash folder

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

static bool silentMode = false;

static void log(const char* fmt, ...) {
    if (silentMode) return;
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    MessageBoxA(nullptr, buf, "ReviANGLE Uninstaller", MB_OK);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // Check for silent mode flag
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"/silent") == 0 || wcscmp(argv[i], L"/S") == 0) {
                silentMode = true;
                break;
            }
        }
        LocalFree(argv);
    }

    // Get the directory where uninstaller is located (should be GD folder)
    wchar_t exePathW[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    fs::path gdFolder = fs::path(exePathW).parent_path();

    // Verify we're in what looks like a Geometry Dash folder
    bool looksLikeGdFolder = fs::exists(gdFolder / L"GeometryDash.exe") ||
                             fs::exists(gdFolder / L"libcocos2d.dll");

    if (!looksLikeGdFolder && !silentMode) {
        int r = MessageBoxW(nullptr,
            L"Could not find GeometryDash.exe in this folder.\n"
            L"Make sure the uninstaller is in the Geometry Dash folder.\n\n"
            L"Continue anyway?",
            L"ReviANGLE Uninstaller", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return 1;
    }

    int removed = 0;
    int failed = 0;

    // Files to remove
    const wchar_t* files[] = {
        L"opengl32.dll",          // Our ANGLE proxy
        L"gd-angle-editor.exe",   // Config editor
        L"opengl32_new.dll",      // Backup if exists
        L"fps_log.csv",           // FPS log if exists
        nullptr
    };

    for (int i = 0; files[i]; i++) {
        fs::path f = gdFolder / files[i];
        if (fs::exists(f)) {
            try {
                fs::remove(f);
                removed++;
            } catch (...) {
                failed++;
            }
        }
    }

    // Optionally remove config (ask user unless silent)
    fs::path configPath = gdFolder / L"angle_config.ini";
    bool removedConfig = false;
    if (fs::exists(configPath)) {
        if (silentMode) {
            // In silent mode, keep config
        } else {
            int r = MessageBoxW(nullptr,
                L"Remove angle_config.ini (your settings file)?\n\n"
                L"Keep it if you plan to reinstall later.",
                L"ReviANGLE Uninstaller", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDYES) {
                try {
                    fs::remove(configPath);
                    removedConfig = true;
                    removed++;
                } catch (...) {
                    failed++;
                }
            } else if (r == IDCANCEL) {
                return 1; // User cancelled
            }
        }
    }

    // Summary
    if (!silentMode) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Uninstall complete.\n\n"
            "Files removed: %d\n"
            "Failed to remove: %d\n"
            "Config removed: %s\n\n"
            "The game will now use the original OpenGL driver.",
            removed, failed, removedConfig ? "yes" : "no");
        MessageBoxA(nullptr, msg, "ReviANGLE Uninstaller", MB_OK);
    }

    return failed > 0 ? 2 : 0;
}
