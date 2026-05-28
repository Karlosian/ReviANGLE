// Boost: Draw Call Sort
// GD renders sprites in scene graph order, which often alternates between
// different textures/shaders. This causes expensive state changes on the GPU.
// We buffer draw commands and sort them by texture ID before flushing,
// minimizing state switches.

#include <windows.h>
#include <vector>
#include <algorithm>
#include "config.hpp"
#include "gl_proxy.hpp"
#include "angle_loader.hpp"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int          GLint;
typedef int          GLsizei;

using DrawArraysFn  = void(WINAPI*)(GLenum, GLint, GLsizei);
using BindTexFn     = void(WINAPI*)(GLenum, GLuint);
using GetIntegervFn = void(WINAPI*)(GLenum, GLint*);

static DrawArraysFn  s_origDrawArrays = nullptr;
static BindTexFn     s_origBindTex    = nullptr;
static GetIntegervFn s_getIntegerv    = nullptr;
static bool g_active = false;

namespace {

struct DrawCmd {
    GLuint texId;
    GLenum mode;
    GLint  first;
    GLsizei count;
};

std::vector<DrawCmd> g_drawBuffer;
GLuint g_currentTexId = 0;

void flushSorted() {
    if (g_drawBuffer.empty()) return;

    // Sort by texture ID
    std::sort(g_drawBuffer.begin(), g_drawBuffer.end(),
              [](const DrawCmd& a, const DrawCmd& b) { return a.texId < b.texId; });

    GLuint lastTex = ~0u;
    for (auto& cmd : g_drawBuffer) {
        if (cmd.texId != lastTex) {
            s_origBindTex(0x0DE1, cmd.texId); // GL_TEXTURE_2D
            lastTex = cmd.texId;
        }
        s_origDrawArrays(cmd.mode, cmd.first, cmd.count);
    }
    g_drawBuffer.clear();
}

} // anonymous namespace

// Called from wgl_wglSwapBuffers at frame boundary — ensures buffered
// commands are flushed before present, preventing stale texture bindings.
extern "C" void boost_drawcall_sort_flush() { flushSorted(); }

static void WINAPI hooked_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!g_active) {
        if (s_origDrawArrays) s_origDrawArrays(mode, first, count);
        return;
    }

    // Get current texture binding
    GLint texBinding = 0;
    if (s_getIntegerv) s_getIntegerv(0x8069, &texBinding); // GL_TEXTURE_BINDING_2D

    // Buffer the draw command
    g_drawBuffer.push_back({static_cast<GLuint>(texBinding), mode, first, count});
}

static void WINAPI hooked_glBindTexture(GLenum target, GLuint texture) {
    if (s_origBindTex) s_origBindTex(target, texture);
    if (target == 0x0DE1) { // GL_TEXTURE_2D
        g_currentTexId = texture;
    }
}

namespace boost_drawcall_sort {
    void flush() { flushSorted(); }
    void apply() {
        if (!Config::get().drawcall_sort) return;

        s_origDrawArrays = (DrawArraysFn)glproxy::resolve("glDrawArrays");
        s_origBindTex    = (BindTexFn)glproxy::resolve("glBindTexture");
        s_getIntegerv    = (GetIntegervFn)glproxy::resolve("glGetIntegerv");

        if (s_origDrawArrays && s_origBindTex) {
            g_drawBuffer.reserve(256);
            g_active = true;
            angle::log("drawcall_sort: active");
        }
    }

    void* getDrawArraysHook() { return g_active ? (void*)hooked_glDrawArrays : nullptr; }
    void* getBindTexHook()    { return g_active ? (void*)hooked_glBindTexture : nullptr; }
}