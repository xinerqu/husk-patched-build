/* A test build's texture diagnostics: catches a GL_INVALID_ENUM at the call that raised it, with the arguments that matter.
 *
 * The first pass (husk-gl-diag.c) showed one `gl: error 0x500` in a run and nothing more: a GL error names no call. These wrappers
 * sit on the texture entry points Godot's Compatibility renderer uses and check glGetError immediately after each, so the log line
 * carries the function and its format/size arguments -- enough to say which texture format ANGLE refused.
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

static GLenum (*x_glGetError)(void);
static void need_error(void);

static int g_reported;

#define GL_INVALID_ENUM 0x0500

/* The second pass only reports, never behaves: every wrapper calls through and returns, whatever happened. */

/* The names Godot's texture storage passes, so a log line reads like the engine's source rather than hex. */
static const char *name_of(GLenum v)
{
    static char buf[32];
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
    case 0x2312: return "GL_COMPRESSED_TEXTURE_FORMATS";
    default: break;
    }
    snprintf(buf, sizeof(buf), "%#x", v);
    return buf;
}

static void note(const char *fn, const char *a, const char *b, const char *c, long n1, long n2)
{
    need_error();
    if (!x_glGetError) return;
    GLenum e = x_glGetError();
    if (!e) return;
    if (g_reported++ > 60) return;
    tl_log_line("gl: %s(%s%s%s%s%s) -> error %#x%s", fn, a ? a : "",
                b ? ", " : "", b ? b : "", c ? ", " : "", c ? c : "", e,
                e == GL_INVALID_ENUM ? " (GL_INVALID_ENUM)" : "");
    if (n1 || n2) tl_log_line("gl:   size %ldx%ld", n1, n2);
}

/* ------------------------------------------------------------- the wrappers */

static void (*r_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void w_TexImage2D(GLenum t, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *px)
{
    r_TexImage2D(t, level, ifmt, w, h, border, fmt, type, px);
    char a[96], b[96];
    snprintf(a, sizeof(a), "internal=%s", name_of((GLenum)ifmt));
    snprintf(b, sizeof(b), "format=%s type=%s", name_of(fmt), name_of(type));
    note("glTexImage2D", a, b, NULL, w, h);
}

static void (*r_CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
static void w_CompressedTexImage2D(GLenum t, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei size, const void *px)
{
    r_CompressedTexImage2D(t, level, ifmt, w, h, border, size, px);
    char a[96], b[64];
    snprintf(a, sizeof(a), "internal=%s", name_of(ifmt));
    snprintf(b, sizeof(b), "bytes=%d", size);
    note("glCompressedTexImage2D", a, b, NULL, w, h);
}

static void (*r_TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void w_TexStorage2D(GLenum t, GLsizei levels, GLenum ifmt, GLsizei w, GLsizei h)
{
    r_TexStorage2D(t, levels, ifmt, w, h);
    char a[96], b[32];
    snprintf(a, sizeof(a), "internal=%s", name_of(ifmt));
    snprintf(b, sizeof(b), "levels=%d", levels);
    note("glTexStorage2D", a, b, NULL, w, h);
}

static void (*r_TexParameteri)(GLenum, GLenum, GLint);
static void w_TexParameteri(GLenum t, GLenum pname, GLint v)
{
    r_TexParameteri(t, pname, v);
    char a[96], b[96];
    snprintf(a, sizeof(a), "pname=%s", name_of(pname));
    snprintf(b, sizeof(b), "value=%s", name_of((GLenum)v));
    note("glTexParameteri", a, b, NULL, 0, 0);
}

static void (*r_TexStorage3D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
static void w_TexStorage3D(GLenum t, GLsizei levels, GLenum ifmt, GLsizei w, GLsizei h, GLsizei d)
{
    r_TexStorage3D(t, levels, ifmt, w, h, d);
    char a[96];
    snprintf(a, sizeof(a), "internal=%s", name_of(ifmt));
    note("glTexStorage3D", a, NULL, NULL, w, h);
}

/* ----------------------------------------------------------- what gets hooked */

/* Called from tl_egl_resolve, on the GL thread, for every entry point the guest asks for: the name decides whether it wants a
 * wrapper, and the real pointer is remembered so the wrapper can call through. */
void *husk_gl_diag_install(const char *name, void *real)
{
    if (!strcmp(name, "glTexImage2D")) { r_TexImage2D = real; return (void *)w_TexImage2D; }
    if (!strcmp(name, "glCompressedTexImage2D")) { r_CompressedTexImage2D = real; return (void *)w_CompressedTexImage2D; }
    if (!strcmp(name, "glTexStorage2D")) { r_TexStorage2D = real; return (void *)w_TexStorage2D; }
    if (!strcmp(name, "glTexStorage3D")) { r_TexStorage3D = real; return (void *)w_TexStorage3D; }
    if (!strcmp(name, "glTexParameteri")) { r_TexParameteri = real; return (void *)w_TexParameteri; }
    return real;
}

bool husk_gl_diag_wants(const char *name)
{
    return !strcmp(name, "glTexImage2D") || !strcmp(name, "glCompressedTexImage2D") || !strcmp(name, "glTexStorage2D")
        || !strcmp(name, "glTexStorage3D") || !strcmp(name, "glTexParameteri");
}

/* glGetError comes in through the same resolver as the wrapped calls, so the first wrapper installs it. */
static void need_error(void)
{
    extern void *tl_egl_resolve(const char *);
    if (!x_glGetError) x_glGetError = (GLenum (*)(void))tl_egl_resolve("glGetError");
}
