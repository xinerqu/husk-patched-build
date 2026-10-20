/* Test build: the batches of one frame, in the order the engine draws them.
 *
 * Everything GL-side is clean on this report: 543 texture uploads all succeed, no error in the run, every piece of draw state
 * identical from frame 100 to frame 1600, every draw into the default framebuffer. What is missing is a whole group of things -- the
 * ground, trees, grass, road, rocks and the player -- while two followers, their shadows and all UI draw normally. The player moves,
 * so it is not a static backdrop: the missing group is a set of draws, and the draws are the one thing not yet looked at.
 *
 * So: for one frame, print the sequence of draws -- the shader program, the texture bound at the time, the number of indices, and
 * the number of triangles that comes out of it. A batch with zero triangles, or one that never appears, is the answer; a batch that
 * looks healthy says to look at the vertex data next.
 *
 * The frame is chosen as "the first frame that draws more than 100 times" (the loading screens draw twice), and then the next four
 * after it, so a scene's batches are printed once and the log stays small.
 *
 * Reached through husk's k_adapt table. Diagnostic only.
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
typedef int GLsizei;

#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_CURRENT_PROGRAM 0x8B8D

static void (*R_GetIntegerv)(GLenum, GLint *);

static long g_frame, g_draws_this_frame, g_batches;
static int g_frames_to_print = 5;   /* the scene's first frame plus four */
static int g_started;               /* has a frame crossed the threshold yet */
static int g_printed_this_frame;

void husk_gldiag_batch_set_real(void *get_ivec) { R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec; }

void husk_gldiag_batch_frame(unsigned long swap)
{
    /* a frame with few draws is a loading screen: it does not start the capture */
    if (!g_started && g_draws_this_frame > 100) { g_started = 1; g_printed_this_frame = 0; }
    else if (g_started) g_frames_to_print--;

    g_frame = (long)swap;
    g_draws_this_frame = 0;
    g_batches = 0;
    if (g_frames_to_print <= 0) g_started = 0;
}

static bool capturing(void) { return g_started && g_frames_to_print > 0; }

void husk_gldiag_draw(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    g_draws_this_frame++;
    if (!capturing() || !R_GetIntegerv) return;
    if (g_printed_this_frame > 260) return;          /* a frame is 600-900 draws; the first 260 name the batches */

    GLint prog = 0, tex = 0;
    R_GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    R_GetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    /* triangles from the index count: the modes this renderer uses are triangles (4) or triangle strip (5) */
    long tris = mode == 4 ? count / 3 : mode == 5 ? (count >= 3 ? count - 2 : 0) : -1;
    tl_log_line("gl: BATCH #%ld draw=%d mode=%#x count=%d type=%#x tris=%ld program=%d tex=%d",
                g_frame, ++g_printed_this_frame, mode, count, type, tris, prog, tex);
    (void)indices;
}
