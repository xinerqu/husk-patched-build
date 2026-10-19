/* Test build: is the pixel data actually reaching the textures?
 *
 * Everything that could stop a draw has been ruled out on this report: the draws are issued (600-900 a frame), no draw state moves
 * (blend, depth, culling, colour mask, scissor identical from frame 100 to 1600), and every draw goes to the default framebuffer
 * (`fb=0` throughout), so nothing is left in an off-screen target. The driver reports one GL error in the whole run.
 *
 * What is left is that the draws happen and the picture is still missing pieces: the textures they sample hold nothing. The upload
 * paths are the ones never watched: glTexImage2D only sizes a texture, the pixels usually arrive by glTexSubImage2D (or its
 * compressed form) -- and neither was wrapped.
 *
 * So: watch every upload call and report one that the driver refuses, plus a tally of the formats actually used. A refused
 * glTexSubImage2D leaves a texture that exists, binds, and samples as empty -- exactly the shape of the symptom.
 *
 * Reached through husk's k_adapt table (the path eglGetProcAddress consults). Diagnostic only.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;

static GLenum (*R_GetError)(void);
static void (*R_GetIntegerv)(GLenum, GLint *);

#define GL_BINDING_2D 0x8069

static long g_images, g_subs, g_compressed, g_refused;
static int g_budget = 30;

/* Formats seen, so the log says which ones this scene really uses. */
typedef struct { GLenum f; long n; } fmtcount;
static fmtcount g_fmts[32];
static int g_nfmts;

void husk_gldiag_upload_set_real(void *get_error, void *get_ivec)
{
    R_GetError = (GLenum (*)(void))get_error;
    R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec;
}

static const char *fmt_name(GLenum f)
{
    switch (f) {
    case 0x1908: return "GL_RGBA";
    case 0x1907: return "GL_RGB";
    case 0x8229: return "GL_R8";
    case 0x8051: return "GL_RGB8";
    case 0x8058: return "GL_RGBA8";
    case 0x881A: return "GL_RGBA16F";
    case 0x8814: return "GL_RGBA32F";
    case 0x8C43: return "GL_SRGB8_ALPHA8";
    case 0x83F0: return "GL_COMPRESSED_RGB_S3TC_DXT1";
    case 0x83F3: return "GL_COMPRESSED_RGBA_S3TC_DXT5";
    case 0x8D64: return "GL_ETC1_RGB8";
    case 0x9274: return "GL_COMPRESSED_RGB8_ETC2";
    case 0x9278: return "GL_COMPRESSED_RGBA8_ETC2_EAC";
    case 0x93B0: return "GL_COMPRESSED_RGBA_ASTC_4x4";
    case 0x8E8C: return "GL_COMPRESSED_RGBA_BPTC";
    case 0x1401: return "GL_UNSIGNED_BYTE";
    case 0x1406: return "GL_FLOAT";
    default: return NULL;
    }
}

static void count_format(GLenum f)
{
    for (int i = 0; i < g_nfmts; i++) if (g_fmts[i].f == f) { g_fmts[i].n++; return; }
    if (g_nfmts < 32) { g_fmts[g_nfmts].f = f; g_fmts[g_nfmts].n = 1; g_nfmts++; }
}

static GLuint bound_2d(void)
{
    GLint v = 0;
    if (R_GetIntegerv) R_GetIntegerv(GL_BINDING_2D, &v);
    return (GLuint)v;
}

static void refused(const char *fn, GLenum internal, GLenum format, GLenum type, int w, int h, GLenum err)
{
    if (err == 0) return;
    g_refused++;
    if (g_budget-- <= 0) return;
    const char *fi = fmt_name(internal), *ff = fmt_name(format), *ft = fmt_name(type);
    tl_log_line("gl: %s REFUSED internal=%s(%#x) format=%s(%#x) type=%s(%#x) %dx%d err=%#x tex=%u",
                fn, fi ? fi : "?", internal, ff ? ff : "?", format, ft ? ft : "?", type, w, h, err, bound_2d());
}

/* The wrappers hand their results here. */
void husk_gldiag_image(GLenum internal, GLenum format, GLenum type, int w, int h, unsigned err)
{
    g_images++;
    count_format(internal);
    refused("glTexImage2D", internal, format, type, w, h, err);
}

void husk_gldiag_subimage(GLenum format, GLenum type, int w, int h, unsigned err)
{
    g_subs++;
    refused("glTexSubImage2D", 0, format, type, w, h, err);
}

void husk_gldiag_compressed(GLenum internal, int w, int h, int bytes, unsigned err)
{
    g_compressed++;
    count_format(internal);
    refused("glCompressedTexImage2D", internal, 0, 0, w, h, err);
    (void)bytes;
}

/* Once per 100 frames: how much uploading happened, and in which formats. */
void husk_gldiag_upload_tick(unsigned long swap)
{
    static long last;
    if ((long)swap - last < 300) return;
    last = (long)swap;
    char fl[640]; size_t k = 0; fl[0] = 0;
    for (int i = 0; i < g_nfmts && k + 40 < sizeof(fl); i++) {
        const char *n = fmt_name(g_fmts[i].f);
        k += (size_t)snprintf(fl + k, sizeof(fl) - k, "%s×%ld ", n ? n : "?", g_fmts[i].n);
    }
    tl_log_line("gl: UPLOAD @swap %lu images=%ld subimages=%ld compressed=%ld refused=%ld | %s",
                swap, g_images, g_subs, g_compressed, g_refused, fl);
}
