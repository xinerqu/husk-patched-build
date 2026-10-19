/* A test build's texture diagnostics: catches a GL_INVALID_ENUM at the call that raised it, and says enough around it to tell
 * what went wrong -- the texture object, its binding, the last calls, and whether the renderer was mid-frame.
 *
 * What a log needs for "textures load but do not draw" (the report after #140's directory fix):
 *
 *   1. the GPU's format capabilities       (husk-gl-diag.c: texture formats, sizes, GL error output)
 *   2. the failing call and its arguments  (here: glTexImage2D/glTexStorage2D/glTexParameteri/... with named enums)
 *   3. the texture object and its state    (here: the id, the bound target, how many times it was bound/uploaded/deleted)
 *   4. the surrounding GL calls            (tl_egl_recent_calls: the ring of the last 256 entry points)
 *   5. the frame it happened on            (here: swap counter + whether it was between swap and clear)
 *
 * Nothing here changes behaviour: every wrapper calls through and returns.
 *
 * Diagnostic build only: not part of the fix proposed in #140.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef int GLsizei;
typedef void *GLvoid;

void *tl_egl_resolve(const char *name);
void tl_egl_recent_calls(char *out, size_t n, int count);

static GLenum (*x_glGetError)(void);
static GLint (*x_glGetIntegerv)(GLenum, GLint *);

static int g_reported;
static unsigned long g_swap;
static GLuint g_last_created, g_bind2d_for_last;

#define GL_INVALID_ENUM 0x0500
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_BINDING_3D 0x806A
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_MAX_TEXTURE_SIZE 0x0D33

/* ------------------------------------------------------------------ names */

static const char *name_of(GLenum v)
{
    static __thread char buf[40];
    switch (v) {
    case 0x1908: return "GL_RGBA";
    case 0x1907: return "GL_RGB";
    case 0x1909: return "GL_LUMINANCE";
    case 0x190A: return "GL_LUMINANCE_ALPHA";
    case 0x8227: return "GL_RG";
    case 0x8229: return "GL_R8";
    case 0x822B: return "GL_RG8";
    case 0x8051: return "GL_RGB8";
    case 0x8058: return "GL_RGBA8";
    case 0x881A: return "GL_RGBA16F";
    case 0x8814: return "GL_RGBA32F";
    case 0x8C3A: return "GL_R11F_G11F_B10F";
    case 0x8C43: return "GL_SRGB8_ALPHA8";
    case 0x83F0: return "GL_COMPRESSED_RGB_S3TC_DXT1_EXT";
    case 0x83F1: return "GL_COMPRESSED_RGBA_S3TC_DXT1_EXT";
    case 0x83F2: return "GL_COMPRESSED_RGBA_S3TC_DXT3_EXT";
    case 0x83F3: return "GL_COMPRESSED_RGBA_S3TC_DXT5_EXT";
    case 0x8D64: return "GL_ETC1_RGB8_OES";
    case 0x9278: return "GL_COMPRESSED_RGBA8_ETC2_EAC";
    case 0x9274: return "GL_COMPRESSED_RGB8_ETC2";
    case 0x8DBB: return "GL_COMPRESSED_RED_RGTC1";
    case 0x8DBD: return "GL_COMPRESSED_RG_RGTC2";
    case 0x8E8C: return "GL_COMPRESSED_RGBA_BPTC_UNORM";
    case 0x93B0: return "GL_COMPRESSED_RGBA_ASTC_4x4_KHR";
    case 0x1401: return "GL_UNSIGNED_BYTE";
    case 0x1403: return "GL_UNSIGNED_SHORT";
    case 0x1406: return "GL_FLOAT";
    case 0x8363: return "GL_UNSIGNED_SHORT_5_6_5";
    case 0x2600: return "GL_NEAREST";
    case 0x2601: return "GL_LINEAR";
    case 0x812F: return "GL_CLAMP_TO_EDGE";
    case 0x2901: return "GL_REPEAT";
    case 0x2800: return "GL_TEXTURE_MAG_FILTER";
    case 0x2801: return "GL_TEXTURE_MIN_FILTER";
    case 0x2802: return "GL_TEXTURE_WRAP_S";
    case 0x2803: return "GL_TEXTURE_WRAP_T";
    case 0x813C: return "GL_TEXTURE_MAX_LEVEL";
    case 0x813D: return "GL_TEXTURE_MAX_LOD";
    case 0x8134: return "GL_TEXTURE_MIN_LOD";
    case 0x84FE: return "GL_TEXTURE_MAX_ANISOTROPY_EXT";
    case 0x8B9D: return "GL_TEXTURE_SWIZZLE_RGBA";
    case 0x8A48: return "GL_TEXTURE_SRGB_DECODE_EXT";
    case 0x0DE1: return "GL_TEXTURE_2D";
    case 0x806F: return "GL_TEXTURE_3D";
    case 0x8513: return "GL_TEXTURE_CUBE_MAP";
    case 0x8C1A: return "GL_TEXTURE_2D_ARRAY";
    default: break;
    }
    snprintf(buf, sizeof(buf), "%#x", v);
    return buf;
}

