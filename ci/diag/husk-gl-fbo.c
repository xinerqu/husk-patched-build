/* Test build: is the scene being drawn into an off-screen target that never reaches the screen?
 *
 * What the runs so far established: the scene draws (600-900 draw calls a frame once the scene starts, at frame ~750), the driver
 * reports one GL error in the whole run, and every piece of draw state -- blending, depth, culling, colour mask, scissor -- is
 * identical from frame 100 to frame 1600. So the engine asks for each draw, nothing is clipped or masked, and part of what it draws
 * is simply not in the finished picture.
 *
 * Two things can do that without an error: drawing into a framebuffer object that is never presented (a Compatibility-renderer
 * backbuffer copy), or a framebuffer that is incomplete (a format ANGLE cannot back, which only shows up as a status code).
 *
 * So: watch glBindFramebuffer, and when a non-default framebuffer is bound, ask glCheckFramebufferStatus what it thinks of it. A
 * status other than FRAMEBUFFER_COMPLETE, or the scene being drawn into a framebuffer that never gets unbound for presentation, is
 * the answer this pass is after.
 *
 * Reached through husk's k_adapt table (the path eglGetProcAddress consults). Diagnostic only.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdio.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef int GLsizei;

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_BINDING 0x8CA6

static GLenum (*R_CheckStatus)(GLenum);
static void (*R_GetIntegerv)(GLenum, GLint *);

static long g_frames, g_binds, g_off_screen;
static int g_fbo_now = -1;          /* 0 = the default framebuffer */
static int g_line_budget = 24;

void husk_gldiag_fbo_set_real(void *check, void *getiv)
{
    R_CheckStatus = (GLenum (*)(GLenum))check;
    R_GetIntegerv = (void (*)(GLenum, GLint *))getiv;
}



/* Called after glBindFramebuffer. */
void husk_gldiag_fbo(unsigned target, unsigned id, int draws_since)
{
    g_binds++;
    int was = g_fbo_now;
    g_fbo_now = (int)id;
    if (was == g_fbo_now) return;
    if (id != 0) g_off_screen++;

    if (g_line_budget-- > 0) {
        unsigned status = 0;
        if (id != 0 && R_CheckStatus) {
            /* the status of the target as bound, whichever target the engine used */
            GLenum t = (target == GL_READ_FRAMEBUFFER) ? GL_READ_FRAMEBUFFER : GL_DRAW_FRAMEBUFFER;
            status = R_CheckStatus(t);
        }
        tl_log_line("gl: FBO %s %u @swap %ld draws_since_last=%d%s%s",
                    id ? "bind" : "unbind", id, g_frames, draws_since,
                    id ? " status=" : "", id ? (status == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE") : "");
        if (id && status != GL_FRAMEBUFFER_COMPLETE && status != 0)
            tl_log_line("gl:   framebuffer %u is incomplete: %#x%s", id, status,
                        status == 0x8CD6 ? " (INCOMPLETE_ATTACHMENT)" :
                        status == 0x8CD7 ? " (INCOMPLETE_MISSING_ATTACHMENT)" :
                        status == 0x8CDD ? " (UNSUPPORTED: the format is not renderable here)" :
                        status == 0x8D56 ? " (INCOMPLETE_MULTISAMPLE)" : "");
    }
    (void)draws_since;
}

/* Once per ~100 frames: where are the draws going? */
void husk_gldiag_fbo_frame(unsigned long swap)
{
    g_frames = (long)swap;
    static long last;
    if (g_frames - last < 100) return;
    last = g_frames;
    tl_log_line("gl: FBO SNAPSHOT @swap %ld fb=%d binds=%ld offscreen_binds=%ld",
                g_frames, g_fbo_now, g_binds, g_off_screen);
}
