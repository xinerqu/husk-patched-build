/* Test build: what exactly does ANGLE refuse?
 *
 * The hook into k_adapt is live now (148k glBindTexture calls in one run), and the one failure it caught was on
 * GL_TEXTURE_CUBE_MAP, not 2D -- which invalidates the earlier reading of this report. This pass prints the failing call with
 * names, plus what that texture was created with, so the next log says which parameter or format ANGLE declined:
 *
 *   gl: TEXPARAM FAILED target=GL_TEXTURE_CUBE_MAP pname=GL_TEXTURE_MAX_ANISOTROPY_EXT(0x84fe) value=4 err=GL_INVALID_ENUM
 *       created as internal=GL_RGBA8 format=GL_RGBA type=GL_UNSIGNED_BYTE 128x128 level=0 tex=7
 *
 * Diagnostic only: every wrapper calls through and returns what the driver returned.
 * Not part of the fix proposed in #140.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef int GLsizei;

static void (*R_GetIntegerv)(GLenum, GLint *);

#define GL_BINDING_2D 0x8069
#define GL_BINDING_3D 0x806A
#define GL_BINDING_CUBE 0x8514

static long g_binds, g_texparams, g_gens, g_dels, g_reported;
static int g_first_lines;

void husk_gldiag_set_real(void *get_ivec) { R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec; }

static void angle_binding(GLint *twod, GLint *threed, GLint *cube)
{
    *twod = *threed = *cube = -1;
    if (!R_GetIntegerv) return;
    R_GetIntegerv(GL_BINDING_2D, twod);
    R_GetIntegerv(GL_BINDING_3D, threed);
    R_GetIntegerv(GL_BINDING_CUBE, cube);
}

/* ------------------------------------------------------------------- names */

static const char *target_name(GLenum t)
{
    switch (t) {
    case 0x0DE1: return "GL_TEXTURE_2D";
    case 0x806F: return "GL_TEXTURE_3D";
    case 0x8513: return "GL_TEXTURE_CUBE_MAP";
    case 0x8C1A: return "GL_TEXTURE_2D_ARRAY";
    case 0x8505: return "GL_TEXTURE_CUBE_MAP_ARRAY";
    default: return "target?";
    }
}

static const char *pname_name(GLenum p)
{
    switch (p) {
    case 0x2800: return "GL_TEXTURE_MAG_FILTER";
    case 0x2801: return "GL_TEXTURE_MIN_FILTER";
    case 0x2802: return "GL_TEXTURE_WRAP_S";
    case 0x2803: return "GL_TEXTURE_WRAP_T";
    case 0x8072: return "GL_TEXTURE_WRAP_R";
    case 0x813C: return "GL_TEXTURE_MAX_LEVEL";
    case 0x813D: return "GL_TEXTURE_MAX_LOD";
    case 0x8134: return "GL_TEXTURE_MIN_LOD";
    case 0x8135: return "GL_TEXTURE_BASE_LEVEL";
    case 0x84FE: return "GL_TEXTURE_MAX_ANISOTROPY_EXT";
    case 0x8A48: return "GL_TEXTURE_SRGB_DECODE_EXT";
    case 0x8B9D: return "GL_TEXTURE_SWIZZLE_RGBA";
    case 0x8864: return "GL_TEXTURE_BORDER_COLOR";
    case 0x884C: return "GL_TEXTURE_COMPARE_MODE";
    case 0x884D: return "GL_TEXTURE_COMPARE_FUNC";
    default: return "pname?";
    }
}

