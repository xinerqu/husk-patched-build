/* Give ANGLE a fixed, never-reused set of texture names, and let the game reuse ids as GL allows.
 *
 * ANGLE, after a burst of texture creation and deletion, answers GL_INVALID_ENUM to a legal glTexParameteri on a texture that
 * glIsTexture calls valid and that is still bound (probe of the report after #140's directory fix: isTexture=1, binding=708,
 * leftover=0, deleted=0; rebinding the object before retrying did not help -- the name itself is what ANGLE has confused).
 *
 * So do not let ANGLE see a name recycled:
 *
 *   game glGenTextures  -> name 708          (an index it chose)
 *   ANGLE glGenTextures -> real name 21      (never released, only ever returned to this pool)
 *   game glBindTexture(708) -> glBindTexture(21)
 *   game glDeleteTextures(708) -> the real name goes back to the pool, 708 may be handed out again by the game
 *
 * ANGLE therefore never deletes a name it later reuses, which is the state machine it gets wrong; the game keeps the semantics GL
 * promises it (a deleted name may be reused, contents undefined).
 *
 * Generic by construction: it sits between the guest and the driver, so every game that uses GL textures goes through it.
 * Nothing changes unless a name is actually recycled.
 *
 * Candidate fix for the rendering half of the report; not part of #140's directory fix.
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

#define MAX_TEX 65536

/* guest id -> real id, and the pool of real ids the game once had. Spare names are what a guest reuses; free ones are what a
 * glGenTextures may hand out. */
static GLuint g_map[MAX_TEX];          /* 0 = the guest id is not in use */
static GLuint *g_pool;                 /* real ids, in the order they were first made */
static int g_pool_n, g_pool_cap;
static char *g_returned;               /* pool index -> returned to the pool by a delete */
static int g_said;

static void (*R_GenTextures)(GLsizei, GLuint *);
static void (*R_DeleteTextures)(GLsizei, const GLuint *);
static void (*R_BindTexture)(GLenum, GLuint);

void husk_gl_names_set_real(void *gen, void *del, void *bind)
{
    R_GenTextures = (void (*)(GLsizei, GLuint *))gen;
    R_DeleteTextures = (void (*)(GLsizei, const GLuint *))del;
    R_BindTexture = (void (*)(GLenum, GLuint))bind;
}

static bool ready(void) { return R_GenTextures && R_DeleteTextures && R_BindTexture; }

/* A real id for a guest id that is being created or reused: one the game has used before if there is one, else a fresh one from
 * ANGLE. The real names are never deleted, so ANGLE never has to recycle one. */
static GLuint real_for(GLuint guest)
{
    for (int i = 0; i < g_pool_n; i++) if (g_returned[i]) { g_returned[i] = 0; if (g_said++ < 5) tl_log_line("gl: name %u reuses real texture %u (ANGLE names are no longer recycled)", guest, g_pool[i]); return g_pool[i]; }
    GLuint fresh = 0;
    R_GenTextures(1, &fresh);
    if (g_pool_n == g_pool_cap) {
        g_pool_cap = g_pool_cap ? g_pool_cap * 2 : 64;
        g_pool = realloc(g_pool, sizeof(GLuint) * (size_t)g_pool_cap);
        g_returned = realloc(g_returned, (size_t)g_pool_cap);
    }
    g_pool[g_pool_n] = fresh; g_returned[g_pool_n] = 0; g_pool_n++;
    return fresh;
}

static void hand_back(GLuint real)
{
    for (int i = 0; i < g_pool_n; i++) if (g_pool[i] == real && !g_returned[i]) { g_returned[i] = 1; return; }
}

/* ---------------------------------------------------------------- the wrappers */

/* The guest's names come from here, not from ANGLE: a small counter that only ever grows, so a name the game deletes is handed
 * back to it again (GL allows that) while the real name behind it is reused from the pool. */
static GLuint g_next_guest = 1;

static void (*r_GenTextures)(GLsizei, GLuint *);
static void w_GenTextures(GLsizei n, GLuint *ids)
{
    if (!ready() || n <= 0 || !ids) { r_GenTextures(n, ids); return; }
    for (GLsizei i = 0; i < n; i++) {
        GLuint guest = g_next_guest++;
        if (guest >= MAX_TEX) { guest = 1; while (guest < MAX_TEX && g_map[guest]) guest++; }
        g_map[guest] = real_for(guest);
        ids[i] = guest;
    }
}

static void (*r_DeleteTextures)(GLsizei, const GLuint *);
static void w_DeleteTextures(GLsizei n, const GLuint *ids)
{
    if (!ready() || n <= 0 || !ids) { r_DeleteTextures(n, ids); return; }
    for (GLsizei i = 0; i < n; i++) {
        GLuint guest = ids[i];
        if (guest && guest < MAX_TEX && g_map[guest]) { hand_back(g_map[guest]); g_map[guest] = 0; }
    }
    /* the real names stay alive in the pool: that is the point */
}

static void (*r_BindTexture)(GLenum, GLuint);
static void w_BindTexture(GLenum target, GLuint id)
{
    GLuint real = (id && id < MAX_TEX && g_map[id]) ? g_map[id] : id;
    r_BindTexture(target, real);
}

/* ---------------------------------------------------------------- where it hooks in */

bool husk_gl_names_wants(const char *name)
{
    return !strcmp(name, "glGenTextures") || !strcmp(name, "glDeleteTextures") || !strcmp(name, "glBindTexture");
}

void *husk_gl_names_install(const char *name, void *real)
{
    if (!strcmp(name, "glGenTextures")) { r_GenTextures = real; return (void *)w_GenTextures; }
    if (!strcmp(name, "glDeleteTextures")) { r_DeleteTextures = real; return (void *)w_DeleteTextures; }
    if (!strcmp(name, "glBindTexture")) { r_BindTexture = real; return (void *)w_BindTexture; }
    return real;
}
