// Boost: on-the-fly texture compression
// Intercepts glTexImage2D calls and compresses RGBA8 textures to DXT1 (BC1).
// Result: 4x less VRAM usage, saving precious graphics memory.
//
// KNOWN ISSUE: conflicts with mods that hook glTexImage2D (e.g. Megahack,
// prevter.imageplus). These hooks run AFTER ReviANGLE but BEFORE ANGLE's
// glCompressedTexImage2D, and they may retain pointers to the original
// (now-freed) pixel buffer. When ANGLE later re-processes the texture via
// its internal path, it reads stale memory -> EXCEPTION_ACCESS_VIOLATION.
// Detecting Megahack and disabling tex_compress when present.

#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "config.hpp"
#include "gl_proxy.hpp"
#include "common/dxt1_encoder.hpp"
#include "angle_loader.hpp"

typedef unsigned int GLenum;
typedef int          GLint;
typedef int          GLsizei;

constexpr GLenum GL_RGBA            = 0x1908;
constexpr GLenum GL_UNSIGNED_BYTE   = 0x1401;
constexpr GLenum GL_TEXTURE_2D      = 0x0DE1;
constexpr GLenum GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0;

// Mod compat is now handled centrally in Config::load() (applyModCompat).
// If megahack_detected is set, tex_compress has already been disabled.
// Keep this local check only as a second-layer guard (e.g. late-loaded mods).

using TexImage2DFn = void(WINAPI*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using CompressedTexImage2DFn = void(WINAPI*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);

static TexImage2DFn           s_origTexImage2D = nullptr;
static CompressedTexImage2DFn s_compressedTex  = nullptr;
static bool                   s_active = false;

static void WINAPI hookedTexImage2D(GLenum target, GLint level, GLint internalformat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels)
{
    // only compress RGBA8 2D textures that are multiples of 4 and >= 16px
    if (s_active && target == GL_TEXTURE_2D && format == GL_RGBA &&
        type == GL_UNSIGNED_BYTE && pixels &&
        width >= 16 && height >= 16 && (width & 3) == 0 && (height & 3) == 0)
    {
        size_t compressedSize = (size_t)(width / 4) * (height / 4) * 8;
        std::vector<uint8_t> compressed(compressedSize);

        size_t wrote = dxt1::compress((const uint8_t*)pixels, width, height, compressed.data());
        if (wrote > 0 && s_compressedTex) {
            s_compressedTex(target, level, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                            width, height, border, (GLsizei)wrote, compressed.data());
            return;
        }
    }
    // fallback to original
    if (s_origTexImage2D) s_origTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

namespace boost_tex_compress {
    void apply() {
        if (!Config::get().tex_compress) return;

        // Disabled centrally by Config::load() when Geode/mods are detected.
        if (Config::get().megahack_detected) {
            angle::log("tex_compress: disabled (mod compat active)");
            return;
        }

        s_origTexImage2D = (TexImage2DFn)glproxy::resolve("glTexImage2D");
        s_compressedTex  = (CompressedTexImage2DFn)glproxy::resolve("glCompressedTexImage2D");

        if (s_origTexImage2D && s_compressedTex) {
            s_active = true;
            angle::log("tex_compress: active, RGBA8 -> DXT1");
        }
    }

    // called by gl_proxy when glTexImage2D is invoked
    void* getHook() { return s_active ? (void*)hookedTexImage2D : nullptr; }
}

extern "C" void* gdangle_texCompressGetTexImage2DHook() {
    return s_active ? (void*)hookedTexImage2D : nullptr;
}
