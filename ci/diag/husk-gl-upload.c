/* Test build: does one texture upload fail, and does the failure belong to that call?
 *
 * The screenshots settled what the symptom is: in the scene that renders wrong, the ground, trees, grass, road, rocks and the player
 * are all absent while two creatures, their shadows and every UI element draw normally. The missing things are the ones cut from a
 * large sheet; the ones that draw are small textures of their own. The run's upload tally also showed one image of 1800x1273.
 *
 * Every earlier attempt to read this from the log was wrong for the same reason: a GL error is a flag the driver keeps until it is
 * read, so any error left by an earlier call is reported against whatever call happens to check next. Two rounds of "REFUSED ... tex=0"
 * were exactly that -- the wrap read glGetError at the wrong moment and printed a made-up texture name.
 *
 * This version cannot make that mistake:
 *
 *   1. drain glGetError until it says GL_NO_ERROR, so nothing from an earlier call can be blamed on this one;
 *   2. make the upload;
 *   3. read glGetError once -- any error it returns was raised by this call;
 *   4. read the binding with the driver's own glGetIntegerv (never a cached value), for the target the upload names.
 *
 * And it prints the format names, if the driver knows them, so a log line reads like the engine's source.
 *
 * Reached through husk's k_adapt table. Diagnostic only, and only for uploads of 512 pixels or more on a side -- the small ones are
 * not what a missing layer is made of, and printing them all would bury the one that matters.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;

#define GL_BINDING_2D 0x8069
#define GL_BINDING_3D 0x806A
#define GL_BINDING_CUBE 0x8514
#define GL_NO_ERROR 0

static GLenum (*R_GetError)(void);
static void (*R_GetIntegerv)(GLenum, GLint *);

void husk_gldiag_upload_set_real(void *get_error, void *get_ivec)
{
    R_GetError = (GLenum (*)(void))get_error;
    R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec;
}

/* Clear whatever an earlier call left, so the next read can only report this call's error. */
unsigned husk_gldiag_drain(void)
{
    if (!R_GetError) return 0;
    for (int i = 0; i < 32; i++) if (R_GetError() == GL_NO_ERROR) break;
    return 0;
}

static unsigned binding_of(unsigned target)
{
    if (!R_GetIntegerv) return 0xFFFFFFFFu;
    GLint v = -1;
    switch (target) {
    case 0x0DE1: R_GetIntegerv(GL_BINDING_2D, &v); break;
    case 0x806F: R_GetIntegerv(GL_BINDING_3D, &v); break;
    case 0x8513: R_GetIntegerv(GL_BINDING_CUBE, &v); break;
    default: R_GetIntegerv(GL_BINDING_2D, &v); break;
    }
    return v < 0 ? 0xFFFFFFFFu : (GLuint)v;
}

static const char *name(GLenum f)
{
    switch (f) {
    case 0x1903: return "GL_RED";
    case 0x8227: return "GL_RG";
    case 0x1906: return "GL_ALPHA";
    case 0x1907: return "GL_RGB";
    case 0x1908: return "GL_RGBA";
    case 0x1909: return "GL_LUMINANCE";
    case 0x190A: return "GL_LUMINANCE_ALPHA";
    case 0x8051: return "GL_RGB8";
    case 0x8058: return "GL_RGBA8";
    case 0x8229: return "GL_R8";
    case 0x822B: return "GL_RG8";
    case 0x8814: return "GL_RGBA32F";
    case 0x881A: return "GL_RGBA16F";
    case 0x8C43: return "GL_SRGB8_ALPHA8";
    case 0x1401: return "GL_UNSIGNED_BYTE";
    case 0x1406: return "GL_FLOAT";
    default: return "?";
    }
}

/* Called after the upload, with an error already attributed to it. */
void husk_gldiag_upload_result(const char *fn, unsigned target, unsigned internal, unsigned format, unsigned type,
                               int w, int h, unsigned err, int level)
{
    if (w < 512 && h < 512) return;                    /* a small upload is not what a missing layer is made of */
    unsigned bound = binding_of(target);
    if (!err) {
        tl_log_line("gl: UPLOAD OK %s internal=%s(%#x) %s(%#x)/%s %dx%d level=%d target=%#x bound=%u",
                    fn, name(internal), internal, name(format), format, name(type), w, h, level, target, bound);
        return;
    }
    tl_log_line("gl: UPLOAD FAILED %s internal=%s(%#x) %s(%#x)/%s %dx%d level=%d target=%#x bound=%u err=%#x",
                fn, name(internal), internal, name(format), format, name(type), w, h, level, target, bound, err);
}