static const char *fmt_name(GLenum f)
{
    switch (f) {
    case 0x1908: return "GL_RGBA";
    case 0x1907: return "GL_RGB";
    case 0x8227: return "GL_RG";
    case 0x8229: return "GL_R8";
    case 0x822B: return "GL_RG8";
    case 0x8051: return "GL_RGB8";
    case 0x8058: return "GL_RGBA8";
    case 0x881A: return "GL_RGBA16F";
    case 0x8814: return "GL_RGBA32F";
    case 0x8C43: return "GL_SRGB8_ALPHA8";
    case 0x83F0: return "GL_COMPRESSED_RGB_S3TC_DXT1_EXT";
    case 0x83F1: return "GL_COMPRESSED_RGBA_S3TC_DXT1_EXT";
    case 0x83F2: return "GL_COMPRESSED_RGBA_S3TC_DXT3_EXT";
    case 0x83F3: return "GL_COMPRESSED_RGBA_S3TC_DXT5_EXT";
    case 0x8D64: return "GL_ETC1_RGB8_OES";
    case 0x9274: return "GL_COMPRESSED_RGB8_ETC2";
    case 0x9278: return "GL_COMPRESSED_RGBA8_ETC2_EAC";
    case 0x8DBB: return "GL_COMPRESSED_RED_RGTC1";
    case 0x8DBD: return "GL_COMPRESSED_RG_RGTC2";
    case 0x8E8C: return "GL_COMPRESSED_RGBA_BPTC_UNORM";
    case 0x93B0: return "GL_COMPRESSED_RGBA_ASTC_4x4_KHR";
    case 0x1401: return "GL_UNSIGNED_BYTE";
    case 0x1406: return "GL_FLOAT";
    case 0x8363: return "GL_UNSIGNED_SHORT_5_6_5";
    default: return "fmt?";
    }
}

/* ------------------------------------------- what each texture name was made as */

#define MAXT 8192
typedef struct { GLuint id; GLenum internal, format, type, target; GLsizei w, h; GLint level; bool used; } made;
static made g_made[MAXT];

static made *slot_of(GLuint id, bool create)
{
    unsigned h = (id * 2654435761u) % MAXT;
    for (int i = 0; i < 16; i++, h = (h + 1) % MAXT) {
        if (g_made[h].used && g_made[h].id == id) return &g_made[h];
        if (!g_made[h].used) {
            if (!create) return NULL;
            g_made[h].used = true; g_made[h].id = id;
            return &g_made[h];
        }
    }
    return NULL;
}

static GLuint bound_for(GLenum target)
{
    GLint v = 0;
    if (!R_GetIntegerv) return 0;
    switch (target) {
    case 0x0DE1: R_GetIntegerv(GL_BINDING_2D, &v); break;
    case 0x806F: R_GetIntegerv(GL_BINDING_3D, &v); break;
    case 0x8513: R_GetIntegerv(GL_BINDING_CUBE, &v); break;
    default: R_GetIntegerv(GL_BINDING_2D, &v); break;
    }
    return (GLuint)v;
}

void husk_gldiag_teximage(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h, GLenum format, GLenum type)
{
    made *m = slot_of(bound_for(target), true);
    if (!m) return;
    m->target = target; m->level = level; m->internal = internal; m->format = format; m->type = type; m->w = w; m->h = h;
}

/* --------------------------------------------------------------- the hooks */

void husk_gldiag_bind(GLenum target, GLuint id)
{
    g_binds++;
    if (g_first_lines < 6) {
        g_first_lines++;
        GLint a = -1, b = -1, c = -1;
        angle_binding(&a, &b, &c);
        tl_log_line("gl: BINDCALL %s(%#x) id=%u -> ANGLE 2D=%d 3D=%d cube=%d", target_name(target), target, id, a, b, c);
    }
    if (g_binds - g_reported >= 20000) {
        g_reported = g_binds;
        tl_log_line("gl: BINDCALL %ld bindings, %ld params, %ld names made, %ld deleted", g_binds, g_texparams, g_gens, g_dels);
    }
}

void husk_gldiag_texparam(GLenum target, GLenum pname, long value, unsigned err)
{
    g_texparams++;
    if (!err) return;
    GLint a = -1, b = -1, c = -1;
    angle_binding(&a, &b, &c);
    GLuint id = bound_for(target);
    tl_log_line("gl: TEXPARAM FAILED target=%s(%#x) pname=%s(%#x) value=%ld err=%#x | ANGLE 2D=%d 3D=%d cube=%d tex=%u",
                target_name(target), target, pname_name(pname), pname, value, err, a, b, c, id);
    made *m = slot_of(id, false);
    if (m)
        tl_log_line("gl:   that texture was made as internal=%s(%#x) format=%s type=%s %dx%d level=%d",
                    fmt_name(m->internal), m->internal, fmt_name(m->format), fmt_name(m->type), (int)m->w, (int)m->h, (int)m->level);
    else
        tl_log_line("gl:   that texture has no recorded glTexImage2D in this run (tex=%u)", id);
}

void husk_gldiag_gen(int n) { g_gens += n; }
void husk_gldiag_del(int n) { g_dels += n; }
