/* Test build: the last question about the texture error after #140's directory fix -- is it ANGLE's state, or the guest's own
 * use of a deleted texture? These probes answer it from both sides at the moment glTexParameteri raises GL_INVALID_ENUM:
 *
 *   glIsTexture(id)          does ANGLE still know this name? (false = the guest kept using a deleted texture)
 *   glGetError before/after  is the error consumed by our own query, or was it already pending from an earlier call?
 *   glGetIntegerv again      is the binding the guest sees the same as the one ANGLE reports right after?
 *   the texture's history     generated / bound / uploaded / deleted, and on which frame each happened
 *
 * Plus a per-frame tally of glDeleteTextures, so a "delete storm" that precedes the error is visible at a glance rather than as
 * hundreds of lines.
 *
 * Diagnostic build only: nothing here changes behaviour, and none of it belongs in the fix proposed in #140.
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
typedef unsigned char GLboolean;

void *tl_egl_resolve(const char *name);

static GLenum (*xGetError)(void);
static GLboolean (*xIsTexture)(GLuint);
static void (*xGetIntegerv)(GLenum, GLint *);

#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_BINDING_3D 0x806A
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_INVALID_ENUM 0x0500
#define GL_NO_ERROR 0

/* ------------------------------------------------------- a texture's short life */

/* The ids this run has seen, with the frame of their last event. 4096 is far more than one frame's worth of textures in any game
 * here (the worst run in #140 deleted ~470 in a session). */
typedef struct { GLuint id; unsigned long gen, bind, upload, del; } texhist;
static texhist g_hist[4096];
static unsigned long g_frame;

static texhist *hist_of(GLuint id, bool create)
{
    unsigned h = (id * 2654435761u) % 4096;
    for (int probe = 0; probe < 32; probe++, h = (h + 1) % 4096) {
        if (g_hist[h].id == id) return &g_hist[h];
        if (!g_hist[h].id) { if (!create) return NULL; g_hist[h].id = id; return &g_hist[h]; }
    }
    return NULL;
}

static int g_deleted_this_frame, g_created_this_frame;

void husk_gl_hist_gen(unsigned n, const unsigned *ids)
{
    for (unsigned i = 0; i < n && i < 32; i++) { texhist *t = hist_of(ids[i], true); if (t) { t->gen = g_frame; t->bind = t->upload = t->del = 0; } }
    g_created_this_frame += (int)n;
}

void husk_gl_hist_bind(unsigned target, unsigned id)
{
    if (target != 0x0DE1 || !id) return;
    texhist *t = hist_of(id, true);
    if (t) t->bind = g_frame;
}

void husk_gl_hist_upload(unsigned target, int level, int border)
{
    (void)target; (void)level; (void)border;
    GLuint bound = 0;
    if (!xGetIntegerv) xGetIntegerv = (void (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
    if (xGetIntegerv) xGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint *)&bound);
    texhist *t = bound ? hist_of(bound, true) : NULL;
    if (t) t->upload = g_frame;
}

void husk_gl_hist_delete(int n, const unsigned *ids)
{
    for (int i = 0; i < n && i < 64; i++) { texhist *t = hist_of(ids[i], false); if (t) t->del = g_frame; }
    g_deleted_this_frame += n;
}

/* ------------------------------------------------------------- the probe at the failure */

static int g_probed;

/* Called by the texture wrappers right after a call returned a GL error. Prints what ANGLE itself says about the name the guest
 * is using, which is the one thing the wrappers alone cannot tell. */
void husk_gl_probe(const char *fn, GLuint bound2d, GLenum err)
{
    if (!xIsTexture) xIsTexture = (GLboolean (*)(GLuint))tl_egl_resolve("glIsTexture");
    if (!xGetError) xGetError = (GLenum (*)(void))tl_egl_resolve("glGetError");
    if (!xGetIntegerv) xGetIntegerv = (void (*)(GLenum, GLint *))tl_egl_resolve("glGetIntegerv");
    if (g_probed++ > 6) return;

    GLboolean valid = xIsTexture ? xIsTexture(bound2d) : 2;
    GLint now = -1;
    if (xGetIntegerv) { xGetIntegerv(GL_TEXTURE_BINDING_2D, &now); }
    GLenum after = xGetError ? xGetError() : 0;      /* our own queries must not have left one behind */
    texhist *t = bound2d ? hist_of(bound2d, false) : NULL;

    tl_log_line("gl: PROBE %s err=%#x id=%u isTexture=%d anglebinding=%d leftover=%#x frame=%lu deletesThisFrame=%d createsThisFrame=%d",
                fn, err, bound2d, (int)valid, now, after, g_frame, g_deleted_this_frame, g_created_this_frame);
    if (t) {
        tl_log_line("gl: PROBE id=%u generated@%lu bound@%lu uploaded@%lu deleted@%lu%s", bound2d, t->gen, t->bind, t->upload, t->del,
                    t->del ? "  <-- DELETED BEFORE THIS CALL" : "");
    } else {
        tl_log_line("gl: PROBE id=%u never seen by this build's history (a name ANGLE did not create?)", bound2d);
    }
    if (after == GL_INVALID_ENUM) tl_log_line("gl: PROBE note: the error was still pending after our queries -- it predates %s", fn);
}

/* --------------------------------------------------------------- per-frame summary */

void husk_gl_frame_begin(void)
{
    g_deleted_this_frame = 0;
    g_created_this_frame = 0;
    g_frame++;
}

void husk_gl_frame_end(unsigned long swap)
{
    /* the counters describe the frame just finished, and are cleared for the next one */
    /* One line only when the frame was busy: a quiet frame says nothing worth a log line. */
    if (g_deleted_this_frame + g_created_this_frame >= 16)
        tl_log_line("gl: frame %lu (swap %lu): created %d textures, deleted %d", g_frame, swap, g_created_this_frame, g_deleted_this_frame);
    husk_gl_frame_begin();
}

unsigned long husk_gl_frame(void) { return g_frame; }
