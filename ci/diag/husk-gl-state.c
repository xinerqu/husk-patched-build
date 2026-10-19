/* Test build: what does the renderer's state look like when part of a scene never appears?
 *
 * The scene draws (260,000 draw calls in a run) yet the ground, grass and one sprite are absent, with no error from the engine or
 * from GL. A previous pass ruled out one candidate: the game never calls glScissor at all, so clipping cannot be hiding it. That pass
 * also filled its log with every blend on/off toggle in the first ten frames, so nothing after frame 10 was recorded.
 *
 * This pass records the other states that make a draw do nothing -- blending, depth test and the depth mask, face culling, the colour
 * mask -- each counter kept separately so one chatty state cannot crowd out the rest, plus a full snapshot every 100 frames and the
 * draw count of the frame it belongs to. Comparing a snapshot from a good scene with one from a missing one says which state moved.
 *
 * Reached through husk's k_adapt table, the only path eglGetProcAddress consults. Diagnostic only: every wrapper calls through.
 *
 * Not part of #140's directory fix.
 */
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "husk-tl-bionic.h"

typedef unsigned GLenum;
typedef int GLint;

static long g_frames, g_draws, g_draws_this_frame, g_last_snapshot;
static int g_scissor_on, g_scissor_seen;

/* Each state keeps its own budget: a state that toggles every frame must not use up the others' lines. */
static int g_line_used[8];
static bool room(int k) { return k < 8 && g_line_used[k]++ < 12; }

enum { L_SCISSOR, L_BLEND, L_BLENDFUNC, L_DEPTH, L_DEPTHMASK, L_CULL, L_COLORMASK, L_FRONT };

/* ------------------------------------------------------------------ state */

static int g_blend_on = -1, g_depth_on = -1, g_depthmask = -1, g_cull_on = -1, g_front = -1;
static unsigned g_bsrc = ~0u, g_bdst = ~0u;
static int g_colormask = -1;
static int g_draws_at_last = 0;

void husk_gldiag_frame(unsigned long swap) { g_frames = (long)swap; g_draws_at_last = (int)g_draws_this_frame; g_draws_this_frame = 0; }

void husk_gldiag_scissor(int x, int y, int w, int h)
{
    if (room(L_SCISSOR))
        tl_log_line("gl: STATE scissor(%d, %d, %d, %d)%s @swap %ld", x, y, w, h, (w <= 0 || h <= 0) ? "  <-- EMPTY" : "", g_frames);
}

void husk_gldiag_enable(unsigned cap, int on)
{
    switch (cap) {
    case 0x0BE2: if (g_blend_on != on) { g_blend_on = on; if (room(L_BLEND)) tl_log_line("gl: STATE blend %s @swap %ld", on ? "on" : "off", g_frames); } break;
    case 0x0B71: if (g_depth_on != on) { g_depth_on = on; if (room(L_DEPTH)) tl_log_line("gl: STATE depthTest %s @swap %ld%s", on ? "on" : "off", g_frames, on ? "" : "  <-- depth test off: later draws can paint over everything"); } break;
    case 0x0B44: if (g_cull_on != on) { g_cull_on = on; if (room(L_CULL)) tl_log_line("gl: STATE cullFace %s @swap %ld", on ? "on" : "off", g_frames); } break;
    case 0x0C11: g_scissor_seen = 1; if (g_scissor_on != on) { g_scissor_on = on; if (room(L_SCISSOR)) tl_log_line("gl: STATE scissorTest %s @swap %ld", on ? "on" : "off", g_frames); } break;
    default: break;
    }
}

void husk_gldiag_blendfunc(unsigned src, unsigned dst)
{
    if (src == g_bsrc && dst == g_bdst) return;
    g_bsrc = src; g_bdst = dst;
    if (room(L_BLENDFUNC))
        tl_log_line("gl: STATE blendFunc(%#x, %#x)%s @swap %ld", src, dst,
                    src == 0 ? "  <-- source ZERO: this draw contributes nothing" : dst == 0 ? "  <-- dest ZERO: this draw erases what is under it" : "",
                    g_frames);
}

void husk_gldiag_depthmask(int on)
{
    if (on == g_depthmask) return;
    g_depthmask = on;
    if (room(L_DEPTHMASK))
        tl_log_line("gl: STATE depthMask %s @swap %ld%s", on ? "on" : "off", g_frames,
                    on ? "" : "  <-- off: later geometry cannot be depth-sorted against this");
}

void husk_gldiag_colormask(int r, int g, int b, int a)
{
    int m = (r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0);
    if (m == g_colormask) return;
    g_colormask = m;
    if (room(L_COLORMASK))
        tl_log_line("gl: STATE colorMask %#x @swap %ld%s", m, g_frames, (m & 7) == 0 ? "  <-- RGB masked: draws write nothing" : "");
}

void husk_gldiag_frontface(unsigned mode)
{
    if (mode == (unsigned)g_front) return;
    g_front = (int)mode;
    if (room(L_FRONT)) tl_log_line("gl: STATE frontFace %#x @swap %ld", mode, g_frames);
}

/* ------------------------------------------------------------------ counting */

void husk_gldiag_draw(void) { g_draws++; g_draws_this_frame++; }

/* Every 100 frames: one line with everything, so a good scene and a broken one can be compared line by line. */
void husk_gldiag_tick(void)
{
    if (g_frames - g_last_snapshot < 100) return;
    g_last_snapshot = g_frames;
    tl_log_line("gl: SNAPSHOT @swap %ld draws_last_frame=%d total=%ld | blend=%s(%#x,%#x) depthTest=%s depthMask=%s cull=%s front=%#x colorMask=%#x scissorTest=%s",
                g_frames, g_draws_at_last, g_draws,
                g_blend_on > 0 ? "on" : g_blend_on == 0 ? "off" : "?",
                g_bsrc == ~0u ? 0 : g_bsrc, g_bdst == ~0u ? 0 : g_bdst,
                g_depth_on > 0 ? "on" : g_depth_on == 0 ? "off" : "?",
                g_depthmask > 0 ? "on" : g_depthmask == 0 ? "off" : "?",
                g_cull_on > 0 ? "on" : g_cull_on == 0 ? "off" : "?",
                g_front < 0 ? 0 : (unsigned)g_front,
                g_colormask < 0 ? 0xf : (unsigned)g_colormask,
                g_scissor_on > 0 ? "on" : g_scissor_on == 0 ? "off" : "?");
}
