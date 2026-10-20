/* Test build: a frame's draws summed per texture, so a texture that draws nothing (or too little) stands out.
 *
 * Everything measured so far on this report is clean: 543 texture uploads all succeed, no GL error in the run, every draw state
 * identical across 1500 frames, every draw to the default framebuffer. What is missing is a group of things -- ground, trees, grass,
 * road, rocks and the moving player -- while two followers, their shadows and the UI draw. The player moves, so this is not one
 * backdrop image: the missing things are a set of draws.
 *
 * A per-draw log of a 700-draw frame cannot be read. Per texture it can: which textures drew, how many times, and how many triangles
 * came out. A texture that is uploaded (the upload pass already proved that) yet draws on three or four triangles, or none, is the
 * one whose batch is sick.
 *
 * Capture: the first frame with more than 100 draws starts it (the loading screens draw twice), then two frames are summed and
 * printed. The counter is read at the END of a frame, and the frame hook uses its own before resetting it -- the earlier attempt at
 * this read the count before the frame had drawn anything and captured a loading screen.
 *
 * Reached through husk's k_adapt table. Diagnostic only: every wrapper calls through.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef int GLint;
typedef int GLsizei;

#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_CURRENT_PROGRAM 0x8B8D

static void (*R_GetIntegerv)(GLenum, GLint *);

#define MAXTEX 1024
struct entry { int tex, program; long draws, tris; };
static struct entry g_tab[MAXTEX];
static int g_ntab;

static long g_frame, g_draws_frame;
static int g_armed;            /* saw a frame with > 100 draws */
static int g_done;             /* the capture has been printed: never arm again */
static int g_frames_left;      /* how many more frames to sum */

static struct entry *slot(int tex, int program)
{
    for (int i = 0; i < g_ntab; i++) if (g_tab[i].tex == tex && g_tab[i].program == program) return &g_tab[i];
    if (g_ntab >= MAXTEX) return NULL;
    g_tab[g_ntab].tex = tex;
    g_tab[g_ntab].program = program;
    g_tab[g_ntab].draws = 0;
    g_tab[g_ntab].tris = 0;
    return &g_tab[g_ntab++];
}

void husk_gldiag_sum_set_real(void *get_ivec) { R_GetIntegerv = (void (*)(GLenum, GLint *))get_ivec; }

/* Called by the draw wrappers, after the draw.
 *
 * Every draw is counted, always: that count is how the first scene frame is recognised (the loading screens draw twice). Only the
 * per-texture summing is gated, so the summary describes the summed frames alone and not the whole session. */
void husk_gldiag_sum_draw_inst(unsigned mode, int count, unsigned type, const void *indices, int instances)
{
    (void)type; (void)indices;
    g_draws_frame++;                       /* always: this is the frame's draw count */
    if (g_frames_left <= 0 || !R_GetIntegerv) return;   /* summing only while a capture runs */

    GLint tex = 0, prog = 0;
    R_GetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    R_GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    /* GL_TRIANGLES (4) is the mode the 2D renderer uses for these batches; each instance repeats the whole element set. */
    long tris = (mode == 4) ? count / 3 : (mode == 5) ? (count >= 3 ? count - 2 : 0) : 1;
    if (tris < 0) tris = 0;
    if (instances > 1) tris *= instances;

    struct entry *e = slot(tex, prog);
    if (!e) return;
    e->draws++;
    e->tris += tris;
}

/* The plain forms, for the few draws that are not instanced (lines, SDF text): one instance. */
void husk_gldiag_sum_draw(unsigned mode, int count, unsigned type, const void *indices)
{
    husk_gldiag_sum_draw_inst(mode, count, type, indices, 1);
}


/* Called at the end of every frame, with the frame number.
 *
 * The frame that trips the threshold is summed in full as well (draw_count is set before this call), so the two summed frames are
 * whole ones. The earlier attempt at this reset the count first, so the arming frame was summed as an empty one. */
void husk_gldiag_sum_frame(unsigned long swap)
{
    long drew = g_draws_frame;      /* this frame's count, before it is reset */
    g_frame = (long)swap;
    g_draws_frame = 0;

    if (g_done) return;

    if (!g_armed) {
        /* The loading screens draw twice a frame; the scene draws hundreds. Arm on the first such frame. */
        if (drew > 100) { g_armed = 1; g_frames_left = 2; }
        else return;
    }

    g_frames_left--;
    if (g_frames_left > 0) return;

    /* last summed frame: print what it held, ordered by triangle count so the thin ones are at the top */
    for (int i = 0; i < g_ntab; i++) {
        int worst = i;
        for (int j = i + 1; j < g_ntab; j++) if (g_tab[j].tris < g_tab[worst].tris) worst = j;
        struct entry t = g_tab[i]; g_tab[i] = g_tab[worst]; g_tab[worst] = t;
    }
    tl_log_line("gl: BATCHSUM @swap %ld draws=%ld textures=%d (two frames summed)", g_frame, drew, g_ntab);
    for (int i = 0; i < g_ntab && i < 200; i++)
        tl_log_line("gl:   tex=%d program=%d draws=%ld tris=%ld", g_tab[i].tex, g_tab[i].program, g_tab[i].draws, g_tab[i].tris);
    g_done = 1;                     /* one capture per run: resetting g_armed alone would arm again next frame */
    g_armed = 0;
    g_ntab = 0;
    g_frames_left = 0;
}
