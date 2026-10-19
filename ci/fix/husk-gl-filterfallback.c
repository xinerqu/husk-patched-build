/* When ANGLE refuses a texture filter, use the one every texture can take.
 *
 * Evidence from the report after #140's directory fix: the single GL error in a 200,000-binding run was
 *
 *   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) -> GL_INVALID_ENUM
 *   on texture 708, made as internal=GL_RGBA32F format=GL_RGBA type=GL_FLOAT 64x64
 *
 * A 32-bit float texture is not filterable on a driver without OES_texture_float_linear, and ANGLE answers the request for LINEAR
 * filtering with INVALID_ENUM (rather than INVALID_OPERATION). Godot then leaves that texture's parameters unset, and the draws
 * that depend on them go blank -- the missing ground and grass in that report. The same run's log on an A15 device shows the same
 * family: "Image format RGBAFloat not supported by hardware, converting to RGBAHalf".
 *
 * So: if the driver refuses a filter setting, set the same parameter to GL_NEAREST for that texture. Nothing happens unless the
 * driver refused, so a texture that takes LINEAR keeps it exactly as the game asked.
 *
 * Generic: it sits on the parameter call every engine uses, and only for the two filter parameters.
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

#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_NEAREST 0x2600
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_OPERATION 0x0502
#define GL_NO_ERROR 0

static void (*R_TexParameteri)(GLenum, GLenum, GLint);
static GLenum (*R_GetError)(void);

static int g_fell_back, g_said;

void husk_gl_filter_set_real(void *tex_param_i, void *get_error)
{
    R_TexParameteri = (void (*)(GLenum, GLenum, GLint))tex_param_i;
    R_GetError = (GLenum (*)(void))get_error;
}

/* Called after the driver refused a parameter call with err. True: repaired (as GL_NEAREST). */
bool husk_gl_filter_fallback(GLenum target, GLenum pname, GLint value, unsigned err)
{
    if (!R_TexParameteri || !R_GetError) return false;
    if (pname != GL_TEXTURE_MAG_FILTER && pname != GL_TEXTURE_MIN_FILTER) return false;
    if (err != GL_INVALID_ENUM && err != GL_INVALID_OPERATION) return false;
    if (value == GL_NEAREST) return false;                 /* already the fallback: nothing better to try */

    R_TexParameteri(target, pname, GL_NEAREST);
    if (R_GetError() != GL_NO_ERROR) return false;         /* the driver refused that too: leave it to the game */
    g_fell_back++;
    if (g_said++ < 8)
        tl_log_line("gl: %s on target %#x was refused; set GL_NEAREST instead (texture keeps filtering, just not smoothed) -- #%d",
                    pname == GL_TEXTURE_MAG_FILTER ? "GL_TEXTURE_MAG_FILTER" : "GL_TEXTURE_MIN_FILTER", target, g_fell_back);
    return true;
}

int husk_gl_filter_fallbacks(void) { return g_fell_back; }
