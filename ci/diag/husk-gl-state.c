/* Test build: what does the renderer's state look like while the ground, grass and following Pokémon are missing?
 *
 * The last several rounds looked for a GL error and found the GL layer clean: 200,000 bindings, one refused float-texture filter
 * (now worked around), no error from the engine, a steady 30 fps, and content that simply is not drawn. Nothing reports that.
 *
 * A draw that is silently dropped is almost always one of three states being wrong: the scissor box (clip_children, viewport
 * clipping), blending (a layer with blend disabled hides everything below it), or the colour mask (channels written as 0). None
 * of those raises an error, and none is visible in the engine's own log.
 *
 * So: count draws and watch those three, and print a line only when something changes -- a scissor box collapsing to nothing, a
 * blend mode switching off, a colour mask clearing -- with the frame it happened on. A run where the areas disappear then says
 * exactly which state moved, and when.
 *
 * Reached through husk's k_adapt table, which is the only path eglGetProcAddress consults (the earlier rounds wrapped elsewhere
 * and never ran). Diagnostic only: every wrapper calls through.
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
typedef int GLsizei;

static long g_draws, g_frames;
static int g_said;

#define GL_SCISSOR_BOX 0x0C10
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_ENABLE_BIT 0x00002000

void husk_gldiag_frame(unsigned long swap) { g_frames = (long)swap; }

/* ------------------------------------------------------------ scissor box */

static int g_sx = -1, g_sy = -1, g_sw = -1, g_sh = -1;

void husk_gldiag_scissor(int x, int y, int w, int h)
{
    if (x == g_sx && y == g_sy && w == g_sw && h == g_sh) return;
    g_sx = x; g_sy = y; g_sw = w; g_sh = h;
    if (g_said++ < 40)
        tl_log_line("gl: STATE scissor(%d, %d, %d, %d)%s @swap %ld", x, y, w, h,
                    (w <= 0 || h <= 0) ? "  <-- EMPTY: everything inside it is clipped away" : "", g_frames);
}

/* ---------------------------------------------------------------- blending */

static int g_blend_on = -1;
static unsigned g_bsrc = 0, g_bdst = 0;

void husk_gldiag_blend_enabled(int on)
{
    if (on == g_blend_on) return;
    g_blend_on = on;
    if (g_said++ < 40)
        tl_log_line("gl: STATE glEnable(GL_BLEND) %s @swap %ld%s", on ? "on" : "off", g_frames,
                    on ? "" : "  <-- blending off: anything drawn with alpha is written opaque or hidden");
}

void husk_gldiag_blend_mode(unsigned src, unsigned dst)
{
    if (src == g_bsrc && dst == g_bdst) return;
    g_bsrc = src; g_bdst = dst;
    if (g_said++ < 40)
        tl_log_line("gl: STATE glBlendFunc(%#x, %#x)%s @swap %ld", src, dst,
                    (src == 0 /* ZERO */) ? "  <-- source factor ZERO: nothing from this draw reaches the screen" : "", g_frames);
}

/* ---------------------------------------------------------------- draw count */

void husk_gldiag_draw(void)
{
    g_draws++;
    /* one line per 5,000 draws: a scene that stops drawing says so by staying flat */
    if (g_draws % 5000 == 0)
        tl_log_line("gl: DRAWS %ld so far @swap %ld (scissor %d,%d %dx%d, blend %d)",
                    g_draws, g_frames, g_sx, g_sy, g_sw, g_sh, g_blend_on);
}