/* ------------------------------------------------- the state, at the moment it fails */

static void dump_state(const char *fn)
{
    if (!x_glGetIntegerv) x_glGetIntegerv = (GLint (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
    if (!x_glGetIntegerv) return;
    GLint tex2d = -1, tex3d = -1, cube = -1, active = -1, prog = -1;
    x_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
    x_glGetIntegerv(GL_TEXTURE_BINDING_3D, &tex3d);
    x_glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &cube);
    x_glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    x_glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    tl_log_line("gl:   after %s: binding2D=%d binding3D=%d bindingCube=%d activeUnit=%#x program=%d swap=%lu",
                fn, tex2d, tex3d, cube, (unsigned)active, prog, g_swap);
    static char ring[2048];
    tl_egl_recent_calls(ring, sizeof(ring), 24);
    tl_log_line("gl:   the calls before it: %.900s", ring);
}

void husk_gl_probe(const char *fn, unsigned bound2d, unsigned err);
void husk_gl_hist_gen(unsigned n, const unsigned *ids);
void husk_gl_hist_bind(unsigned target, unsigned id);
void husk_gl_hist_upload(unsigned target, int level, int border);
void husk_gl_hist_delete(int n, const unsigned *ids);

static void note(const char *fn, const char *args, long w, long h)
{
    if (!x_glGetError) x_glGetError = (GLenum (*)(void))tl_egl_resolve("glGetError");
    if (!x_glGetError) return;
    GLenum e = x_glGetError();
    if (!e) return;
    if (g_reported++ > 40) return;
    tl_log_line("gl: %s(%s) -> error %#x%s%s", fn, args ? args : "", e, (w || h) ? "" : "",
                e == GL_INVALID_ENUM ? " (GL_INVALID_ENUM)" : e == 0x0501 ? " (GL_INVALID_VALUE)" : e == 0x0502 ? " (GL_INVALID_OPERATION)" : "");
    if (w || h) tl_log_line("gl:   size %ldx%ld", w, h);
    dump_state(fn);
    husk_gl_probe(fn, g_bind2d_for_last, e);
}

/* ------------------------------------------------------------- the wrappers */

static void (*r_BindTexture)(GLenum, GLuint);
static void w_BindTexture(GLenum target, GLuint id)
{
    r_BindTexture(target, id);
    if (target == 0x0DE1) { g_bind2d_for_last = id; husk_gl_hist_bind(target, id); }
}

static void (*r_DeleteTextures)(GLsizei, const GLuint *);
static void w_DeleteTextures(GLsizei n, const GLuint *ids)
{
    if (ids && n > 0) husk_gl_hist_delete((int)n, (const unsigned *)ids);
    r_DeleteTextures(n, ids);
}

static void (*r_GenTextures)(GLsizei, GLuint *);
static void w_GenTextures(GLsizei n, GLuint *ids)
{
    r_GenTextures(n, ids);
    if (n > 0 && ids) { g_last_created = ids[0]; husk_gl_hist_gen((unsigned)n, (const unsigned *)ids); }
}

static void (*r_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void w_TexImage2D(GLenum t, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *px)
{
    r_TexImage2D(t, level, ifmt, w, h, border, fmt, type, px);
    husk_gl_hist_upload(t, level, border);
    char a[160];
    snprintf(a, sizeof(a), "target=%s internal=%s format=%s type=%s level=%d", name_of(t), name_of((GLenum)ifmt), name_of(fmt), name_of(type), level);
    note("glTexImage2D", a, w, h);
}

static void (*r_CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
static void w_CompressedTexImage2D(GLenum t, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei size, const void *px)
{
    r_CompressedTexImage2D(t, level, ifmt, w, h, border, size, px);
    husk_gl_hist_upload(t, level, border);
    char a[160];
    snprintf(a, sizeof(a), "target=%s internal=%s level=%d bytes=%d", name_of(t), name_of(ifmt), level, size);
    note("glCompressedTexImage2D", a, w, h);
}

static void (*r_TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void w_TexStorage2D(GLenum t, GLsizei levels, GLenum ifmt, GLsizei w, GLsizei h)
{
    r_TexStorage2D(t, levels, ifmt, w, h);
    char a[160];
    snprintf(a, sizeof(a), "target=%s internal=%s levels=%d", name_of(t), name_of(ifmt), levels);
    note("glTexStorage2D", a, w, h);
}

static void (*r_TexStorage3D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
static void w_TexStorage3D(GLenum t, GLsizei levels, GLenum ifmt, GLsizei w, GLsizei h, GLsizei d)
{
    r_TexStorage3D(t, levels, ifmt, w, h, d);
    char a[160];
    snprintf(a, sizeof(a), "target=%s internal=%s levels=%d depth=%d", name_of(t), name_of(ifmt), levels, d);
    note("glTexStorage3D", a, w, h);
}

static void (*r_TexParameteri)(GLenum, GLenum, GLint);
static void w_TexParameteri(GLenum t, GLenum pname, GLint v)
{
    r_TexParameteri(t, pname, v);
    char a[192];
    snprintf(a, sizeof(a), "target=%s pname=%s value=%s lastgen=%u bound2D=%u", name_of(t), name_of(pname),
             (pname == 0x2801 || pname == 0x2800 || pname == 0x2802 || pname == 0x2803 || (GLenum)v > 0x10000) ? name_of((GLenum)v) : "",
             g_last_created, g_bind2d_for_last);
    note("glTexParameteri", a, 0, 0);
}

static void (*r_TexParameterf)(GLenum, GLenum, float);
static void w_TexParameterf(GLenum t, GLenum pname, float v)
{
    r_TexParameterf(t, pname, v);
    char a[192];
    snprintf(a, sizeof(a), "target=%s pname=%s value=%g lastgen=%u bound2D=%u", name_of(t), name_of(pname), (double)v, g_last_created, g_bind2d_for_last);
    note("glTexParameterf", a, 0, 0);
}

static void (*r_GenerateMipmap)(GLenum);
static void w_GenerateMipmap(GLenum t)
{
    r_GenerateMipmap(t);
    char a[96];
    snprintf(a, sizeof(a), "target=%s bound2D=%u", name_of(t), g_bind2d_for_last);
    note("glGenerateMipmap", a, 0, 0);
}

/* ------------------------------------------------------------- where it hooks in */

bool husk_gl_diag_wants(const char *name)
{
    static const char *const k[] = {
        "glTexImage2D", "glCompressedTexImage2D", "glTexStorage2D", "glTexStorage3D",
        "glTexParameteri", "glTexParameterf", "glGenerateMipmap", "glDeleteTextures", "glGenTextures", "glBindTexture", NULL
    };
    for (int i = 0; k[i]; i++) if (!strcmp(name, k[i])) return true;
    return false;
}

/* Called from tl_egl_resolve for every entry point the guest asks for: the name decides whether it wants a wrapper, and the
 * real pointer is remembered so the wrapper can call through it. */
void *husk_gl_diag_install(const char *name, void *real)
{
    if (!strcmp(name, "glTexImage2D")) { r_TexImage2D = real; return (void *)w_TexImage2D; }
    if (!strcmp(name, "glCompressedTexImage2D")) { r_CompressedTexImage2D = real; return (void *)w_CompressedTexImage2D; }
    if (!strcmp(name, "glTexStorage2D")) { r_TexStorage2D = real; return (void *)w_TexStorage2D; }
    if (!strcmp(name, "glTexStorage3D")) { r_TexStorage3D = real; return (void *)w_TexStorage3D; }
    if (!strcmp(name, "glTexParameteri")) { r_TexParameteri = real; return (void *)w_TexParameteri; }
    if (!strcmp(name, "glTexParameterf")) { r_TexParameterf = real; return (void *)w_TexParameterf; }
    if (!strcmp(name, "glGenerateMipmap")) { r_GenerateMipmap = real; return (void *)w_GenerateMipmap; }
    if (!strcmp(name, "glDeleteTextures")) { r_DeleteTextures = real; return (void *)w_DeleteTextures; }
    if (!strcmp(name, "glGenTextures")) { r_GenTextures = real; return (void *)w_GenTextures; }
    if (!strcmp(name, "glBindTexture")) { r_BindTexture = real; return (void *)w_BindTexture; }
    return real;
}

/* The swap counter the log quotes: set from the frame loop, so an error can be placed in the run. */
void husk_gl_diag_swap(unsigned long n) { g_swap = n; }
