#pragma once
#include <windows.h>

// Vulkan GL proxy - translates OpenGL function calls to Vulkan operations
// Provides similar interface to glproxy but backed by Vulkan instead of ANGLE

namespace vkgl {
    // Initialize Vulkan rendering backend
    // Called once at DLL startup if REVIANGLE_BACKEND_VULKAN is configured
    void init();

    // Get function pointer for OpenGL function (mapped to Vulkan equivalents)
    void* resolve(const char* name);
}

// Export macros for Vulkan-backed GL functions
#define VKGL_FORWARD(ret, name, sig, args) \
    extern "C" __declspec(dllexport) ret WINAPI gl_##name sig { \
        using Fn = ret (WINAPI *) sig; \
        static Fn fn = (Fn)vkgl::resolve(#name); \
        if (!fn) return (ret)0; \
        return fn args; \
    }

#define VKGL_FORWARD_VOID(name, sig, args) \
    extern "C" __declspec(dllexport) void WINAPI gl_##name sig { \
        using Fn = void (WINAPI *) sig; \
        static Fn fn = (Fn)vkgl::resolve(#name); \
        if (!fn) return; \
        fn args; \
    }
