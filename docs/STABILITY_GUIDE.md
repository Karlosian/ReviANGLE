# ReviANGLE Stability & Troubleshooting Guide

This guide compiles known conflicts, startup issues, and system-level configuration problems when using ReviANGLE, especially under the Vulkan backend or heavily modded environments.

---

## 1. Icon Ninja DEP Violation Crash

### Symptom
On startup, the game crashes immediately with a log entry similar to:
```
== Exception Information ==
Faulty Module: D:\Games\SteamGames\steamapps\common\Geometry Dash\glew32.dll
Faulty Mod: <Unknown>
Exception Code: c0000005 (EXCEPTION_ACCESS_VIOLATION)
Instruction Address: 0x7ffb54209768 (glew32.dll + 0x79768)
Exception Details: Failed to execute memory (DEP violation) at 0x7FFB54209768
Crashed thread: Main

== Stack Trace ==
- glew32.dll + 0x79768 (_glewGetShaderiv + 0x0)
- libcocos2d.dll + 0x86cad (cocos2d::CCGLProgram::logForOpenGLObject + 0x3d)
- undefined0.icon_ninja.dll + 0x159c7
```

### Cause
The mod **Icon Ninja** (`icon_ninja.dll`) attempts to execute memory in a way that violates Windows **Data Execution Prevention (DEP)** policies when interacting with OpenGL calls forwarded through the proxy DLL.

### Solution
Disable or remove the **Icon Ninja** mod from your Geode loader:
1. Open your Geode mods directory (`Geometry Dash/geode/mods`).
2. Delete or move `icon_ninja.geode` (or disable it from the in-game Geode menu if you can boot via DX11 first).

---

## 2. Globed PBO Mapping / Out of Memory Crash (Vulkan Backend)

### Symptom
When starting the game with `backend=vulkan` enabled in `angle_config.ini`, the game crashes on the loading screen with:
```
Reason: PreloadManager: failed to map a PBO, likely ran out of memory! Please report this to the Globed developers and include the latest game log.
Faulty Mod: Globed v2.1.4 (dankmeme.globed2)
Crashed thread: Main
```

### Cause
The crash is caused by a conflict between **Globed's asset preloader** (which maps Pixel Buffer Objects to upload texture data rapidly) and the ReviANGLE **`workingset_lock`** optimization:
1. **`workingset_lock=true`** tells Windows to keep a hot floor of physical memory residency, but originally set an artificial **1.5 GB maximum working set ceiling** for the process.
2. The Vulkan backend in Google ANGLE allocates device-local and host-visible memory blocks (up-front translation pools), which quickly pushes a heavily-modded 64-bit Geometry Dash client (250+ mods) past the 1.5 GB limit.
3. Windows refuses to commit more physical memory for mapping. As a result, the Vulkan driver fails to map the PBO memory range, returning `nullptr`, which triggers the Globed Out of Memory crash.

### Solution
1. **Disable `workingset_lock`**: Open `angle_config.ini` and set:
   ```ini
   workingset_lock=false
   ```
   *(This optimization is disabled by default starting with Vulkan-compatible builds of ReviANGLE to prevent this crash entirely.)*
2. **Upgrade to ReviANGLE x64-aware build**:
   If you must use `workingset_lock=true`, use the latest ReviANGLE build which has been updated to:
   - Identify 64-bit processes (`_WIN64`) and scale memory bounds safely (up to 16 GB).
   - Use Windows `QUOTA_LIMITS_HARDWS_MAX_DISABLE` to ensure the OS never enforces a low maximum working set ceiling under memory pressure, allowing the process to grow dynamically.

---

## 3. Launcher Working Directory (CWD) & Config Ignoring Issues

### Symptom
Changes made to `angle_config.ini` (such as setting `workingset_lock=false`) are seemingly ignored, or the mod behaves as if all experimental features are turned on, causing crashes or background noise even after saving.

### Cause
When Geometry Dash is launched via third-party launchers (like certain versions of Geode, custom Steam shortcuts, or level editors), the current working directory of the process might point to a launcher folder rather than the actual game directory. 
As a result:
1. The relative file-loading system (`std::ifstream("angle_config.ini")`) failed to find the config file next to `GeometryDash.exe`.
2. The proxy DLL silently fell back to the hardcoded default values (which historically had many experimental rendering optimizations and `workingset_lock` set to `true`), causing the Globed crash or rendering glitches.

### Solution
- **Dynamic Absolute Path Resolution**: ReviANGLE has been upgraded to dynamically resolve the location of the `opengl32.dll` at runtime and load `angle_config.ini`, `angle_log.txt`, `shader_cache/`, and `plist_cache/` directly using absolute paths relative to the DLL itself. 
- This guarantees that your configuration changes are **always loaded** and **always applied**, no matter how or where you launch your game from!
