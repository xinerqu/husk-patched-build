/* ANGLE refuses a legal glTexParameteri on a valid, bound texture after a burst of texture churn (probe from the report after
 * #140: isTexture=1, anglebinding=708, leftover=0, deleted@0). Android's own GLES driver accepts the same call.
 *
 * This retries such a call once, after putting the binding down and setting it again -- a no-op for a driver that was right, and
 * a repair of ANGLE's view of the object for one that was not.
 *
 * IMPORTANT: every function this module calls must be the *real* one handed to husk_gl_angle_retry_set_real() at installation.
 * Calling tl_egl_resolve() from inside a wrapper returns the wrapper again and recurses until the stack dies -- that is what
 * happened to the first attempt at this fix, which crashed on the first frame.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef float GLfloat;

static GLenum (*R_GetError)(void);
static void (*R_BindTexture)(GLenum, GLuint);
static void (*R_TexParameteri)(GLenum, GLenum, GLint);
static void (*R_TexParameterf)(GLenum, GLenum, GLfloat);
static void (*R_GetIntegerv)(GLenum, GLint *);
static void (*R_GenerateMipmap)(GLenum);

#define GL_TEXTURE_2D 0x0DE1
#define GL_NO_ERROR 0
#define GL_BINDING_2D 0x8069

static int g_repaired;
static int g_gave_up;

/* Called once, by the wrappers, with the pointers they were installed with. */
void husk_gl_angle_retry_set_real(void *get_error, void *bind_texture, void *tex_param_i, void *tex_param_f,
                                  void *get_ivec, void *generate_mipmap)
{
    R_GetError = (GLenum (*)(void))get_error;
    R_BindTexture = (void (*)(GLenum, GLuint))bind_texture;
    R_TexParameteri = (void (*)(GLenum, GLenum, GLint))tex_param_i;
    R_TexParameterf = (void (*)(GLenum, GLenum, GLfloat))tex_param_f;
    R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec;
    R_GenerateMipmap = (void (*)(GLenum))generate_mipmap;
}

static bool ready(void) { return R_GetError && R_BindTexture && R_TexParameteri && R_GetIntegerv; }

static GLuint bound_2d(GLenum target)
{
    GLint v = 0;
    R_GetIntegerv(target == GL_TEXTURE_2D ? GL_BINDING_2D : GL_BINDING_2D, &v);
    return (GLuint)v;
}

/* Unbind and bind again: ANGLE rebuilds its entry for the object; a correct driver just moves the binding. */
static bool rebind(GLenum target, GLuint id)
{
    R_BindTexture(target, 0);
    R_BindTexture(target, id);
    return R_GetError() == GL_NO_ERROR;
}

/* Called from the wrapper after the driver reported GL_INVALID_ENUM. True: repaired, the caller reports nothing. */
bool husk_gl_angle_retry_i(GLenum target, GLenum pname, GLint value, GLuint bound)
{
    if (!ready()) return false;
    if (!bound || !rebind(target, bound)) { if (g_gave_up++ < 5) tl_log_line("gl: could not rebind texture %u (target %#x)", bound, target); return false; }
    R_TexParameteri(target, pname, value);
    if (R_GetError() != GL_NO_ERROR) { if (g_gave_up++ < 5) tl_log_line("gl: retry failed for texture %u pname %#x", bound, pname); return false; }
    if (g_repaired++ < 20) tl_log_line("gl: repaired glTexParameteri(%#x, %#x, %d) on texture %u by rebinding", target, pname, value, bound);
    return true;
}

bool husk_gl_angle_retry_f(GLenum target, GLenum pname, GLfloat value, GLuint bound)
{
    if (!ready() || !R_TexParameterf) return false;
    if (!bound || !rebind(target, bound)) { if (g_gave_up++ < 5) tl_log_line("gl: could not rebind texture %u (target %#x)", bound, target); return false; }
    R_TexParameterf(target, pname, value);
    if (R_GetError() != GL_NO_ERROR) { if (g_gave_up++ < 5) tl_log_line("gl: retry failed for texture %u pname %#x", bound, pname); return false; }
    if (g_repaired++ < 20) tl_log_line("gl: repaired glTexParameterf(%#x, %#x, %g) on texture %u by rebinding", target, pname, (double)value, bound);
    return true;
}

bool husk_gl_angle_retry_mipmap(GLenum target)
{
    if (!ready() || !R_GenerateMipmap) return false;
    GLuint bound = bound_2d(target);
    if (!bound || !rebind(target, bound)) return false;
    R_GenerateMipmap(target);
    return R_GetError() == GL_NO_ERROR;
}

/* The wrappers ask for the id they should retry on: what the target holds, read with the real entry point. */
GLuint husk_gl_angle_bound(GLenum target)
{
    if (!R_GetIntegerv) return 0;
    return bound_2d(target);
}
