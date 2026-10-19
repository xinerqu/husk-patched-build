/* ANGLE, after a burst of texture creation and deletion, answers GL_INVALID_ENUM to glTexParameteri for a texture that glIsTexture
 * says is valid and that is still bound (probe of the report after #140's directory fix: isTexture=1, binding=708, leftover=0,
 * deleted@0). The game has not done anything wrong, and Android's own GLES driver accepts the same call.
 *
 * So: when a texture parameter call fails with GL_INVALID_ENUM, put the binding down and set it again, then retry once. That is a
 * no-op for a driver that was right, and repairs ANGLE's view of the object for one that was not. Generic: it applies to every
 * game rather than one, and it only fires when the driver has already refused the call.
 *
 * This is the candidate fix for the rendering half of the report; it is not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef float GLfloat;

void *tl_egl_resolve(const char *name);

static GLenum (*xGetError)(void);
static void (*xBindTexture)(GLenum, GLuint);
static void (*xTexParameteri)(GLenum, GLenum, GLint);
static void (*xTexParameterf)(GLenum, GLenum, GLfloat);

#define GL_TEXTURE_2D 0x0DE1
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500

static int g_repaired;

static void resolve(void)
{
    if (!xGetError) xGetError = (GLenum (*)(void))tl_egl_resolve("glGetError");
    if (!xBindTexture) xBindTexture = (void (*)(GLenum, GLuint))tl_egl_resolve("glBindTexture");
    if (!xTexParameteri) xTexParameteri = (void (*)(GLenum, GLenum, GLint))tl_egl_resolve("glTexParameteri");
    if (!xTexParameterf) xTexParameterf = (void (*)(GLenum, GLenum, GLfloat))tl_egl_resolve("glTexParameterf");
}

/* Does the driver accept a bounded round trip on this texture? Leaves the binding as it found it. */
static bool rebind_cycle(GLenum target, GLuint id)
{
    if (!xBindTexture || !xGetError) return false;
    xBindTexture(target, 0);
    xBindTexture(target, id);
    return xGetError() == GL_NO_ERROR;
}

/* Called from the wrapper after the call reported an error: true when the call was repaired and does not need reporting. */
bool husk_gl_param_retry(GLenum target, GLenum pname, GLint ival, GLfloat fval, bool is_float)
{
    resolve();
    if (!xGetError || !xBindTexture || !xTexParameteri) return false;
    GLuint bound = 0;
    /* the id the call was made on: whatever the target holds, which is what failed */
    {
        void (*getiv)(GLenum, GLint *) = (void (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
        if (getiv) getiv(target == GL_TEXTURE_2D ? 0x8069 : target == 0x8513 ? 0x8514 : 0x806A, (GLint *)&bound);
    }
    if (!bound) return false;
    if (!rebind_cycle(target, bound)) return false;
    if (is_float) { if (xTexParameterf) xTexParameterf(target, pname, fval); }
    else xTexParameteri(target, pname, ival);
    GLenum after = xGetError();
    if (after != GL_NO_ERROR) return false;
    if (g_repaired++ < 20)
        tl_log_line("gl: repaired %s(%s, %s) on texture %u by rebinding after ANGLE's GL_INVALID_ENUM",
                    is_float ? "glTexParameterf" : "glTexParameteri", "target", "pname", bound);
    return true;
}

/* Also covers the other call the same failure hits: glGenerateMipmap, which sets the mip range internally. */
bool husk_gl_generate_mipmap_retry(GLenum target)
{
    resolve();
    if (!xGetError || !xBindTexture) return false;
    GLuint bound = 0;
    void (*getiv)(GLenum, GLint *) = (void (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
    if (getiv) getiv(target == GL_TEXTURE_2D ? 0x8069 : 0x806A, (GLint *)&bound);
    if (!bound) return false;
    if (!rebind_cycle(target, bound)) return false;
    void (*gen)(GLenum) = (void (*)(GLenum))tl_egl_resolve("glGenerateMipmap");
    if (!gen) return false;
    gen(target);
    return xGetError() == GL_NO_ERROR;
}
